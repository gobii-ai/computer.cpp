#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace ComputerCpp {

struct ControlSessionRecord {
    std::string token;
    std::string scope;
    std::string daemonSession;
    std::string owner;
    std::string purpose;
    std::string state;
    int64_t createdAtMs = 0;
    int64_t acquiredAtMs = 0;
    int64_t renewedAtMs = 0;
    int64_t expiresAtMs = 0;
    int64_t releasedAtMs = 0;
    int64_t lastSeenAtMs = 0;
    int64_t maxRuntimeMs = 0;
    int64_t maxExpiresAtMs = 0;
};

struct ControlSessionEvent {
    int64_t id = 0;
    std::string token;
    std::string scope;
    std::string daemonSession;
    std::string owner;
    std::string purpose;
    std::string event;
    std::string code;
    std::string message;
    int64_t createdAtMs = 0;
    std::string metadataJson;
};

struct ControlSessionResource {
    int64_t id = 0;
    std::string token;
    std::string scope;
    std::string resourceType;
    std::string resourceId;
    std::string label;
    std::string state;
    int64_t createdAtMs = 0;
    int64_t releasedAtMs = 0;
    std::string metadataJson;
};

struct ControlSessionResult {
    bool ok = false;
    std::string code;
    std::string error;
    ControlSessionRecord record;
    std::optional<ControlSessionRecord> holder;
    bool released = false;
};

struct ControlSessionAcquireOptions {
    std::string scope = "desktop:local";
    std::string daemonSession = "default";
    std::string owner;
    std::string purpose;
    int64_t ttlMs = 10 * 60 * 1000;
    int64_t waitMs = 0;
    int64_t maxRuntimeMs = 0;
};

constexpr const char* kDefaultControlScope = "desktop:local";

bool IsControlSessionScopeValid(const std::string& scope);
int64_t ClampControlSessionTtlMs(int64_t ttlMs);
int64_t ClampControlSessionWaitMs(int64_t waitMs);
int64_t ClampControlSessionMaxRuntimeMs(int64_t maxRuntimeMs);

ControlSessionResult AcquireControlSession(const ControlSessionAcquireOptions& options);
ControlSessionResult AcquireOrResumeControlSession(const ControlSessionAcquireOptions& options);
ControlSessionResult RenewControlSession(const std::string& token, int64_t ttlMs);
ControlSessionResult ReleaseControlSession(const std::string& token);
ControlSessionResult ReleaseActiveControlSession(
    const std::string& scope,
    const std::string& expectedOwner = "",
    const std::string& expectedPurpose = "",
    const std::string& reason = ""
);
ControlSessionResult ValidateControlSession(const std::string& token, const std::string& expectedScope);
ControlSessionResult GetControlSessionStatus(const std::string& scope, const std::string& token = "");
bool HasActiveControlSession(const std::string& scope);
std::vector<ControlSessionEvent> RecentControlSessionEvents(const std::string& scope, int limit = 50);
std::vector<ControlSessionResource> ActiveControlSessionResources(const std::string& token);
std::vector<ControlSessionResource> ExpiredControlSessionResources(const std::string& scope);
void RegisterControlSessionResource(
    const std::string& token,
    const std::string& resourceType,
    const std::string& resourceId,
    const std::string& label,
    const nlohmann::json& metadata = nlohmann::json::object()
);
void ReleaseControlSessionResource(const std::string& token, const std::string& resourceType, const std::string& resourceId);
void AbandonControlSessionResource(
    const std::string& token,
    const std::string& resourceType,
    const std::string& resourceId,
    const std::string& reason = ""
);
nlohmann::json ControlSessionMetricsJson(
    const std::string& scope = kDefaultControlScope,
    int64_t staleAfterMs = 60 * 1000,
    int64_t longRunningAfterMs = 30 * 60 * 1000,
    int eventLimit = 20
);
std::string ControlSessionPrometheus(
    const std::string& scope = kDefaultControlScope,
    int64_t staleAfterMs = 60 * 1000,
    int64_t longRunningAfterMs = 30 * 60 * 1000
);

nlohmann::json ControlSessionRecordToJson(const ControlSessionRecord& record, bool includeToken = true);
nlohmann::json ControlSessionEventToJson(const ControlSessionEvent& event);
nlohmann::json ControlSessionResourceToJson(const ControlSessionResource& resource);

}
