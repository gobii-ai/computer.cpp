#include "ControlSessionStore.h"

#include "computer_cpp/Timeline.h"

#include <sqlite3.h>

namespace ComputerCpp::ControlSessionStore {

ControlSessionResource RowToResource(sqlite3_stmt* stmt) {
    ControlSessionResource resource;
    resource.id = ColumnInt64(stmt, 0);
    resource.token = ColumnText(stmt, 1);
    resource.scope = ColumnText(stmt, 2);
    resource.resourceType = ColumnText(stmt, 3);
    resource.resourceId = ColumnText(stmt, 4);
    resource.label = ColumnText(stmt, 5);
    resource.state = ColumnText(stmt, 6);
    resource.createdAtMs = ColumnInt64(stmt, 7);
    resource.releasedAtMs = ColumnInt64(stmt, 8);
    resource.metadataJson = ColumnText(stmt, 9);
    return resource;
}

std::vector<ControlSessionResource> ActiveResources(sqlite3* db, const std::string& token) {
    Statement stmt(
        db,
        "SELECT id,token,scope,resource_type,resource_id,coalesce(label,''),state,created_at_ms,coalesce(released_at_ms,0),metadata_json "
        "FROM control_session_resources WHERE token=? AND state='active' ORDER BY id;");
    stmt.bindText(1, token);
    std::vector<ControlSessionResource> resources;
    while (IsRow(stmt.step())) {
        resources.push_back(RowToResource(stmt.get()));
    }
    return resources;
}

std::vector<ControlSessionResource> ActiveResources(const std::string& token) {
    if (token.empty()) {
        return {};
    }
    Db db;
    return ActiveResources(db.get(), token);
}

std::vector<ControlSessionResource> ExpiredResources(const std::string& rawScope) {
    std::string scope = rawScope.empty() ? kDefaultControlScope : rawScope;
    if (!IsControlSessionScopeValid(scope)) {
        return {};
    }
    Db db;
    Transaction transaction(db.get());
    ExpireOldSessions(db.get(), NowMs());
    Statement stmt(
        db.get(),
        "SELECT r.id,r.token,r.scope,r.resource_type,r.resource_id,coalesce(r.label,''),r.state,r.created_at_ms,coalesce(r.released_at_ms,0),r.metadata_json "
        "FROM control_session_resources r "
        "JOIN control_sessions s ON s.token=r.token "
        "WHERE r.scope=? AND r.state='active' AND s.state!='active' "
        "ORDER BY r.id;");
    stmt.bindText(1, scope);
    std::vector<ControlSessionResource> resources;
    while (IsRow(stmt.step())) {
        resources.push_back(RowToResource(stmt.get()));
    }
    transaction.commit();
    return resources;
}

void RegisterResource(
    const std::string& token,
    const std::string& resourceType,
    const std::string& resourceId,
    const std::string& label,
    const nlohmann::json& metadata
) {
    if (token.empty() || resourceType.empty() || resourceId.empty()) {
        return;
    }
    Db db;
    Transaction transaction(db.get());
    int64_t nowMs = NowMs();
    ExpireOldSessions(db.get(), nowMs);
    auto record = SelectByToken(db.get(), token);
    if (!record.has_value() || record->state != "active") {
        transaction.commit();
        return;
    }
    Statement closeExisting(
        db.get(),
        "UPDATE control_session_resources SET state='superseded', released_at_ms=? "
        "WHERE token=? AND resource_type=? AND resource_id=? AND state='active';");
    closeExisting.bindInt64(1, nowMs);
    closeExisting.bindText(2, token);
    closeExisting.bindText(3, resourceType);
    closeExisting.bindText(4, resourceId);
    closeExisting.expectDone();

    Statement stmt(
        db.get(),
        "INSERT INTO control_session_resources(token,scope,resource_type,resource_id,label,state,created_at_ms,released_at_ms,metadata_json) "
        "VALUES (?,?,?,?,?,'active',?,NULL,?);");
    stmt.bindText(1, token);
    stmt.bindText(2, record->scope);
    stmt.bindText(3, resourceType);
    stmt.bindText(4, resourceId);
    stmt.bindText(5, label);
    stmt.bindInt64(6, nowMs);
    stmt.bindText(7, metadata.dump());
    stmt.expectDone();
    InsertEvent(db.get(), *record, "resource_acquired", resourceType, "control session resource acquired", {
        {"resourceType", resourceType},
        {"resourceId", resourceId},
        {"label", label}
    });
    transaction.commit();
}

void ReleaseResource(const std::string& token, const std::string& resourceType, const std::string& resourceId) {
    if (token.empty() || resourceType.empty() || resourceId.empty()) {
        return;
    }
    Db db;
    Transaction transaction(db.get());
    int64_t nowMs = NowMs();
    auto record = SelectByToken(db.get(), token);
    Statement stmt(
        db.get(),
        "UPDATE control_session_resources SET state='released', released_at_ms=? "
        "WHERE token=? AND resource_type=? AND resource_id=? AND state='active';");
    stmt.bindInt64(1, nowMs);
    stmt.bindText(2, token);
    stmt.bindText(3, resourceType);
    stmt.bindText(4, resourceId);
    stmt.expectDone();
    if (record.has_value()) {
        InsertEvent(db.get(), *record, "resource_released", resourceType, "control session resource released", {
            {"resourceType", resourceType},
            {"resourceId", resourceId}
        });
    }
    transaction.commit();
}

void AbandonResource(
    const std::string& token,
    const std::string& resourceType,
    const std::string& resourceId,
    const std::string& reason
) {
    if (token.empty() || resourceType.empty() || resourceId.empty()) {
        return;
    }
    Db db;
    Transaction transaction(db.get());
    int64_t nowMs = NowMs();
    auto record = SelectByToken(db.get(), token);
    Statement stmt(
        db.get(),
        "UPDATE control_session_resources SET state='abandoned', released_at_ms=? "
        "WHERE token=? AND resource_type=? AND resource_id=? AND state='active';");
    stmt.bindInt64(1, nowMs);
    stmt.bindText(2, token);
    stmt.bindText(3, resourceType);
    stmt.bindText(4, resourceId);
    stmt.expectDone();
    if (record.has_value()) {
        InsertEvent(db.get(), *record, "resource_abandoned", resourceType, "control session resource abandoned", {
            {"resourceType", resourceType},
            {"resourceId", resourceId},
            {"reason", reason}
        });
    }
    transaction.commit();
}

} // namespace ComputerCpp::ControlSessionStore
