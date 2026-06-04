#include "DaemonSnapshot.h"

#include "computer_cpp/AppPaths.h"
#include "computer_cpp/Platform.h"
#include "computer_cpp/RefStore.h"
#include "computer_cpp/StringUtils.h"
#include "DaemonJson.h"
#include "DaemonParsing.h"
#include "DaemonProtocol.h"

namespace ComputerCpp {

nlohmann::json RunGetCommand(const std::string& session, const nlohmann::json& params) {
    if (auto unknown = UnknownParam(params, {"target", "field", "controlSession", "controlSessionToken", "controlScope"})) {
        return Error("unknown get parameter: " + *unknown, "invalid_target");
    }

    auto targetParam = StringParam(params, "target", "");
    auto fieldParam = StringParam(params, "field", "all");
    if (!targetParam || !fieldParam) {
        return Error("get requires string target/field", "invalid_target");
    }
    std::string refName = *targetParam;
    if (IsBlank(refName)) {
        return Error("get requires target ref", "invalid_target");
    }
    std::string field = *fieldParam;
    if (field != "text" && field != "value" && field != "bounds" && field != "all") {
        return Error("get field must be text, value, bounds, or all", "invalid_target");
    }

    auto refs = LoadRefs(RefStorePath(session));
    auto found = FindRef(refs, refName);
    if (!found.has_value()) {
        return Error("ref not found: " + refName, "target_not_found");
    }

    nlohmann::json refJson = RefToJson(*found);
    if (field == "text") {
        return Ok({{"text", found->name.empty() ? found->value : found->name}, {"ref", refJson}});
    }
    if (field == "value") {
        return Ok({{"value", found->value}, {"ref", refJson}});
    }
    if (field == "bounds") {
        return Ok({{"bounds", BoundsToJson(found->bounds)}, {"ref", refJson}});
    }
    return Ok({{"ref", refJson}});
}

nlohmann::json RunSnapshotCommand(const std::string& session, const nlohmann::json& params) {
    if (auto unknown = UnknownParam(params, {
        "interactive", "bounds", "actions", "maxDepth", "maxNodes",
        "controlSession", "controlSessionToken", "controlScope"
    })) {
        return Error("unknown snapshot parameter: " + *unknown, "invalid_snapshot");
    }

    auto interactive = BoolParam(params, "interactive", false);
    auto bounds = BoolParam(params, "bounds", false);
    auto actions = BoolParam(params, "actions", false);
    auto maxDepth = IntParam(params, "maxDepth", 8);
    auto maxNodes = IntParam(params, "maxNodes", 700);
    if (!interactive || !bounds || !actions || !maxDepth || !maxNodes) {
        return Error("snapshot requires boolean interactive/bounds/actions and integer maxDepth/maxNodes", "invalid_snapshot");
    }
    if (*maxDepth < 0 || *maxNodes < 0) {
        return Error("snapshot requires non-negative maxDepth and maxNodes", "invalid_snapshot");
    }

    Platform::SnapshotOptions options;
    options.interactiveOnly = *interactive;
    options.includeBounds = *bounds;
    options.includeActions = *actions;
    options.maxDepth = *maxDepth;
    options.maxNodes = *maxNodes;

    auto snapshot = Platform::TakeSnapshot(options);
    SaveRefs(RefStorePath(session), snapshot.refs);

    nlohmann::json refs = nlohmann::json::array();
    for (const auto& ref : snapshot.refs) {
        refs.push_back(RefToJson(ref));
    }

    nlohmann::json data = {
        {"text", snapshot.text},
        {"frontmostApp", AppToJson(snapshot.frontmostApp)},
        {"frontmostWindowBounds", BoundsToJson(snapshot.frontmostWindowBounds)},
        {"refs", refs},
        {"refStore", RefStorePath(session).string()}
    };
    if (!snapshot.warning.empty()) {
        data["warning"] = snapshot.warning;
    }
    return Ok(data);
}

} // namespace ComputerCpp
