#include <iostream>
#include <string>
#include <map>
#include <iomanip>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <future>
#include <thread>
#include <algorithm>

#include "GetConfig.hpp"
#include "CppSQLite/DataBaseStorage.hpp"

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
    SINCE_ESTABLISHED,
    CUSTOMIZED_TIME
};

struct Thredhold {
    double percentile_high;
    double percentile_low;
};

struct TradeOperation {
    long buy_timestamp = 0;
    double buy_price = 0;

    long sell_timestamp = 0;
    double sell_price = 0;

    bool money_not_enough = false; // 剩的钱是否够这次买入
    bool big_grid_size = false; // 是否是大格子策略
    bool dealed = false;
};

// 写入回调函数
size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

// 下载网页内容
string fetch_url(const string& url, CURLcode& res) {
    CURL* curl;
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
    CURLcode res;

    string js_text = fetch_url(url, res);
    if (res != CURLE_OK) {
        return net_worth_dict;
    }
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

// 异步版本的数据获取函数
std::future<map<long, double>> generate_data_async(const string& fund_code) {
    return std::async(std::launch::async, [fund_code]() -> map<long, double> {
        string url = "http://fund.eastmoney.com/pingzhongdata/" + fund_code + ".js";
        map<long, double> net_worth_dict;
        CURLcode res;

        string js_text = fetch_url(url, res);
        if (res != CURLE_OK) {
            return net_worth_dict;
        }
        
        size_t start = js_text.find("var Data_ACWorthTrend = ");
        if (start != string::npos) {
            start = js_text.find('[', start);
            size_t end = js_text.find("];", start);
            if (start != string::npos && end != string::npos) {
                string json_str = js_text.substr(start, end - start + 1);
                try {
                    json j = json::parse(json_str);
                    for (const auto& item : j) {
                        long timestamp = long(item[0]) / 1000;
                        double net_value = item[1];
                        net_worth_dict[timestamp] = net_value;
                    }
                } catch (const json::exception& e) {
                    cerr << "JSON解析错误: " << e.what() << " for fund code: " << fund_code << endl;
                }
            } else {
                cerr << "未找到完整的 JSON 数组 for fund code: " << fund_code << endl;
            }
        } else {
            cerr << "未找到 Data_netWorthTrend 变量 for fund code: " << fund_code << endl;
        }
        return net_worth_dict;
    });
}

// 批量异步获取数据
std::vector<std::future<std::pair<std::string, map<long, double>>>> 
batch_generate_data_async(const std::vector<std::string>& fund_codes, size_t batch_size = 10) {
    std::vector<std::future<std::pair<std::string, map<long, double>>>> futures;
    
    for (size_t i = 0; i < fund_codes.size(); i += batch_size) {
        size_t end = std::min(i + batch_size, fund_codes.size());
        
        for (size_t j = i; j < end; ++j) {
            const auto& fund_code = fund_codes[j];
            
            auto future = std::async(std::launch::async, [fund_code]() -> std::pair<std::string, map<long, double>> {
                string url = "http://fund.eastmoney.com/pingzhongdata/" + fund_code + ".js";
                map<long, double> net_worth_dict;
                CURLcode res;

                string js_text = fetch_url(url, res);
                if (res != CURLE_OK) {
                    return {fund_code, net_worth_dict};
                }
                
                size_t start = js_text.find("var Data_netWorthTrend = ");
                if (start != string::npos) {
                    start = js_text.find('[', start);
                    size_t end = js_text.find("];", start);
                    if (start != string::npos && end != string::npos) {
                        string json_str = js_text.substr(start, end - start + 1);
                        try {
                            json j = json::parse(json_str);
                            for (const auto& item : j) {
                                long timestamp = long(item["x"]) / 1000;
                                double net_value = item["y"];
                                net_worth_dict[timestamp] = net_value;
                            }
                        } catch (const json::exception& e) {
                            cerr << "JSON解析错误: " << e.what() << " for fund code: " << fund_code << endl;
                        }
                    }
                }
                return {fund_code, net_worth_dict};
            });
            
            futures.push_back(std::move(future));
        }
        
        // 在每个批次之间短暂休息，避免对服务器造成过大压力
        if (i + batch_size < fund_codes.size()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    return futures;
}

void generate_report(
    double balance, string fund_code,
    double holdings, double latest_price, double profit,
    const vector<TradeOperation>& operations,
    const string& period, double touched_lowest_balance) 
{
    std::string file_name = "report/" + fund_code + "_" + period + "_report.txt";
    std::ofstream report(file_name);
    if (!report.is_open()) {
        std::cerr << "无法打开报告文件: " << file_name << std::endl;
        return;
    }
    report << std::fixed << std::setprecision(2);
    report << "SUM: " << CONFIG.sum << "  Amount: " << CONFIG.amount << "  Grid Size: " << CONFIG.grid_size << endl;
    report << "Holdings Value: " << holdings * latest_price << "(holds " << holdings << " at price " << latest_price << ")" << "  Balance: " << balance << " Total Value: " << holdings * latest_price + balance << endl;
    report << "PS: If Total Value (Holdings Value + Balance) < SUM, that shows you lost money at this moment!!!" << endl;
    report << "Profit: " << profit << "  Loss" << endl;
    report << "Touched Lowest Balance: " << touched_lowest_balance << endl;

    int dealed_count = 0, not_dealed_count = 0;
    std::for_each(operations.begin(), operations.end(), [&](const TradeOperation& operation) {
        if (operation.dealed) { dealed_count++; }
        else if (!operation.money_not_enough) { not_dealed_count++; }
    });
    report << endl << "dealed trade: " << dealed_count << "  not dealed trade: " << not_dealed_count << endl;
    report << "================================== trade operations ==================================" << endl;

    for (const auto& operation : operations) {
        if (operation.money_not_enough) {
            report << "Not enough money to buy" << endl;
        }
        report << "Buy Time: " << put_time(std::localtime(&operation.buy_timestamp), "%Y-%m-%d") << ",    Price: " << operation.buy_price << endl;

        if (operation.dealed) {
            report << "Sell Time: " << put_time(std::localtime(&operation.sell_timestamp), "%Y-%m-%d") << ",    Price: " << operation.sell_price << endl;
        }
        else {
            report << "Sell Time: " << operation.sell_timestamp << ",    Price: " << operation.sell_price << endl;
        }
        if (operation.big_grid_size) {
            report << "Big Grid Size Operation" << endl;
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
        case CUSTOMIZED_TIME:
            last_timestamp = latest_timestamp - 5 * 365 * 24 * 3600;
            break;
        default:
            return net_worth_data.begin();
    }
    auto it = net_worth_data.lower_bound(last_timestamp);
    return it;
}

std::map<long, double>::const_iterator get_end_date(const map<long, double>& net_worth_data, const std::string& period) {
    auto latest_it = --net_worth_data.end();
    long latest_timestamp = latest_it->first;
    long last_timestamp = latest_timestamp;

    switch (static_cast<Period>(std::stoi(period))) {
        case LAST_3_MONTHS:
        case LAST_6_MONTHS:
        case LAST_1_YEAR:
        case LAST_3_YEARS:
        case LAST_5_YEARS:
        case SINCE_ESTABLISHED:
            return net_worth_data.end();
        case CUSTOMIZED_TIME:
            last_timestamp = 1726761600; // 2024-09-20 00:00:00 黎明前
            break;
        default:
            return net_worth_data.end();
    }
    auto it = net_worth_data.lower_bound(last_timestamp);
    return it;
}

Thredhold calculate_thresholds(std::map<long, double>::const_iterator start_it, std::map<long, double>::const_iterator end_it)
{
    vector<double> values;
    for (auto it = start_it; it != end_it; ++it) {
        values.push_back(it->second);
    }
    sort(values.begin(), values.end());

    size_t n = values.size();
    Thredhold thresholds;
    if (n > 0) {
        size_t high_index = std::min(static_cast<size_t>(n * CONFIG.threshold_high), n - 1);
        size_t low_index = std::min(static_cast<size_t>(n * CONFIG.threshold_low), n - 1);
        
        thresholds.percentile_high = values[high_index];
        thresholds.percentile_low = values[low_index];
    }
    return thresholds;
}

void calculate_profit(
    const std::string& fund_code, const std::string& period, const map<long, double>& net_worth_data)
{
    auto start_date_it = get_start_date(net_worth_data, period);
    cout << "Start date: " << put_time(std::localtime(&start_date_it->first), "%Y-%m-%d") << endl;
    auto end_date_it = get_end_date(net_worth_data, period);
    if (std::distance(start_date_it, end_date_it) <= 0) {
        cerr << "Start date is after end date for fund code: " << fund_code << " and period: " << period << endl;
        return;
    }

    auto thresholds = calculate_thresholds(start_date_it, end_date_it);
    double current_balance = CONFIG.sum;
    double touched_lowest_balance = current_balance;
    double current_holdings = 0;
    double total_profit = 0;
    double current_base_price = start_date_it->second;
    double current_big_base_price = start_date_it->second;
    vector<TradeOperation> operations;
    for_each(start_date_it, end_date_it, [&](const auto& item) {
        long timestamp = item.first;
        double price = item.second;
        // cout << "Timestamp: " << timestamp << ", Price: " << price << ", base: " << current_base_price << ", big_base: " << current_big_base_price << endl;
        if (current_base_price * (BASE - CONFIG.grid_size) >= price and price < thresholds.percentile_high and price >= thresholds.percentile_low) {
            TradeOperation operation;
            if (current_balance < CONFIG.amount) {
                operation.money_not_enough = true;
                operation.buy_timestamp = timestamp;
                operation.buy_price = price;
                operations.push_back(operation);
                cout << fund_code << ": Not enough money for this operation." << endl;
            }
            else {
                current_balance -= CONFIG.amount;
                touched_lowest_balance = std::min(touched_lowest_balance, current_balance);
                current_holdings += CONFIG.amount / price;
                current_base_price = price;
                operation.buy_timestamp = timestamp;
                operation.buy_price = price;
                operations.push_back(operation);
            }
        }
        else if (current_big_base_price * (BASE - CONFIG.grid_size) >= price and price < thresholds.percentile_low) {
            TradeOperation operation;
            if (current_balance < CONFIG.amount * CONFIG.factor) {
                operation.money_not_enough = true;
                operation.buy_timestamp = timestamp;
                operation.buy_price = price;
                operation.big_grid_size = true;
                operations.push_back(operation);
                cout << fund_code << ": Not enough money for this operation." << endl;
            }
            else {
                current_balance -= CONFIG.amount * CONFIG.factor;
                touched_lowest_balance = std::min(touched_lowest_balance, current_balance);
                current_holdings += CONFIG.amount * CONFIG.factor / price;
                current_big_base_price = price;
                operation.buy_timestamp = timestamp;
                operation.buy_price = price;
                operation.big_grid_size = true;
                operations.push_back(operation);
            }
        }
        else if (current_base_price * (BASE + CONFIG.grid_size) <= price) {
            current_base_price = price; // 更新基准价格
            for (auto& operation : operations) {
                if (operation.money_not_enough || operation.dealed || operation.big_grid_size || operation.buy_price * (BASE + CONFIG.grid_size) > price) {
                    continue;
                }
                operation.sell_timestamp = timestamp;
                operation.sell_price = price;
                double profit = (CONFIG.amount / operation.buy_price) * operation.sell_price - CONFIG.amount;
                total_profit += profit;
                current_balance += (CONFIG.amount / operation.buy_price) * operation.sell_price;
                current_holdings -= CONFIG.amount / operation.buy_price;
                operation.dealed = true;
            }
        }
        else if (current_big_base_price * (BASE + CONFIG.big_grid_size) <= price) {
            current_big_base_price = price; // 更新基准价格
            for (auto& operation : operations) {
                if (operation.money_not_enough || operation.dealed || !operation.big_grid_size || operation.buy_price * (BASE + CONFIG.big_grid_size) > price) {
                    continue;
                }
                operation.sell_timestamp = timestamp;
                operation.sell_price = price;
                double profit = (CONFIG.amount * CONFIG.factor / operation.buy_price) * operation.sell_price - CONFIG.amount * CONFIG.factor;
                total_profit += profit;
                current_balance += (CONFIG.amount * CONFIG.factor / operation.buy_price) * operation.sell_price;
                current_holdings -= CONFIG.amount * CONFIG.factor / operation.buy_price;
                operation.dealed = true;
            }
        }
    });
    double latest_price = end_date_it->second;
    cout << fund_code << ": Total money left: " << current_balance << endl;
    cout << fund_code << ": Total profit: " << total_profit << endl;
    cout << fund_code << ": Touched Lowest Balance: " << touched_lowest_balance << endl;
    generate_report(current_balance, fund_code,
        current_holdings, latest_price, total_profit, operations, period, touched_lowest_balance
    );
    DatabaseStorage db_storage;
    db_storage.add(fund_code, period, current_holdings * latest_price + current_balance,
        current_balance, current_holdings * latest_price, total_profit, 0,
        thresholds.percentile_high, thresholds.percentile_low, 0);
}

void run_grid_strategy(const string fund_code) {
    // 使用异步版本获取数据
    auto data_future = generate_data_async(fund_code);
    
    // 这里可以做其他工作，比如处理配置验证等
    if (CONFIG.periods.empty()) {
        cerr << "No periods specified in the configuration." << endl;
        return;
    }
    
    // 等待网络请求完成
    const auto& net_worth_data = data_future.get();
    if (net_worth_data.empty()) {
        cerr << "No data found for fund code: " << fund_code << endl;
        return;
    }

    for (const auto& period : CONFIG.periods) {

        calculate_profit(fund_code, period, net_worth_data);
        std::cout << std::endl;
    }
}

// 异步版本的网格策略执行函数
std::future<void> run_grid_strategy_async(const string fund_code) {
    return std::async(std::launch::async, [fund_code]() {
        run_grid_strategy(fund_code);
    });
}


int main() {
    GetConfig get_config("config.txt");
    CONFIG = get_config.Get();

    std::cout << "Starting batch processing for " << CONFIG.fund_codes.size() << " fund codes..." << std::endl;
    
    // 方法1: 使用限制并发数的方式
    const size_t hardware_threads = std::thread::hardware_concurrency();
    const size_t MAX_CONCURRENT_THREADS = std::min(static_cast<size_t>(10), hardware_threads > 0 ? hardware_threads * 2 : 4);
    std::vector<std::future<void>> futures;
    
    for (size_t i = 0; i < CONFIG.fund_codes.size(); ++i) {
        const auto& code = CONFIG.fund_codes[i];
        std::cout << "Queuing fund code: " << code << " (" << (i+1) << "/" << CONFIG.fund_codes.size() << ")" << std::endl;
        
        // 如果达到最大并发数，等待一些任务完成
        if (futures.size() >= MAX_CONCURRENT_THREADS) {
            // 等待第一个任务完成
            futures.front().get();
            futures.erase(futures.begin());
        }
        
        // 启动新的异步任务
        futures.push_back(run_grid_strategy_async(code));
    }
    
    // 等待所有剩余任务完成
    std::cout << "Waiting for remaining " << futures.size() << " tasks to complete..." << std::endl;
    for (auto& future : futures) {
        future.get();
    }
    
    std::cout << "All fund codes processed successfully!" << std::endl;
    return 0;
}

// 备选的批量处理版本 - 如果你想使用批量网络请求
/*
int main_batch_version() {
    GetConfig get_config("config.txt");
    CONFIG = get_config.Get();

    std::cout << "Starting batch network requests for " << CONFIG.fund_codes.size() << " fund codes..." << std::endl;
    
    // 批量获取所有数据
    const size_t BATCH_SIZE = 10; // 每批处理10个
    auto data_futures = batch_generate_data_async(CONFIG.fund_codes, BATCH_SIZE);
    
    // 收集所有数据
    std::map<std::string, map<long, double>> all_data;
    size_t completed = 0;
    
    for (auto& future : data_futures) {
        auto [fund_code, net_worth_data] = future.get();
        all_data[fund_code] = std::move(net_worth_data);
        ++completed;
        
        if (completed % 50 == 0 || completed == data_futures.size()) {
            std::cout << "Downloaded data for " << completed << "/" << data_futures.size() << " fund codes" << std::endl;
        }
    }
    
    // 现在处理所有数据
    std::cout << "Processing downloaded data..." << std::endl;
    std::vector<std::future<void>> processing_futures;
    
    for (const auto& [fund_code, net_worth_data] : all_data) {
        if (net_worth_data.empty()) {
            cerr << "No data found for fund code: " << fund_code << endl;
            continue;
        }
        
        auto future = std::async(std::launch::async, [fund_code, &net_worth_data]() {
            if (CONFIG.periods.empty()) {
                cerr << "No periods specified in the configuration." << endl;
                return;
            }

            for (const auto& period : CONFIG.periods) {
                auto start_date_it = get_start_date(net_worth_data, period);
                calculate_profit(fund_code, period, net_worth_data);
            }
        });
        
        processing_futures.push_back(std::move(future));
    }
    
    // 等待所有处理任务完成
    for (auto& future : processing_futures) {
        future.get();
    }
    
    std::cout << "All fund codes processed successfully!" << std::endl;
    return 0;
}
*/
// 编译命令：g++ -g -o fund main.cpp GetConfig.cpp CppSQLite/DataBaseStorage.cpp CppSQLite/CppSQLite3.cpp -lcurl -lsqlite3 -std=c++17