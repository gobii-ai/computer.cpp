#pragma once

#include "computer_cpp/ControlSession.h"
#include "Sqlite.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace ComputerCpp::ControlSessionStore {

using Sqlite::ColumnInt64;
using Sqlite::ColumnText;
using Sqlite::Exec;
using Sqlite::IsRow;
using Sqlite::Statement;
using Sqlite::Transaction;

class Db {
public:
    Db();
    ~Db();

    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    sqlite3* get();

private:
    void init();
    void exec(const char* sql);
    void addColumnIfMissing(const char* table, const char* column, const char* definition);

    Sqlite::Connection connection_;
};

ControlSessionRecord RowToRecord(sqlite3_stmt* stmt);
ControlSessionEvent RowToEvent(sqlite3_stmt* stmt);
ControlSessionResource RowToResource(sqlite3_stmt* stmt);

std::optional<ControlSessionRecord> SelectActiveForScope(sqlite3* db, const std::string& scope, int64_t nowMs);
std::optional<ControlSessionRecord> SelectByToken(sqlite3* db, const std::string& token);
void InsertEvent(
    sqlite3* db,
    const ControlSessionRecord& record,
    const std::string& event,
    const std::string& code = "",
    const std::string& message = "",
    const nlohmann::json& metadata = nlohmann::json::object()
);
std::vector<ControlSessionRecord> SelectExpiredActiveSessions(sqlite3* db, int64_t nowMs);
void ExpireOldSessions(sqlite3* db, int64_t nowMs);
void InsertRecord(sqlite3* db, const ControlSessionRecord& record);
std::map<std::string, int64_t> EventCounts(sqlite3* db, const std::string& scope);
std::vector<ControlSessionEvent> RecentEvents(sqlite3* db, const std::string& scope, int limit);
std::vector<ControlSessionEvent> RecentEvents(const std::string& scope, int limit);
std::vector<ControlSessionResource> ActiveResources(sqlite3* db, const std::string& token);
std::vector<ControlSessionResource> ActiveResources(const std::string& token);
std::vector<ControlSessionResource> ExpiredResources(const std::string& scope);
void RegisterResource(
    const std::string& token,
    const std::string& resourceType,
    const std::string& resourceId,
    const std::string& label,
    const nlohmann::json& metadata
);
void ReleaseResource(const std::string& token, const std::string& resourceType, const std::string& resourceId);
void AbandonResource(
    const std::string& token,
    const std::string& resourceType,
    const std::string& resourceId,
    const std::string& reason
);

} // namespace ComputerCpp::ControlSessionStore
