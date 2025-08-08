import akshare as ak

def get_all_etf_code():
    """
    获取所有ETF基金代码
    :return: ETF基金代码列表
    """
    etf_df = ak.fund_etf_spot_em()
    etf_codes = etf_df[['代码', '名称']]
    return etf_codes

if __name__ == "__main__":
    etf_codes = get_all_etf_code()
    # write to file
    with open('all_etf_codes.txt', 'w', encoding='utf-8') as f:
        for _, row in etf_codes.iterrows():
            code = row['代码']
            name = row['名称']
            f.write(f"{code}, {name}\n")