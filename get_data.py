import requests
import re
import json
from datetime import datetime

fund_code = '004997'  # 例如易方达消费行业
url = f'http://fund.eastmoney.com/pingzhongdata/{fund_code}.js'
response = requests.get(url)

js_text = response.text

# 提取 Data_netWorthTrend 的 JSON 数据
match = re.search(r'var Data_netWorthTrend = (.*?);', js_text)
if match:
    data_list = json.loads(match.group(1))

    # 构造 {timestamp: net_value} 字典
    net_worth_dict = {item['x']: item['y'] for item in data_list}

    # 写入 JSON 文件
    with open(f'{fund_code}_net_value.json', 'w', encoding='utf-8') as f_json:
        json.dump(net_worth_dict, f_json, ensure_ascii=False, indent=2)

    # 写入 CSV 文件，日期格式
    with open(f'{fund_code}_net_value.csv', 'w', encoding='utf-8') as f_csv:
        f_csv.write('date,net_value\n')
        for timestamp in sorted(net_worth_dict.keys()):
            date_str = datetime.fromtimestamp(timestamp / 1000).strftime('%Y-%m-%d')
            net_value = net_worth_dict[timestamp]
            f_csv.write(f'{date_str},{net_value}\n')

    print(f'写入完成：{fund_code}_net_value.json 和 {fund_code}_net_value.csv')
else:
    print("未找到净值数据")