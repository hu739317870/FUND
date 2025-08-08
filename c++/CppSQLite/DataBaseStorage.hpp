/**
 * Copyright 2019 Nokia Solutions and Networks. All rights reserved.
 **/

#ifndef ATL_MOCK_DATABASESTORAGE_HPP_
#define ATL_MOCK_DATABASESTORAGE_HPP_

#include <string>
#include "CppSQLite3.h"

class DatabaseStorage
{
public:
    explicit DatabaseStorage();
    ~DatabaseStorage();

    bool add(const std::string& fund_code, const std::string& period, double total_value,
       double balance, double holdings_value, double profit, double loss,
       double percentile_70_price, double percentile_30_price, int operation_id);

private:
    CppSQLite3DB db_;
};

#endif  // ASM_DATABASESTORAGE_HPP_
