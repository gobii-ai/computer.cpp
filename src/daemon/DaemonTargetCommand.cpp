#include "DaemonTargetCommand.h"

#include "computer_cpp/StringUtils.h"

#include "DaemonParsing.h"
#include "DaemonProtocol.h"
#include "DaemonTargetRefs.h"
#include "DaemonTargetResolve.h"
#include "DaemonTargetText.h"

#include <vector>

namespace ComputerCpp {

namespace {

using json = nlohmann::json;

} // namespace

bool IsTargetCommand(const std::string& method) {
    return method == "target_find" || method == "target_resolve" || method == "target_explain";
}

nlohmann::json RunTargetCommand(const std::string& session, const std::string& method, const nlohmann::json& params) {
    if (auto unknown = UnknownParam(params, {
        "query", "target", "limit", "controlSession", "controlSessionToken", "controlScope"
    })) {
        return Error("unknown target parameter: " + *unknown, "invalid_target");
    }
    auto queryParam = StringParam(params, "query", "");
    auto targetParam = StringParam(params, "target", "");
    if (!queryParam || !targetParam) {
        return Error("target query/target must be strings", "invalid_target");
    }
    std::string rawTarget = !IsBlank(*queryParam) ? *queryParam : *targetParam;
    std::string trimmedTarget = Trim(rawTarget);
    bool removedVisualTarget = HasRemovedVisualTargetPrefix(trimmedTarget);
    auto roleTarget = ParseRoleTarget(rawTarget);
    if (roleTarget.malformed) {
        return Error("invalid role target selector", "invalid_target");
    }
    std::string query = removedVisualTarget ? std::string{} : TargetQueryString(rawTarget);
    if (!roleTarget.valid && query.empty() && trimmedTarget.empty()) {
        return Error("target query is required", "invalid_target");
    }
    auto limitParam = IntParam(params, "limit", method == "target_resolve" ? 1 : 20);
    if (!limitParam) {
        return Error("target search requires integer limit", "invalid_limit");
    }
    int limit = *limitParam;
    if (limit <= 0) {
        return Error("target search limit must be positive", "invalid_limit");
    }
    std::vector<json> candidates;
    if (roleTarget.valid) {
        candidates = RoleTargetCandidates(session, roleTarget, limit);
    } else if (!removedVisualTarget && !trimmedTarget.empty() && static_cast<int>(candidates.size()) < limit) {
        auto refs = SnapshotTargetCandidates(session, query, limit - static_cast<int>(candidates.size()));
        candidates.insert(candidates.end(), refs.begin(), refs.end());
    }

    if (!removedVisualTarget && candidates.empty()) {
        if (auto point = PointFromTarget(session, {{"target", rawTarget}})) {
            candidates.push_back({
                {"source", "coordinates"},
                {"target", rawTarget},
                {"coordinateSpace", "screen"},
                {"matchScore", 100000},
                {"clickPoint", {{"x", point->first}, {"y", point->second}}}
            });
        }
    }

    json data = {
        {"query", roleTarget.valid ? rawTarget : (removedVisualTarget ? rawTarget : query)},
        {"candidates", candidates},
        {"strategy", json({"accessibility/ref-store", "coordinates"})}
    };
    if (method == "target_resolve") {
        if (candidates.empty()) {
            if (removedVisualTarget) {
                return Error("visual target selectors were removed: " + rawTarget, "unsupported_visual_target");
            }
            return Error("target not found: " + rawTarget, "target_not_found");
        }
        data["target"] = candidates.front();
    }
    if (method == "target_explain") {
        data["explanation"] = candidates.empty()
            ? "No candidate matched current accessibility refs or coordinate targets."
            : "Selected first candidate from accessibility refs or coordinate targets.";
    }
    return Ok(data);
}

} // namespace ComputerCpp
