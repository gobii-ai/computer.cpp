#include "computer_cpp/ControlSession.h"

#include "ControlSessionStore.h"

namespace ComputerCpp {

std::vector<ControlSessionEvent> RecentControlSessionEvents(const std::string& rawScope, int limit) {
    return ControlSessionStore::RecentEvents(rawScope, limit);
}

std::vector<ControlSessionResource> ActiveControlSessionResources(const std::string& token) {
    return ControlSessionStore::ActiveResources(token);
}

std::vector<ControlSessionResource> ExpiredControlSessionResources(const std::string& rawScope) {
    return ControlSessionStore::ExpiredResources(rawScope);
}

void RegisterControlSessionResource(
    const std::string& token,
    const std::string& resourceType,
    const std::string& resourceId,
    const std::string& label,
    const nlohmann::json& metadata
) {
    ControlSessionStore::RegisterResource(token, resourceType, resourceId, label, metadata);
}

void ReleaseControlSessionResource(const std::string& token, const std::string& resourceType, const std::string& resourceId) {
    ControlSessionStore::ReleaseResource(token, resourceType, resourceId);
}

void AbandonControlSessionResource(
    const std::string& token,
    const std::string& resourceType,
    const std::string& resourceId,
    const std::string& reason
) {
    ControlSessionStore::AbandonResource(token, resourceType, resourceId, reason);
}

} // namespace ComputerCpp
