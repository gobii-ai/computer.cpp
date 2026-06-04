#include "Sqlite.h"

#include <sqlite3.h>

#include <stdexcept>

namespace ComputerCpp::Sqlite {

Connection::Connection(const std::filesystem::path& path, const std::string& errorPrefix, int busyTimeoutMs) {
    if (sqlite3_open(path.string().c_str(), &db_) != SQLITE_OK) {
        std::string error = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error(errorPrefix + ": " + error);
    }
    if (busyTimeoutMs > 0) {
        sqlite3_busy_timeout(db_, busyTimeoutMs);
    }
}

Connection::~Connection() {
    if (db_) {
        sqlite3_close(db_);
    }
}

sqlite3* Connection::get() {
    return db_;
}

Statement::Statement(sqlite3* db, const char* sql) : db_(db) {
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
}

Statement::Statement(sqlite3* db, const std::string& sql) : Statement(db, sql.c_str()) {}

Statement::~Statement() {
    if (stmt_) {
        sqlite3_finalize(stmt_);
    }
}

Statement::Statement(Statement&& other) noexcept : db_(other.db_), stmt_(other.stmt_) {
    other.db_ = nullptr;
    other.stmt_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
        db_ = other.db_;
        stmt_ = other.stmt_;
        other.db_ = nullptr;
        other.stmt_ = nullptr;
    }
    return *this;
}

sqlite3_stmt* Statement::get() const {
    return stmt_;
}

int Statement::step() const {
    return sqlite3_step(stmt_);
}

void Statement::reset() const {
    sqlite3_reset(stmt_);
}

void Statement::bindText(int index, const std::string& value) const {
    sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

void Statement::bindInt(int index, int value) const {
    sqlite3_bind_int(stmt_, index, value);
}

void Statement::bindInt64(int index, int64_t value) const {
    sqlite3_bind_int64(stmt_, index, value);
}

void Statement::bindDouble(int index, double value) const {
    sqlite3_bind_double(stmt_, index, value);
}

void Statement::expectDone() const {
    if (step() != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
}

Transaction::Transaction(sqlite3* db, const char* beginSql) : db_(db), active_(true) {
    Exec(db_, beginSql);
}

Transaction::~Transaction() {
    if (active_) {
        char* error = nullptr;
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &error);
        sqlite3_free(error);
        active_ = false;
    }
}

void Transaction::commit() {
    if (!active_) {
        return;
    }
    Exec(db_, "COMMIT;");
    active_ = false;
}

void Transaction::rollback() {
    if (!active_) {
        return;
    }
    Exec(db_, "ROLLBACK;");
    active_ = false;
}

void Exec(sqlite3* db, const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = error ? error : "unknown sqlite error";
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}

bool IsRow(int stepResult) {
    return stepResult == SQLITE_ROW;
}

std::string ColumnText(sqlite3_stmt* stmt, int column) {
    const unsigned char* text = sqlite3_column_text(stmt, column);
    return text ? reinterpret_cast<const char*>(text) : "";
}

int64_t ColumnInt64(sqlite3_stmt* stmt, int column) {
    return sqlite3_column_int64(stmt, column);
}

} // namespace ComputerCpp::Sqlite
