#include "GetConfig.hpp"

#include <fstream>
#include <sstream>
#include <iostream>

std::vector<std::string> parse_array(const std::string& s) {
    std::vector<std::string> result;
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


GetConfig::GetConfig(const std::string& filename) : config_()
{
    auto config_map = ReadConfig(filename);

    if (config_map.empty()) {
        std::cerr << "配置文件读取失败或为空。" << std::endl;
        return;
    }
    if (!config_map.empty()) {
        config_.grid_size = std::stod(config_map["grid_size"]);
        config_.big_grid_size = std::stod(config_map["big_grid_size"]);
        config_.factor = std::stoi(config_map["factor"]);
        config_.sum = std::stod(config_map["sum"]);
        config_.amount = std::stod(config_map["amount"]);
        config_.fund_codes = parse_array(config_map["fund_codes"]);
        config_.periods = parse_array(config_map["period"]);
        config_.threshold_low = std::stof(config_map["threshold_low"]);
        config_.threshold_high = std::stof(config_map["threshold_high"]);
    }
}

Config GetConfig::Get() const
{
    return config_;
}

std::unordered_map<std::string, std::string> GetConfig::ReadConfig(const std::string& filename)
{
    std::unordered_map<std::string, std::string> config;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "无法打开配置文件：" << filename << std::endl;
        return config;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);

        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        config[key] = value;
    }
    return config;
}