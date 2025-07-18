#include <iostream>
#include <string>
#include <map>
#include <iomanip>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <fstream>

#include "GetConfig.hpp"
#include "CppSQLite3/DataBaseStorage.hpp"

using json = nlohmann::json;
using namespace std;

static const double BASE = 1;
static Config CONFIG;

enum Period {
    LAST_3_MONTHS = 0,
    LAST_6_MONTHS,
    LAST_1_YEAR,
    LAST_3_YEARS,
    LAST_5_YEARS,
    SINCE_ESTABLISHED
};

struct Thredhold {
    double percentile_70;
    double percentile_30;
};

struct TradeOperation {
    long buy_timestamp;
    double buy_price;

    long sell_timestamp = 0;
    double sell_price = 0;

    bool money_not_enough = false; // 剩的钱是否够这次买入
};

// 写入回调函数
size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

// 下载网页内容
string fetch_url(const string& url) {
    CURL* curl;
    CURLcode res;
    string response;

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        // 设置回调函数
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        // 忽略 SSL 验证（如果用的是 https）
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
        }

        curl_easy_cleanup(curl);
    }
    return response;
}

map<long, double> generate_data(const string& fund_code) {
    string url = "http://fund.eastmoney.com/pingzhongdata/" + fund_code + ".js";
    map<long, double> net_worth_dict;

    string js_text = fetch_url(url);
    size_t start = js_text.find("var Data_netWorthTrend = ");
    if (start != string::npos) {
        start = js_text.find('[', start);
        size_t end = js_text.find("];", start);
        if (start != string::npos && end != string::npos) {
            string json_str = js_text.substr(start, end - start + 1);
            json j = json::parse(json_str);
            for (const auto& item : j) {
                long timestamp = long(item["x"]) / 1000;
                double net_value = item["y"];
                net_worth_dict[timestamp] = net_value;
            }
        } else {
            cerr << "未找到完整的 JSON 数组" << endl;
        }
    } else {
        cerr << "未找到 Data_netWorthTrend 变量" << endl;
    }
    return net_worth_dict;
}

void generate_report(
    double balance, string fund_code,
    double holdings, double latest_price, double profit,
    const vector<TradeOperation>& operations,
    const vector<TradeOperation>& transacted_operations,
    const string& period) 
{
    std::string file_name = "report/" + fund_code + "_" + period + "_report.txt";
    std::ofstream report(file_name);
    if (!report.is_open()) {
        std::cerr << "无法打开报告文件: " << file_name << std::endl;
        return;
    }
    report << "SUM: " << CONFIG.sum << "  Amount: " << CONFIG.amount << "  Grid Size: " << CONFIG.grid_size << endl;
    report << "Holdings Value: " << holdings * latest_price << "(holds " << holdings << " at price " << latest_price << ")" << "  Balance: " << balance << " Total Value: " << holdings * latest_price + balance << endl;
    report << "PS: If Total Value (Holdings Value + Balance) < SUM, that shows you lost money at this moment!!!" << endl;
    report << "Profit: " << profit << "  Loss" << endl;

    report << endl << "dealed trade: " << transacted_operations.size() << "  not dealed trade: " << operations.size() << endl;
    report << "================================== trade operations ==================================" << endl;
    std::vector<TradeOperation> all_operations;
    all_operations.reserve(operations.size() + transacted_operations.size());
    all_operations.insert(all_operations.end(), operations.begin(), operations.end());
    all_operations.insert(all_operations.end(), transacted_operations.begin(), transacted_operations.end());
    std::sort(all_operations.begin(), all_operations.end(),
        [](const TradeOperation& a, const TradeOperation& b) {
            return a.buy_timestamp < b.buy_timestamp;
        });
    for (const auto& operation : all_operations) {
        report << "Buy Time: " << put_time(std::localtime(&operation.buy_timestamp), "%Y-%m-%d") << ",    Price: " << operation.buy_price << endl;
        if (operation.sell_timestamp != 0) {
            report << "Sell Time: " << put_time(std::localtime(&operation.sell_timestamp), "%Y-%m-%d") << ",    Price: " << operation.sell_price << endl;
        }
        else {
            report << "Sell Time: " << operation.sell_timestamp << ",    Price: " << operation.sell_price << endl;
        }
        report << "----------------------------------------------------------" << endl;
    }
    report.close();
}

std::map<long, double>::const_iterator get_start_date(const map<long, double>& net_worth_data, const std::string& period) {
    auto latest_it = --net_worth_data.end();
    long latest_timestamp = latest_it->first;
    long last_timestamp = 0;

    switch (static_cast<Period>(std::stoi(period))) {
        case LAST_3_MONTHS:
            last_timestamp = latest_timestamp - 90 * 24 * 3600;
            break;
        case LAST_6_MONTHS:
            last_timestamp = latest_timestamp - 180 * 24 * 3600;
            break;
        case LAST_1_YEAR:
            last_timestamp = latest_timestamp - 365 * 24 * 3600;
            break;
        case LAST_3_YEARS:
            last_timestamp = latest_timestamp - 3 * 365 * 24 * 3600;
            break;
        case LAST_5_YEARS:
            last_timestamp = latest_timestamp - 5 * 365 * 24 * 3600;
            break;
        case SINCE_ESTABLISHED:
            return net_worth_data.begin();
        default:
            break;
    }
    auto it = net_worth_data.lower_bound(last_timestamp);
    return it;
}

Thredhold calculate_thresholds(const map<long, double>& net_worth_data) {
    vector<double> values;
    for ( auto [key, value] : net_worth_data) {
        values.push_back(value);
    }
    sort(values.begin(), values.end());
    size_t n = values.size();
    Thredhold thresholds;
    thresholds.percentile_70 = values[n * CONFIG.threshold_high - 1];
    thresholds.percentile_30 = values[n * CONFIG.threshold_low];
    return thresholds;
}

void calculate_profit(
    const std::string& fund_code, const std::string& period, const map<long, double>& net_worth_data, map<long, double>::const_iterator start_date_it)
{
    auto thresholds = calculate_thresholds(net_worth_data);
    double current_balance = CONFIG.sum;
    double current_holdings = 0;
    double total_profit = 0;
    double current_base_price = start_date_it->second;
    vector<TradeOperation> operations;
    vector<TradeOperation> transacted_operations;
    for_each(start_date_it, net_worth_data.end(), [&](const auto& item) {
        long timestamp = item.first;
        double price = item.second;
        if (current_base_price * (BASE - CONFIG.grid_size) >= price and price < thresholds.percentile_70) {
            TradeOperation operation;
            if (current_balance < CONFIG.amount) {
                operation.money_not_enough = true;
                cout << "Not enough money for this operation." << endl;
            }
            else {
                current_balance -= CONFIG.amount;
                current_holdings += CONFIG.amount / price;
                current_base_price = price;
                operation.buy_timestamp = timestamp;
                operation.buy_price = price;
                operations.push_back(operation);
                //cout << "buy time: " << put_time(std::localtime(&timestamp), "%Y-%m-%d") << ", price: " << price << endl;
                //cout << "Money left: " << current_balance << endl;
            }
        }
        else if (current_base_price * (BASE + CONFIG.grid_size) <= price) {
            while (!operations.empty()) {
                TradeOperation operation = operations.back();
                if (operation.buy_price * (BASE + CONFIG.grid_size) > price) {
                    break;
                }
                operation.sell_timestamp = timestamp;
                operation.sell_price = price;
                transacted_operations.push_back(operation);
                operations.pop_back();
                double profit = (CONFIG.amount / operation.buy_price) * operation.sell_price - CONFIG.amount;
                total_profit += profit;
                current_balance += (CONFIG.amount / operation.buy_price) * operation.sell_price;
                current_holdings -= CONFIG.amount / operation.buy_price;
                //cout << "sell time: " << put_time(std::localtime(&timestamp), "%Y-%m-%d") << ", price: " << price << ", profit: " << profit << endl;
            }
            current_base_price = price; // 更新基准价格
        }
    });
    double latest_price = net_worth_data.rbegin()->second;
    cout << "Total money left: " << current_balance << endl;
    cout << "Total profit: " << total_profit << endl;
    generate_report(current_balance, fund_code,
        current_holdings, latest_price, total_profit,
        operations, transacted_operations, period
    );
    DatabaseStorage db_storage;
    db_storage.add(fund_code, period, current_holdings * latest_price + current_balance,
        current_balance, current_holdings * latest_price, total_profit, 0,
        thresholds.percentile_70, thresholds.percentile_30, 0);
}

void run_grid_strategy(const string fund_code) {
    const auto& net_worth_data = generate_data(fund_code);
    if (CONFIG.periods.empty()) {
        cerr << "No periods specified in the configuration." << endl;
        return;
    }

    for (const auto& period : CONFIG.periods) {
        auto start_date_it = get_start_date(net_worth_data, period);
        cout << "Start date: " << put_time(std::localtime(&start_date_it->first), "%Y-%m-%d") << endl;
        calculate_profit(fund_code, period, net_worth_data, start_date_it);
        std::cout << std::endl;
    }
}


int main() {
    GetConfig get_config("config.txt");
    CONFIG = get_config.Get();

    for (const auto& code : CONFIG.fund_codes) {
        std::cout << "Processing fund code: " << code << std::endl;
        run_grid_strategy(code);
    }
    return 0;
}
// 编译命令：g++ -g -o fund main.cpp GetConfig.cpp CppSQLite3/DataBaseStorage.cpp CppSQLite3/CppSQLite3.cpp -lcurl -lsqlite3 -std=c++17