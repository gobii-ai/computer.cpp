#include "ControlSessionStore.h"

#include "computer_cpp/AppPaths.h"
#include "computer_cpp/Timeline.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <sqlite3.h>
#include <stdexcept>

namespace fs = std::filesystem;

namespace ComputerCpp::ControlSessionStore {
namespace {

fs::path ControlSessionDbPath() {
    return AppDataDir() / "control-sessions.sqlite";
}

} // namespace

Db::Db() : connection_(ControlSessionDbPath(), "failed to open control session db", 5000) {
    init();
}

Db::~Db() = default;

sqlite3* Db::get() {
    return connection_.get();
}

void Db::init() {
    exec("PRAGMA journal_mode=WAL;");
    exec(
        "CREATE TABLE IF NOT EXISTS control_sessions ("
        "token TEXT PRIMARY KEY,"
        "scope TEXT NOT NULL,"
        "daemon_session TEXT NOT NULL,"
        "owner TEXT NOT NULL,"
        "purpose TEXT NOT NULL,"
        "state TEXT NOT NULL,"
        "created_at_ms INTEGER NOT NULL,"
        "acquired_at_ms INTEGER NOT NULL,"
        "renewed_at_ms INTEGER NOT NULL,"
        "expires_at_ms INTEGER NOT NULL,"
        "released_at_ms INTEGER,"
        "last_seen_at_ms INTEGER NOT NULL"
        ");");
    addColumnIfMissing("control_sessions", "max_runtime_ms", "INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing("control_sessions", "max_expires_at_ms", "INTEGER NOT NULL DEFAULT 0");
    exec("CREATE INDEX IF NOT EXISTS idx_control_sessions_scope_state ON control_sessions(scope, state, expires_at_ms);");
    exec("CREATE INDEX IF NOT EXISTS idx_control_sessions_created ON control_sessions(created_at_ms);");
    exec(
        "CREATE TABLE IF NOT EXISTS control_session_events ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "token TEXT,"
        "scope TEXT NOT NULL,"
        "daemon_session TEXT NOT NULL,"
        "owner TEXT NOT NULL,"
        "purpose TEXT NOT NULL,"
        "event TEXT NOT NULL,"
        "code TEXT,"
        "message TEXT,"
        "created_at_ms INTEGER NOT NULL,"
        "metadata_json TEXT NOT NULL DEFAULT '{}'"
        ");");
    exec("CREATE INDEX IF NOT EXISTS idx_control_session_events_scope_created ON control_session_events(scope, created_at_ms);");
    exec("CREATE INDEX IF NOT EXISTS idx_control_session_events_token ON control_session_events(token);");
    exec(
        "CREATE TABLE IF NOT EXISTS control_session_resources ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "token TEXT NOT NULL,"
        "scope TEXT NOT NULL,"
        "resource_type TEXT NOT NULL,"
        "resource_id TEXT NOT NULL,"
        "label TEXT,"
        "state TEXT NOT NULL,"
        "created_at_ms INTEGER NOT NULL,"
        "released_at_ms INTEGER,"
        "metadata_json TEXT NOT NULL DEFAULT '{}'"
        ");");
    exec("CREATE INDEX IF NOT EXISTS idx_control_session_resources_token_state ON control_session_resources(token, state);");
}

void Db::exec(const char* sql) {
    Exec(get(), sql);
}

void Db::addColumnIfMissing(const char* table, const char* column, const char* definition) {
    std::string pragma = std::string("PRAGMA table_info(") + table + ");";
    Statement stmt(get(), pragma);
    bool found = false;
    while (stmt.step() == SQLITE_ROW) {
        if (column == ColumnText(stmt.get(), 1)) {
            found = true;
            break;
        }
    }
    if (!found) {
        std::string sql = std::string("ALTER TABLE ") + table + " ADD COLUMN " + column + " " + definition + ";";
        exec(sql.c_str());
    }
}

ControlSessionRecord RowToRecord(sqlite3_stmt* stmt) {
    ControlSessionRecord record;
    record.token = ColumnText(stmt, 0);
    record.scope = ColumnText(stmt, 1);
    record.daemonSession = ColumnText(stmt, 2);
    record.owner = ColumnText(stmt, 3);
    record.purpose = ColumnText(stmt, 4);
    record.state = ColumnText(stmt, 5);
    record.createdAtMs = ColumnInt64(stmt, 6);
    record.acquiredAtMs = ColumnInt64(stmt, 7);
    record.renewedAtMs = ColumnInt64(stmt, 8);
    record.expiresAtMs = ColumnInt64(stmt, 9);
    record.releasedAtMs = ColumnInt64(stmt, 10);
    record.lastSeenAtMs = ColumnInt64(stmt, 11);
    record.maxRuntimeMs = ColumnInt64(stmt, 12);
    record.maxExpiresAtMs = ColumnInt64(stmt, 13);
    return record;
}

ControlSessionEvent RowToEvent(sqlite3_stmt* stmt) {
    ControlSessionEvent event;
    event.id = ColumnInt64(stmt, 0);
    event.token = ColumnText(stmt, 1);
    event.scope = ColumnText(stmt, 2);
    event.daemonSession = ColumnText(stmt, 3);
    event.owner = ColumnText(stmt, 4);
    event.purpose = ColumnText(stmt, 5);
    event.event = ColumnText(stmt, 6);
    event.code = ColumnText(stmt, 7);
    event.message = ColumnText(stmt, 8);
    event.createdAtMs = ColumnInt64(stmt, 9);
    event.metadataJson = ColumnText(stmt, 10);
    return event;
}

std::optional<ControlSessionRecord> SelectActiveForScope(sqlite3* db, const std::string& scope, int64_t nowMs) {
    Statement stmt(
        db,
        "SELECT token,scope,daemon_session,owner,purpose,state,created_at_ms,acquired_at_ms,renewed_at_ms,expires_at_ms,"
        "coalesce(released_at_ms,0),last_seen_at_ms,coalesce(max_runtime_ms,0),coalesce(max_expires_at_ms,0) "
        "FROM control_sessions WHERE scope=? AND state='active' AND expires_at_ms>? "
        "AND (coalesce(max_expires_at_ms,0)=0 OR max_expires_at_ms>?) "
        "ORDER BY acquired_at_ms DESC LIMIT 1;");
    stmt.bindText(1, scope);
    stmt.bindInt64(2, nowMs);
    stmt.bindInt64(3, nowMs);
    std::optional<ControlSessionRecord> record;
    if (IsRow(stmt.step())) {
        record = RowToRecord(stmt.get());
    }
    return record;
}

std::optional<ControlSessionRecord> SelectByToken(sqlite3* db, const std::string& token) {
    Statement stmt(
        db,
        "SELECT token,scope,daemon_session,owner,purpose,state,created_at_ms,acquired_at_ms,renewed_at_ms,expires_at_ms,"
        "coalesce(released_at_ms,0),last_seen_at_ms,coalesce(max_runtime_ms,0),coalesce(max_expires_at_ms,0) "
        "FROM control_sessions WHERE token=? LIMIT 1;");
    stmt.bindText(1, token);
    std::optional<ControlSessionRecord> record;
    if (IsRow(stmt.step())) {
        record = RowToRecord(stmt.get());
    }
    return record;
}

void InsertEvent(
    sqlite3* db,
    const ControlSessionRecord& record,
    const std::string& event,
    const std::string& code,
    const std::string& message,
    const nlohmann::json& metadata
) {
    Statement stmt(
        db,
        "INSERT INTO control_session_events(token,scope,daemon_session,owner,purpose,event,code,message,created_at_ms,metadata_json) "
        "VALUES (?,?,?,?,?,?,?,?,?,?);");
    stmt.bindText(1, record.token);
    stmt.bindText(2, record.scope);
    stmt.bindText(3, record.daemonSession);
    stmt.bindText(4, record.owner);
    stmt.bindText(5, record.purpose);
    stmt.bindText(6, event);
    stmt.bindText(7, code);
    stmt.bindText(8, message);
    stmt.bindInt64(9, NowMs());
    stmt.bindText(10, metadata.dump());
    stmt.expectDone();
}

std::vector<ControlSessionRecord> SelectExpiredActiveSessions(sqlite3* db, int64_t nowMs) {
    Statement stmt(
        db,
        "SELECT token,scope,daemon_session,owner,purpose,state,created_at_ms,acquired_at_ms,renewed_at_ms,expires_at_ms,"
        "coalesce(released_at_ms,0),last_seen_at_ms,coalesce(max_runtime_ms,0),coalesce(max_expires_at_ms,0) "
        "FROM control_sessions WHERE state='active' AND (expires_at_ms<=? OR (coalesce(max_expires_at_ms,0)>0 AND max_expires_at_ms<=?));");
    stmt.bindInt64(1, nowMs);
    stmt.bindInt64(2, nowMs);
    std::vector<ControlSessionRecord> records;
    while (IsRow(stmt.step())) {
        records.push_back(RowToRecord(stmt.get()));
    }
    return records;
}

void ExpireOldSessions(sqlite3* db, int64_t nowMs) {
    auto expired = SelectExpiredActiveSessions(db, nowMs);
    Statement stmt(
        db,
        "UPDATE control_sessions SET state='expired', last_seen_at_ms=? "
        "WHERE state='active' AND (expires_at_ms<=? OR (coalesce(max_expires_at_ms,0)>0 AND max_expires_at_ms<=?));");
    stmt.bindInt64(1, nowMs);
    stmt.bindInt64(2, nowMs);
    stmt.bindInt64(3, nowMs);
    stmt.expectDone();
    for (const auto& record : expired) {
        std::string code = record.maxExpiresAtMs > 0 && record.maxExpiresAtMs <= nowMs ? "max_runtime" : "ttl";
        InsertEvent(db, record, "expired", code, "control session expired");
    }
}

void InsertRecord(sqlite3* db, const ControlSessionRecord& record) {
    Statement stmt(
        db,
        "INSERT INTO control_sessions(token,scope,daemon_session,owner,purpose,state,created_at_ms,acquired_at_ms,"
        "renewed_at_ms,expires_at_ms,released_at_ms,last_seen_at_ms,max_runtime_ms,max_expires_at_ms) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,NULL,?,?,?);");
    stmt.bindText(1, record.token);
    stmt.bindText(2, record.scope);
    stmt.bindText(3, record.daemonSession);
    stmt.bindText(4, record.owner);
    stmt.bindText(5, record.purpose);
    stmt.bindText(6, record.state);
    stmt.bindInt64(7, record.createdAtMs);
    stmt.bindInt64(8, record.acquiredAtMs);
    stmt.bindInt64(9, record.renewedAtMs);
    stmt.bindInt64(10, record.expiresAtMs);
    stmt.bindInt64(11, record.lastSeenAtMs);
    stmt.bindInt64(12, record.maxRuntimeMs);
    stmt.bindInt64(13, record.maxExpiresAtMs);
    stmt.expectDone();
}

std::map<std::string, int64_t> EventCounts(sqlite3* db, const std::string& scope) {
    Statement stmt(
        db,
        "SELECT event,count(*) FROM control_session_events WHERE scope=? GROUP BY event;");
    stmt.bindText(1, scope);
    std::map<std::string, int64_t> counts;
    while (IsRow(stmt.step())) {
        counts[ColumnText(stmt.get(), 0)] = ColumnInt64(stmt.get(), 1);
    }
    return counts;
}

std::vector<ControlSessionEvent> RecentEvents(sqlite3* db, const std::string& scope, int limit) {
    Statement stmt(
        db,
        "SELECT id,coalesce(token,''),scope,daemon_session,owner,purpose,event,coalesce(code,''),coalesce(message,''),created_at_ms,metadata_json "
        "FROM control_session_events WHERE scope=? ORDER BY id DESC LIMIT ?;");
    stmt.bindText(1, scope);
    stmt.bindInt(2, std::clamp(limit, 1, 500));
    std::vector<ControlSessionEvent> events;
    while (IsRow(stmt.step())) {
        events.push_back(RowToEvent(stmt.get()));
    }
    return events;
}

std::vector<ControlSessionEvent> RecentEvents(const std::string& rawScope, int limit) {
    std::string scope = rawScope.empty() ? kDefaultControlScope : rawScope;
    if (!IsControlSessionScopeValid(scope)) {
        return {};
    }
    Db db;
    Transaction transaction(db.get());
    ExpireOldSessions(db.get(), NowMs());
    auto events = RecentEvents(db.get(), scope, limit);
    transaction.commit();
    return events;
}

} // namespace ComputerCpp::ControlSessionStore
