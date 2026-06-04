#include "computer_cpp/ControlSession.h"

namespace ComputerCpp {
namespace {

nlohmann::json ParseStoredMetadata(std::string_view value) {
    auto parsed = nlohmann::json::parse(value.empty() ? "{}" : value, nullptr, false);
    if (parsed.is_discarded()) {
        return {{"raw", std::string(value)}};
    }
    return parsed;
}

} // namespace

nlohmann::json ControlSessionRecordToJson(const ControlSessionRecord& record, bool includeToken) {
    nlohmann::json data = {
        {"scope", record.scope},
        {"daemonSession", record.daemonSession},
        {"owner", record.owner},
        {"purpose", record.purpose},
        {"state", record.state},
        {"createdAtMs", record.createdAtMs},
        {"acquiredAtMs", record.acquiredAtMs},
        {"renewedAtMs", record.renewedAtMs},
        {"expiresAtMs", record.expiresAtMs},
        {"releasedAtMs", record.releasedAtMs},
        {"lastSeenAtMs", record.lastSeenAtMs},
        {"maxRuntimeMs", record.maxRuntimeMs},
        {"maxExpiresAtMs", record.maxExpiresAtMs},
    };
    if (includeToken) {
        data["token"] = record.token;
    }
    return data;
}

nlohmann::json ControlSessionEventToJson(const ControlSessionEvent& event) {
    nlohmann::json metadata = ParseStoredMetadata(event.metadataJson);
    return {
        {"id", event.id},
        {"token", event.token.empty() ? "" : event.token.substr(0, 8)},
        {"scope", event.scope},
        {"daemonSession", event.daemonSession},
        {"owner", event.owner},
        {"purpose", event.purpose},
        {"event", event.event},
        {"code", event.code},
        {"message", event.message},
        {"createdAtMs", event.createdAtMs},
        {"metadata", metadata},
    };
}

nlohmann::json ControlSessionResourceToJson(const ControlSessionResource& resource) {
    nlohmann::json metadata = ParseStoredMetadata(resource.metadataJson);
    return {
        {"id", resource.id},
        {"token", resource.token.empty() ? "" : resource.token.substr(0, 8)},
        {"scope", resource.scope},
        {"resourceType", resource.resourceType},
        {"resourceId", resource.resourceId},
        {"label", resource.label},
        {"state", resource.state},
        {"createdAtMs", resource.createdAtMs},
        {"releasedAtMs", resource.releasedAtMs},
        {"metadata", metadata},
    };
}

} // namespace ComputerCpp
