#include "computer_cpp/RefStore.h"

#include "computer_cpp/AppPaths.h"
#include "computer_cpp/StringUtils.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace ComputerCpp {

namespace {

nlohmann::json BoundsToJson(const Platform::Bounds& bounds) {
    return {
        {"available", bounds.available},
        {"x", bounds.x},
        {"y", bounds.y},
        {"width", bounds.width},
        {"height", bounds.height}
    };
}

Platform::Bounds BoundsFromJson(const nlohmann::json& value) {
    Platform::Bounds bounds;
    if (!value.is_object()) {
        return bounds;
    }
    bounds.available = value.value("available", false);
    bounds.x = value.value("x", 0.0);
    bounds.y = value.value("y", 0.0);
    bounds.width = value.value("width", 0.0);
    bounds.height = value.value("height", 0.0);
    return bounds;
}

nlohmann::json RefToJson(const Platform::RefRecord& ref) {
    return {
        {"ref", ref.ref},
        {"kind", ref.kind},
        {"source", ref.source},
        {"role", ref.role},
        {"name", ref.name},
        {"value", ref.value},
        {"app", ref.app},
        {"pid", ref.pid},
        {"bounds", BoundsToJson(ref.bounds)},
        {"confidence", ref.confidence}
    };
}

Platform::RefRecord RefFromJson(const nlohmann::json& value) {
    Platform::RefRecord ref;
    if (!value.is_object()) {
        return ref;
    }
    ref.ref = value.value("ref", "");
    ref.kind = value.value("kind", "");
    ref.source = value.value("source", "");
    ref.role = value.value("role", "");
    ref.name = value.value("name", "");
    ref.value = value.value("value", "");
    ref.app = value.value("app", "");
    ref.pid = value.value("pid", -1);
    ref.bounds = BoundsFromJson(value.value("bounds", nlohmann::json::object()));
    ref.confidence = value.value("confidence", 1.0);
    return ref;
}

}

void SaveRefs(const std::filesystem::path& path, const std::vector<Platform::RefRecord>& refs) {
    EnsureDirectory(path.parent_path());
    nlohmann::json payload = nlohmann::json::array();
    for (const auto& ref : refs) {
        payload.push_back(RefToJson(ref));
    }
    std::ofstream out(path);
    out << payload.dump(2);
}

std::vector<Platform::RefRecord> LoadRefs(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return {};
    }

    nlohmann::json payload;
    in >> payload;
    std::vector<Platform::RefRecord> refs;
    if (!payload.is_array()) {
        return refs;
    }
    for (const auto& item : payload) {
        auto ref = RefFromJson(item);
        if (!ref.ref.empty()) {
            refs.push_back(ref);
        }
    }
    return refs;
}

std::optional<Platform::RefRecord> FindRef(const std::vector<Platform::RefRecord>& refs, const std::string& ref) {
    std::string normalized = Trim(ref);
    if (!normalized.empty() && normalized.front() == '@') {
        normalized.erase(normalized.begin());
    }
    for (const auto& candidate : refs) {
        if (candidate.ref == normalized || "@" + candidate.ref == ref) {
            return candidate;
        }
    }
    return std::nullopt;
}

}
