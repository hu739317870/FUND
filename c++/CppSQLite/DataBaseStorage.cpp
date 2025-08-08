/**
 * Copyright 2019 Nokia Solutions and Networks. All rights reserved.
 **/

#include <iostream>
#include <cmath>
#include "DataBaseStorage.hpp"


const std::string CREATE_TABLE = std::string("create table [TB_FUND](id INTEGER PRIMARY KEY AUTOINCREMENT,")
    + " fund_code TEXT, period TEXT, total_value REAL, balance REAL, holdings_value REAL, "
    + " profit REAL, loss REAL, percentile_70_price REAL, percentile_30_price REAL, operation_id INTEGER);";

const std::string INSERT_SQL = std::string("insert into [TB_FUND] values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");

DatabaseStorage::DatabaseStorage()
{
    std::string databasePath = "/home/zhahu/FUND/c++/fund.db";
    db_.open(databasePath.c_str());

    if (!db_.tableExists("TB_FUND"))
    {
        db_.execDML(CREATE_TABLE.c_str());
    }
}

DatabaseStorage::~DatabaseStorage()
{
    try
    {
        db_.close();
    }
    catch (CppSQLite3Exception& e)
    {
        std::cerr << "Error closing database: " << e.errorMessage() << std::endl;
    }
}

bool DatabaseStorage::add(const std::string& fund_code, const std::string& period, double total_value,
    double balance, double holdings_value, double profit, double loss,
    double percentile_70_price, double percentile_30_price, int operation_id)
{
    auto round_to_two = [](double value) {
        return std::round(value * 100.0) / 100.0;
    };

    total_value = round_to_two(total_value);
    balance = round_to_two(balance);
    holdings_value = round_to_two(holdings_value);
    profit = round_to_two(profit);
    loss = round_to_two(loss);
    percentile_70_price = round_to_two(percentile_70_price);
    percentile_30_price = round_to_two(percentile_30_price);

    db_.execDML("begin transaction;");
    try
    {
        CppSQLite3Statement smt = db_.compileStatement(INSERT_SQL.c_str());
        smt.bindNull(1); // id will be auto-incremented
        smt.bind(2, fund_code.c_str());
        smt.bind(3, period.c_str());
        smt.bind(4, total_value);
        smt.bind(5, balance);
        smt.bind(6, holdings_value);
        smt.bind(7, profit);
        smt.bind(8, loss);
        smt.bind(9, percentile_70_price);
        smt.bind(10, percentile_30_price);
        smt.bind(11, operation_id);
        smt.execDML();
        smt.reset();
    }
    catch (CppSQLite3Exception& e)
    {
        std::cerr << "Error inserting data: " << e.errorMessage() << std::endl;
        db_.execDML("rollback transaction;");
        return false;
    }
    db_.execDML("commit transaction;");
    return true;
}
