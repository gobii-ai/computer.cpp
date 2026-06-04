#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

struct sqlite3;
struct sqlite3_stmt;

namespace ComputerCpp::Sqlite {

class Connection {
public:
    Connection(const std::filesystem::path& path, const std::string& errorPrefix, int busyTimeoutMs = 0);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    sqlite3* get();

private:
    sqlite3* db_ = nullptr;
};

class Statement {
public:
    Statement(sqlite3* db, const char* sql);
    Statement(sqlite3* db, const std::string& sql);
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    sqlite3_stmt* get() const;
    int step() const;
    void reset() const;
    void bindText(int index, const std::string& value) const;
    void bindInt(int index, int value) const;
    void bindInt64(int index, int64_t value) const;
    void bindDouble(int index, double value) const;
    void expectDone() const;

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

class Transaction {
public:
    explicit Transaction(sqlite3* db, const char* beginSql = "BEGIN IMMEDIATE;");
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit();
    void rollback();

private:
    sqlite3* db_ = nullptr;
    bool active_ = false;
};

void Exec(sqlite3* db, const char* sql);
bool IsRow(int stepResult);
std::string ColumnText(sqlite3_stmt* stmt, int column);
int64_t ColumnInt64(sqlite3_stmt* stmt, int column);

} // namespace ComputerCpp::Sqlite
