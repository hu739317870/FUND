#include <string>
#include <vector>
#include <unordered_map>

struct Config 
{
    double grid_size;
    double big_grid_size;
    int factor;
    double sum;
    double amount;
    std::vector<std::string> fund_codes;
    std::vector<std::string> periods; // 0: LAST_3_MONTHS, 1: LAST_6_MONTHS, etc.
    float threshold_low;
    float threshold_high;
};

class GetConfig 
{
public:
    GetConfig(const std::string& filename);
    Config Get() const;

private:
    std::unordered_map<std::string, std::string> ReadConfig(const std::string& filename);

private:
    Config config_;
};
