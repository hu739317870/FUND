#include <iostream>
#include <string>
#include <map>
#include <iomanip>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <unordered_map>

using json = nlohmann::json;
using namespace std;

static const double BASE = 1;

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
    std::cout << js_text.size() << std::endl;
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

std::vector<int> generate_random_start_date(int count) {
    return {};
}

void generate_report(
    double grid_size, double sum, double balance, double amount, string fund_code,
    double holdings, double latest_price, double profit,
    const vector<TradeOperation>& operations,
    const vector<TradeOperation>& transacted_operations) 
{
    std::string file_name = fund_code + "_report.txt";
    std::ofstream report(file_name);
    if (!report.is_open()) {
        std::cerr << "无法打开报告文件: " << file_name << std::endl;
        return;
    }
    report << "SUM: " << sum << "  Amount: " << amount << "  Grid Size: " << grid_size << endl;
    report << "Holdings Value: " << holdings * latest_price << "(holds " << holdings << " at price " << latest_price << ")" << "  Balance: " << balance << endl;
    report << "PS: If Holdings Value + Balance < SUM, that shows you lost money at this moment!!!" << endl;
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

void run_grid_strategy(double grid_size, double sum, double amount, const string fund_code) {
    auto net_worth_data = generate_data(fund_code);
    double current_balance = sum;
    double current_holdings = 0;
    double total_profit = 0;
    double current_base_price = net_worth_data.begin()->second;
    vector<TradeOperation> operations;
    vector<TradeOperation> transacted_operations;
    for_each(net_worth_data.begin(), net_worth_data.end(), [&](const auto& item) {
        long timestamp = item.first;
        double price = item.second;
        if (current_base_price * (BASE - grid_size) >= price) {
            TradeOperation operation;
            if (current_balance < amount) {
                operation.money_not_enough = true;
                cout << "Not enough money for this operation." << endl;
            }
            else {
                current_balance -= amount;
                current_holdings += amount / price;
                current_base_price = price;
                operation.buy_timestamp = timestamp;
                operation.buy_price = price;
                operations.push_back(operation);
                cout << "buy time: " << put_time(std::localtime(&timestamp), "%Y-%m-%d") << ", price: " << price << endl;
                cout << "Money left: " << current_balance << endl;
            }
        }
        else if (current_base_price * (BASE + grid_size) <= price) {
            while (!operations.empty()) {
                TradeOperation operation = operations.back();
                if (operation.buy_price * (BASE + grid_size) > price) {
                    break;
                }
                operation.sell_timestamp = timestamp;
                operation.sell_price = price;
                transacted_operations.push_back(operation);
                operations.pop_back();
                double profit = (amount / operation.buy_price) * operation.sell_price - amount;
                total_profit += profit;
                current_balance += (amount / operation.buy_price) * operation.sell_price;
                current_holdings -= amount / operation.buy_price;
                cout << "sell time: " << put_time(std::localtime(&timestamp), "%Y-%m-%d") << ", price: " << price << ", profit: " << profit << endl;
            }
            current_base_price = price; // 更新基准价格
        }
    });
    double latest_price = net_worth_data.rbegin()->second;
    cout << "Total money left: " << current_balance << endl;
    cout << "Total profit: " << total_profit << endl;
    generate_report(
        grid_size, sum, current_balance, amount, fund_code,
        current_holdings, latest_price, total_profit,
        operations, transacted_operations
    );
}

std::unordered_map<std::string, std::string> read_config(const std::string& filename) {
    std::unordered_map<std::string, std::string> config;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "无法打开配置文件：" << filename << std::endl;
        return config;
    }
    std::string line;
    while (std::getline(file, line)) {
        // 忽略空行和注释
        if (line.empty() || line[0] == '#') continue;

        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue; // 格式错误跳过

        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);

        // 去除空格（可选）
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        config[key] = value;
    }
    return config;
}

std::vector<string> parse_array(const std::string& s) {
    std::vector<string> result;
    if (s.size() < 2 || s.front() != '[' || s.back() != ']') {
        std::cerr << "格式错误，不是数组形式" << std::endl;
        return result;
    }
    std::string content = s.substr(1, s.size() - 2);
    std::stringstream ss(content);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item.erase(0, item.find_first_not_of(" \t"));
        item.erase(item.find_last_not_of(" \t") + 1);
        result.push_back(item);
    }
    return result;
}

int main() {
    auto config = read_config("config.txt");
    if (config.empty()) {
        std::cerr << "配置读取失败" << std::endl;
        return 1;
    }

    double grid_size = std::stod(config["grid_size"]);
    double sum = std::stod(config["sum"]);
    double amount = std::stod(config["amount"]);
    auto fund_codes = parse_array(config["fund_codes"]);

    for (const auto& code : fund_codes) {
        std::cout << "Processing fund code: " << code << std::endl;
        run_grid_strategy(grid_size, sum, amount, code);
    }
    return 0;
}
// 编译命令：g++ -o profit profit.cpp -lcurl