#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace ComputerCpp {

constexpr const char* kFeatureMapSchemaVersion = "2";

struct RuntimeProvenance {
    int pid = 0;
    std::string executablePath;
    std::string executableFingerprint;
    std::string buildGitSha;
    std::string buildTimestamp;
    std::string featureMapSchemaVersion = kFeatureMapSchemaVersion;
};

std::string CurrentExecutablePath();
std::string ExecutableFingerprint(const std::string& path);
RuntimeProvenance RuntimeProvenanceForExecutable(const std::string& path);
RuntimeProvenance CurrentRuntimeProvenance();
nlohmann::json RuntimeProvenanceToJson(const RuntimeProvenance& provenance);

}
