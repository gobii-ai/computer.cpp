#include "DaemonTargetRefs.h"

#include "computer_cpp/AppPaths.h"
#include "computer_cpp/RefStore.h"
#include "computer_cpp/StringUtils.h"

#include "DaemonJson.h"

namespace ComputerCpp {
namespace {

nlohmann::json RefCandidateJson(const Platform::RefRecord& ref) {
    return {
        {"source", ref.source.empty() ? "accessibility" : ref.source},
        {"target", "@" + ref.ref},
        {"text", ref.name.empty() ? ref.value : ref.name},
        {"role", ref.role},
        {"bounds", BoundsToJson(ref.bounds)},
        {"confidence", ref.confidence}
    };
}

} // namespace

std::vector<nlohmann::json> RoleTargetCandidates(const std::string& session, const RoleTarget& roleTarget, int limit) {
    std::vector<nlohmann::json> candidates;
    auto refs = LoadRefs(RefStorePath(session));
    for (const auto& ref : refs) {
        if (NormalizeRole(ref.role) != roleTarget.role) {
            continue;
        }
        if (!roleTarget.name.empty()) {
            std::string haystack = ref.name.empty() ? ref.value : ref.name;
            if (!ContainsCaseInsensitive(haystack, roleTarget.name)) {
                continue;
            }
        }
        candidates.push_back(RefCandidateJson(ref));
        if (static_cast<int>(candidates.size()) >= limit) {
            break;
        }
    }
    return candidates;
}

std::vector<nlohmann::json> SnapshotTargetCandidates(const std::string& session, const std::string& query, int limit) {
    std::vector<nlohmann::json> candidates;
    auto refs = LoadRefs(RefStorePath(session));
    for (const auto& ref : refs) {
        std::string haystack = ref.name + " " + ref.value + " " + ref.role;
        if (ContainsCaseInsensitive(haystack, query)) {
            candidates.push_back(RefCandidateJson(ref));
            if (static_cast<int>(candidates.size()) >= limit) {
                break;
            }
        }
    }
    return candidates;
}

} // namespace ComputerCpp
