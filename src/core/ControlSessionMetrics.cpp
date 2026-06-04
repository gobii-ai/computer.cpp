#include "computer_cpp/ControlSession.h"

#include "ControlSessionStore.h"

#include "computer_cpp/Timeline.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace ComputerCpp {

nlohmann::json ControlSessionMetricsJson(
    const std::string& rawScope,
    int64_t staleAfterMs,
    int64_t longRunningAfterMs,
    int eventLimit
) {
    std::string scope = rawScope.empty() ? kDefaultControlScope : rawScope;
    nlohmann::json metrics = {
        {"scope", scope},
        {"nowMs", NowMs()},
        {"available", true},
        {"thresholds", {
            {"staleAfterMs", staleAfterMs},
            {"longRunningAfterMs", longRunningAfterMs}
        }}
    };
    if (!IsControlSessionScopeValid(scope)) {
        metrics["error"] = "invalid_control_scope";
        return metrics;
    }

    ControlSessionStore::Db db;
    ControlSessionStore::Transaction transaction(db.get());
    int64_t nowMs = NowMs();
    metrics["nowMs"] = nowMs;
    ControlSessionStore::ExpireOldSessions(db.get(), nowMs);
    auto active = ControlSessionStore::SelectActiveForScope(db.get(), scope, nowMs);
    auto counts = ControlSessionStore::EventCounts(db.get(), scope);
    nlohmann::json eventCounts = nlohmann::json::object();
    for (const auto& [event, count] : counts) {
        eventCounts[event] = count;
    }
    metrics["eventCounts"] = eventCounts;

    nlohmann::json events = nlohmann::json::array();
    for (const auto& event : ControlSessionStore::RecentEvents(db.get(), scope, std::clamp(eventLimit, 1, 100))) {
        events.push_back(ControlSessionEventToJson(event));
    }
    metrics["recentEvents"] = events;
    transaction.commit();

    if (active.has_value()) {
        int64_t ageMs = std::max<int64_t>(0, nowMs - active->acquiredAtMs);
        int64_t idleMs = std::max<int64_t>(0, nowMs - active->lastSeenAtMs);
        int64_t expiresInMs = active->expiresAtMs - nowMs;
        int64_t maxRemainingMs = active->maxExpiresAtMs > 0 ? active->maxExpiresAtMs - nowMs : 0;
        bool stale = staleAfterMs > 0 && idleMs >= staleAfterMs;
        bool longRunning = (longRunningAfterMs > 0 && ageMs >= longRunningAfterMs) ||
            (active->maxRuntimeMs > 0 && ageMs >= active->maxRuntimeMs * 8 / 10);
        auto resources = ActiveControlSessionResources(active->token);
        nlohmann::json resourceJson = nlohmann::json::array();
        for (const auto& resource : resources) {
            resourceJson.push_back(ControlSessionResourceToJson(resource));
        }
        metrics["available"] = false;
        metrics["active"] = ControlSessionRecordToJson(*active, false);
        metrics["active"]["ageMs"] = ageMs;
        metrics["active"]["idleMs"] = idleMs;
        metrics["active"]["expiresInMs"] = expiresInMs;
        metrics["active"]["maxRemainingMs"] = maxRemainingMs;
        metrics["active"]["stale"] = stale;
        metrics["active"]["longRunning"] = longRunning;
        metrics["active"]["activeResources"] = resourceJson;
        metrics["active"]["activeResourceCount"] = static_cast<int>(resources.size());
    }
    return metrics;
}

} // namespace ComputerCpp
