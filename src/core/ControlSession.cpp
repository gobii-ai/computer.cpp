#include "computer_cpp/ControlSession.h"

#include "ControlSessionInternal.h"
#include "ControlSessionStore.h"

#include "computer_cpp/Timeline.h"

#include <optional>

namespace ComputerCpp {
namespace {

using ControlSessionStore::Db;
using ControlSessionStore::ExpireOldSessions;
using ControlSessionStore::InsertEvent;
using ControlSessionStore::SelectActiveForScope;
using ControlSessionStore::SelectByToken;
using ControlSessionStore::Statement;
using ControlSessionStore::Transaction;
using ControlSessionInternal::EnsureCurrentHolder;
using ControlSessionInternal::ErrorResult;
using ControlSessionInternal::ExpireControlSessionForMaxRuntime;
using ControlSessionInternal::NextControlSessionExpiry;
using ControlSessionInternal::RecordResult;
using ControlSessionInternal::RenewActiveControlSession;

}

ControlSessionRecord ReleaseRecord(sqlite3* db, const ControlSessionRecord& record, int64_t nowMs) {
    Statement stmt(
        db,
        "UPDATE control_sessions SET state='released', released_at_ms=?, last_seen_at_ms=? WHERE token=?;");
    stmt.bindInt64(1, nowMs);
    stmt.bindInt64(2, nowMs);
    stmt.bindText(3, record.token);
    stmt.expectDone();
    return SelectByToken(db, record.token).value_or(record);
}

std::string NormalizeScopeOrDefault(const std::string& rawScope) {
    return rawScope.empty() ? kDefaultControlScope : rawScope;
}

ControlSessionResult RenewControlSession(const std::string& token, int64_t ttlMs) {
    if (token.empty()) {
        return ErrorResult("control_session_required", "control session token is required");
    }
    ttlMs = ClampControlSessionTtlMs(ttlMs);
    Db db;
    Transaction transaction(db.get());
    int64_t nowMs = NowMs();
    ExpireOldSessions(db.get(), nowMs);
    auto record = SelectByToken(db.get(), token);
    if (!record.has_value()) {
        transaction.commit();
        return ErrorResult("control_session_not_found", "control session was not found");
    }
    if (record->state != "active") {
        transaction.commit();
        return ErrorResult("control_session_not_active", "control session is not active");
    }
    if (record->expiresAtMs <= nowMs) {
        transaction.commit();
        return ErrorResult("control_session_expired", "control session has expired");
    }
    auto holderCheck = EnsureCurrentHolder(db.get(), *record, nowMs);
    if (!holderCheck.ok) {
        transaction.commit();
        return holderCheck;
    }

    auto nextExpiresAtMs = NextControlSessionExpiry(*record, nowMs, ttlMs);
    if (!nextExpiresAtMs) {
        auto expired = ExpireControlSessionForMaxRuntime(db.get(), *record, nowMs);
        transaction.commit();
        return expired;
    }

    auto updated = RenewActiveControlSession(
        db.get(),
        *record,
        nowMs,
        *nextExpiresAtMs,
        "renewed",
        "control session renewed",
        {{"ttlMs", ttlMs}}
    );
    transaction.commit();
    if (!updated.has_value()) {
        return ErrorResult("control_session_not_found", "control session was not found after renewal");
    }
    return RecordResult(*updated);
}

ControlSessionResult ReleaseControlSession(const std::string& token) {
    if (token.empty()) {
        return ErrorResult("control_session_required", "control session token is required");
    }
    Db db;
    Transaction transaction(db.get());
    int64_t nowMs = NowMs();
    ExpireOldSessions(db.get(), nowMs);
    auto record = SelectByToken(db.get(), token);
    if (!record.has_value()) {
        transaction.commit();
        return ErrorResult("control_session_not_found", "control session was not found");
    }
    ControlSessionResult result = RecordResult(*record);
    if (record->state == "active") {
        auto updated = ReleaseRecord(db.get(), *record, nowMs);
        result.released = true;
        result.record = updated;
        InsertEvent(db.get(), updated, "released", "", "control session released");
    }
    transaction.commit();
    return result;
}

ControlSessionResult ReleaseActiveControlSession(
    const std::string& rawScope,
    const std::string& expectedOwner,
    const std::string& expectedPurpose,
    const std::string& reason
) {
    std::string scope = NormalizeScopeOrDefault(rawScope);
    if (!IsControlSessionScopeValid(scope)) {
        return ErrorResult("invalid_control_scope", "invalid control session scope");
    }
    Db db;
    Transaction transaction(db.get());
    int64_t nowMs = NowMs();
    ExpireOldSessions(db.get(), nowMs);
    auto record = SelectActiveForScope(db.get(), scope, nowMs);
    if (!record.has_value()) {
        transaction.commit();
        return ErrorResult("control_session_not_found", "no active control session for scope");
    }
    if (!expectedOwner.empty() && record->owner != expectedOwner) {
        auto result = ErrorResult("control_session_owner_mismatch", "active control session owner did not match");
        result.holder = record;
        transaction.commit();
        return result;
    }
    if (!expectedPurpose.empty() && record->purpose != expectedPurpose) {
        auto result = ErrorResult("control_session_purpose_mismatch", "active control session purpose did not match");
        result.holder = record;
        transaction.commit();
        return result;
    }

    auto updated = ReleaseRecord(db.get(), *record, nowMs);
    ControlSessionResult result = RecordResult(updated);
    result.released = true;
    InsertEvent(db.get(), result.record, "debug_released", "release_active", reason.empty() ? "active control session released by debug command" : reason, {
        {"expectedOwner", expectedOwner},
        {"expectedPurpose", expectedPurpose}
    });
    transaction.commit();
    return result;
}

ControlSessionResult ValidateControlSession(const std::string& token, const std::string& rawExpectedScope) {
    if (token.empty()) {
        return ErrorResult("control_session_required", "an active control session is required");
    }
    std::string expectedScope = NormalizeScopeOrDefault(rawExpectedScope);
    if (!IsControlSessionScopeValid(expectedScope)) {
        return ErrorResult("invalid_control_scope", "invalid control session scope");
    }
    Db db;
    Transaction transaction(db.get());
    int64_t nowMs = NowMs();
    ExpireOldSessions(db.get(), nowMs);
    auto record = SelectByToken(db.get(), token);
    if (!record.has_value()) {
        transaction.commit();
        return ErrorResult("control_session_not_found", "control session was not found");
    }
    if (record->scope != expectedScope) {
        ControlSessionResult result = ErrorResult("control_session_scope_mismatch", "control session scope does not match this action");
        result.record = *record;
        transaction.commit();
        return result;
    }
    if (record->state != "active") {
        ControlSessionResult result = ErrorResult("control_session_not_active", "control session is not active");
        result.record = *record;
        transaction.commit();
        return result;
    }
    if (record->expiresAtMs <= nowMs) {
        ControlSessionResult result = ErrorResult("control_session_expired", "control session has expired");
        result.record = *record;
        transaction.commit();
        return result;
    }
    auto holderCheck = EnsureCurrentHolder(db.get(), *record, nowMs);
    if (!holderCheck.ok) {
        transaction.commit();
        return holderCheck;
    }

    Statement stmt(db.get(), "UPDATE control_sessions SET last_seen_at_ms=? WHERE token=?;");
    stmt.bindInt64(1, nowMs);
    stmt.bindText(2, token);
    stmt.expectDone();
    auto updated = SelectByToken(db.get(), token);
    transaction.commit();
    return RecordResult(updated.value_or(*record));
}

ControlSessionResult GetControlSessionStatus(const std::string& rawScope, const std::string& token) {
    std::string scope = NormalizeScopeOrDefault(rawScope);
    if (!IsControlSessionScopeValid(scope)) {
        return ErrorResult("invalid_control_scope", "invalid control session scope");
    }
    Db db;
    Transaction transaction(db.get());
    int64_t nowMs = NowMs();
    ExpireOldSessions(db.get(), nowMs);
    std::optional<ControlSessionRecord> record;
    if (!token.empty()) {
        record = SelectByToken(db.get(), token);
    } else {
        record = SelectActiveForScope(db.get(), scope, nowMs);
    }
    transaction.commit();
    ControlSessionResult result;
    result.ok = true;
    if (record.has_value()) {
        result.record = *record;
    } else {
        result.code = "control_session_available";
    }
    return result;
}

bool HasActiveControlSession(const std::string& rawScope) {
    std::string scope = NormalizeScopeOrDefault(rawScope);
    if (!IsControlSessionScopeValid(scope)) {
        return false;
    }
    Db db;
    Transaction transaction(db.get());
    int64_t nowMs = NowMs();
    ExpireOldSessions(db.get(), nowMs);
    auto record = SelectActiveForScope(db.get(), scope, nowMs);
    transaction.commit();
    return record.has_value();
}

}
