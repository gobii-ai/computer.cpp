#include "ControlSessionInternal.h"

#include "ControlSessionStore.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <random>
#include <utility>

namespace ComputerCpp::ControlSessionInternal {
namespace {

using ControlSessionStore::InsertEvent;
using ControlSessionStore::InsertRecord;
using ControlSessionStore::SelectActiveForScope;
using ControlSessionStore::SelectByToken;
using ControlSessionStore::Statement;

std::string DefaultOwner() {
    if (const char* user = std::getenv("USER")) {
        if (*user) return user;
    }
    if (const char* logname = std::getenv("LOGNAME")) {
        if (*logname) return logname;
    }
    return "unknown";
}

std::string GenerateToken() {
    std::array<unsigned char, 24> bytes{};
    std::random_device rd;
    for (auto& byte : bytes) {
        byte = static_cast<unsigned char>(rd() & 0xff);
    }
    const char* hex = "0123456789abcdef";
    std::string token = "acs_";
    token.reserve(4 + bytes.size() * 2);
    for (unsigned char byte : bytes) {
        token.push_back(hex[(byte >> 4) & 0x0f]);
        token.push_back(hex[byte & 0x0f]);
    }
    return token;
}

ControlSessionRecord NewActiveControlSessionRecord(const ControlSessionAcquireOptions& options, int64_t nowMs) {
    ControlSessionRecord record;
    record.token = GenerateToken();
    record.scope = options.scope;
    record.daemonSession = options.daemonSession;
    record.owner = options.owner;
    record.purpose = options.purpose;
    record.state = "active";
    record.createdAtMs = nowMs;
    record.acquiredAtMs = nowMs;
    record.renewedAtMs = nowMs;
    record.expiresAtMs = nowMs + options.ttlMs;
    record.maxRuntimeMs = options.maxRuntimeMs;
    record.maxExpiresAtMs = options.maxRuntimeMs > 0 ? nowMs + options.maxRuntimeMs : 0;
    if (record.maxExpiresAtMs > 0) {
        record.expiresAtMs = std::min(record.expiresAtMs, record.maxExpiresAtMs);
    }
    record.releasedAtMs = 0;
    record.lastSeenAtMs = nowMs;
    return record;
}

} // namespace

ControlSessionResult ErrorResult(std::string code, std::string error) {
    ControlSessionResult result;
    result.ok = false;
    result.code = std::move(code);
    result.error = std::move(error);
    return result;
}

ControlSessionResult RecordResult(const ControlSessionRecord& record) {
    ControlSessionResult result;
    result.ok = true;
    result.record = record;
    return result;
}

ControlSessionResult EnsureCurrentHolder(sqlite3* db, const ControlSessionRecord& record, int64_t nowMs) {
    auto holder = SelectActiveForScope(db, record.scope, nowMs);
    if (!holder.has_value() || holder->token != record.token) {
        auto result = ErrorResult("control_session_not_holder", "control session is not the current holder for this scope");
        result.record = record;
        if (holder.has_value()) {
            result.holder = holder;
        }
        return result;
    }
    return RecordResult(record);
}

ControlSessionAcquireOptions NormalizeAcquireOptions(const ControlSessionAcquireOptions& rawOptions) {
    ControlSessionAcquireOptions options = rawOptions;
    options.ttlMs = ClampControlSessionTtlMs(options.ttlMs);
    options.waitMs = ClampControlSessionWaitMs(options.waitMs);
    options.maxRuntimeMs = ClampControlSessionMaxRuntimeMs(options.maxRuntimeMs);
    if (options.scope.empty()) {
        options.scope = kDefaultControlScope;
    }
    if (options.daemonSession.empty()) {
        options.daemonSession = "default";
    }
    if (options.owner.empty()) {
        options.owner = DefaultOwner();
    }
    return options;
}

ControlSessionRecord InsertNewActiveControlSession(
    sqlite3* db,
    const ControlSessionAcquireOptions& options,
    int64_t nowMs,
    nlohmann::json eventMetadata
) {
    ControlSessionRecord record = NewActiveControlSessionRecord(options, nowMs);
    InsertRecord(db, record);
    eventMetadata["ttlMs"] = options.ttlMs;
    eventMetadata["waitMs"] = options.waitMs;
    eventMetadata["maxRuntimeMs"] = options.maxRuntimeMs;
    InsertEvent(db, record, "acquired", "", "control session acquired", eventMetadata);
    return record;
}

std::optional<int64_t> NextControlSessionExpiry(const ControlSessionRecord& record, int64_t nowMs, int64_t ttlMs) {
    int64_t expiresAtMs = nowMs + ttlMs;
    if (record.maxExpiresAtMs > 0) {
        expiresAtMs = std::min(expiresAtMs, record.maxExpiresAtMs);
    }
    if (expiresAtMs <= nowMs) {
        return std::nullopt;
    }
    return expiresAtMs;
}

ControlSessionResult ExpireControlSessionForMaxRuntime(sqlite3* db, const ControlSessionRecord& record, int64_t nowMs) {
    Statement expireStmt(db, "UPDATE control_sessions SET state='expired', last_seen_at_ms=? WHERE token=?;");
    expireStmt.bindInt64(1, nowMs);
    expireStmt.bindText(2, record.token);
    expireStmt.expectDone();
    InsertEvent(db, record, "expired", "max_runtime", "control session max runtime expired");
    return ErrorResult("control_session_expired", "control session max runtime has expired");
}

std::optional<ControlSessionRecord> RenewActiveControlSession(
    sqlite3* db,
    const ControlSessionRecord& record,
    int64_t nowMs,
    int64_t expiresAtMs,
    const std::string& event,
    const std::string& message,
    nlohmann::json metadata
) {
    Statement stmt(db, "UPDATE control_sessions SET renewed_at_ms=?, expires_at_ms=?, last_seen_at_ms=? WHERE token=?;");
    stmt.bindInt64(1, nowMs);
    stmt.bindInt64(2, expiresAtMs);
    stmt.bindInt64(3, nowMs);
    stmt.bindText(4, record.token);
    stmt.expectDone();

    auto updated = SelectByToken(db, record.token);
    if (updated.has_value()) {
        metadata["expiresAtMs"] = updated->expiresAtMs;
        InsertEvent(db, *updated, event, "", message, metadata);
    }
    return updated;
}

} // namespace ComputerCpp::ControlSessionInternal
