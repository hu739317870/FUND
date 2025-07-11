import React, { useState } from "react";
import "./style.css";

function parseFundData(jsText) {
  const start = jsText.indexOf("var Data_netWorthTrend = ");
  if (start === -1) return null;
  const jsonStart = jsText.indexOf("[", start);
  const jsonEnd = jsText.indexOf("];", jsonStart);
  if (jsonStart === -1 || jsonEnd === -1) return null;
  const jsonStr = jsText.substring(jsonStart, jsonEnd + 1);
  try {
    return JSON.parse(jsonStr).map(item => ({
      timestamp: item.x / 1000,
      price: item.y,
    }));
  } catch (e) {
    console.error("JSON parse error", e);
    return null;
  }
}

function App() {
  const [fundCode, setFundCode] = useState("");
  const [gridSize, setGridSize] = useState(0.05);
  const [sum, setSum] = useState(10000);
  const [amount, setAmount] = useState(1000);
  const [report, setReport] = useState("");

  const runGridStrategy = async () => {
    const proxyUrl = "https://cors-anywhere.herokuapp.com/";
    const url = `https://fund.eastmoney.com/pingzhongdata/${fundCode}.js`;
    const res = await fetch(proxyUrl+url);
    const jsText = await res.text();
    const netWorthData = parseFundData(jsText);
    if (!netWorthData) {
      setReport("无法获取或解析基金数据");
      return;
    }

    let currentBalance = sum;
    let currentHoldings = 0;
    let totalProfit = 0;
    let currentBasePrice = netWorthData[0].price;
    const operations = [];
    const transacted = [];
    const BASE = 1;

    for (const { timestamp, price } of netWorthData) {
      if (price <= currentBasePrice * (BASE - gridSize)) {
        if (currentBalance < amount) continue;
        operations.push({ buy_timestamp: timestamp, buy_price: price });
        currentHoldings += amount / price;
        currentBalance -= amount;
        currentBasePrice = price;
      } else if (price >= currentBasePrice * (BASE + gridSize)) {
        while (operations.length) {
          const op = operations[operations.length - 1];
          if (price < op.buy_price * (BASE + gridSize)) break;
          operations.pop();
          const sellAmount = (amount / op.buy_price) * price;
          totalProfit += sellAmount - amount;
          currentBalance += sellAmount;
          currentHoldings -= amount / op.buy_price;
          transacted.push({ ...op, sell_timestamp: timestamp, sell_price: price });
        }
        currentBasePrice = price;
      }
    }

    const latestPrice = netWorthData[netWorthData.length - 1].price;
    const holdingsValue = currentHoldings * latestPrice;
    let profitString =
      `SUM: ${sum}, Amount: ${amount}, Grid Size: ${gridSize}\n` +
      `Holdings Value: ${holdingsValue.toFixed(2)} (holds ${currentHoldings.toFixed(2)} at price ${latestPrice})\n` +
      `Balance: ${currentBalance.toFixed(2)}\n` +
      `Total Profit: ${totalProfit.toFixed(2)}\n` +
      `Trades: ${transacted.length} completed, ${operations.length} open\n` +
      `============================ Trades ============================\n`;
    const allOps = [...operations, ...transacted].sort((a, b) => a.buy_timestamp - b.buy_timestamp);
    for (const op of allOps) {
      profitString += `Buy: ${new Date(op.buy_timestamp * 1000).toLocaleDateString()} at ${op.buy_price}\n`;
      if (op.sell_timestamp) {
        profitString += `Sell: ${new Date(op.sell_timestamp * 1000).toLocaleDateString()} at ${op.sell_price}\n`;
      }
      profitString += `----------------------------------------------------------\n`;
    }
    setReport(profitString);
  };

  return (
    <div className="container">
      <input
        type="text"
        placeholder="基金代码（如 161725)"
        value={fundCode}
        onChange={e => setFundCode(e.target.value)}
        className="input"
      />
      <div className="grid-two-cols">
        <input
          type="number"
          step="0.01"
          placeholder="网格比例（如 0.05)"
          value={gridSize}
          onChange={e => setGridSize(parseFloat(e.target.value))}
          className="input"
        />
        <input
          type="number"
          placeholder="初始资金"
          value={sum}
          onChange={e => setSum(parseFloat(e.target.value))}
          className="input"
        />
        <input
          type="number"
          placeholder="每次交易金额"
          value={amount}
          onChange={e => setAmount(parseFloat(e.target.value))}
          className="input"
        />
      </div>
      <button onClick={runGridStrategy} className="button">
        运行策略
      </button>
      <div className="report">{report || "暂无报告"}</div>
    </div>
  );
}

export default App;
