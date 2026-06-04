#include "CliSessionCleanup.h"

#include "computer_cpp/ControlSession.h"
#include "computer_cpp/Transport.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <utility>
#include <vector>

using json = nlohmann::json;

namespace ComputerCpp::Cli {
namespace {

json MakeRequest(const std::string& method, json params = json::object()) {
    return {
        {"id", method},
        {"method", method},
        {"params", std::move(params)},
    };
}

bool CleanupControlSessionResourceList(
    const CliOptions& options,
    const std::vector<ControlSessionResource>& resources,
    const std::string& cleanupToken,
    const std::string& scope,
    bool releaseOriginalResource,
    bool abandonOriginalOnFailure = false
) {
    bool ok = true;
    for (const auto& resource : resources) {
        if (resource.resourceType != "window") {
            std::cerr << "Warning: no cleanup handler for active control session resource "
                      << resource.resourceType << ":" << resource.resourceId << "\n";
            if (abandonOriginalOnFailure) {
                AbandonControlSessionResource(resource.token, resource.resourceType, resource.resourceId, "no cleanup handler");
            } else {
                ok = false;
            }
            continue;
        }

        json params = {
            {"id", resource.resourceId},
            {"controlSession", cleanupToken},
            {"controlScope", scope},
        };
        json response = SendDaemonRequest(options.session, MakeRequest("window_close", params));
        if (!response.value("ok", false)) {
            std::string reason = response.value("error", response.value("code", "unknown error"));
            std::cerr << "Warning: failed to close managed window " << resource.resourceId << ": " << reason << "\n";
            if (abandonOriginalOnFailure) {
                AbandonControlSessionResource(resource.token, "window", resource.resourceId, reason);
            } else {
                ok = false;
            }
            continue;
        }

        json data = response.value("data", json::object());
        if (data.value("found", false) == false) {
            ReleaseControlSessionResource(resource.token, "window", resource.resourceId);
            continue;
        }
        if (!data.value("closed", false)) {
            std::cerr << "Warning: managed window " << resource.resourceId << " was found but did not close cleanly\n";
            if (abandonOriginalOnFailure) {
                AbandonControlSessionResource(resource.token, "window", resource.resourceId, "window did not close cleanly");
            } else {
                ok = false;
            }
            continue;
        }
        if (releaseOriginalResource && resource.token != cleanupToken) {
            ReleaseControlSessionResource(resource.token, "window", resource.resourceId);
        }
    }
    return ok;
}

}

bool CleanupControlSessionResources(const CliOptions& options, const std::string& token, const std::string& scope) {
    auto resources = ActiveControlSessionResources(token);
    bool ok = CleanupControlSessionResourceList(options, resources, token, scope, false);

    auto remaining = ActiveControlSessionResources(token);
    if (!remaining.empty()) {
        ok = false;
        for (const auto& resource : remaining) {
            std::cerr << "Warning: active resource still attached to lease after cleanup: "
                      << resource.resourceType << ":" << resource.resourceId << "\n";
        }
    }
    return ok;
}

bool CleanupExpiredControlSessionResources(const CliOptions& options, const std::string& cleanupToken, const std::string& scope) {
    auto resources = ExpiredControlSessionResources(scope);
    if (resources.empty()) {
        return true;
    }
    std::cerr << "cleaning up " << resources.size() << " expired control session resource(s)\n";
    bool ok = CleanupControlSessionResourceList(options, resources, cleanupToken, scope, true, true);
    auto remaining = ExpiredControlSessionResources(scope);
    if (!remaining.empty()) {
        ok = false;
        for (const auto& resource : remaining) {
            std::cerr << "Warning: expired resource still attached after cleanup: "
                      << resource.resourceType << ":" << resource.resourceId
                      << " token=" << (resource.token.size() > 8 ? resource.token.substr(0, 8) : resource.token)
                      << "\n";
        }
    }
    return ok;
}

}
