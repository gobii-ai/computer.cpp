#pragma once

#include "computer_cpp/Updater.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace ComputerCpp::Updater {

struct SemVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string normalized;
};

std::string GitHubRepository();
std::string LatestReleaseApiUrl();
std::string CompatibleMacAssetName(std::string_view version);
std::string CompatibleWindowsAssetName(std::string_view version);
std::string CompatibleAssetName(std::string_view version);
std::string ShellQuote(std::string_view value);

std::optional<SemVersion> ParseSemVersion(std::string_view value);
int CompareSemVersions(const SemVersion& left, const SemVersion& right);
std::optional<int> CompareVersionStrings(std::string_view left, std::string_view right);

CheckResult ParseGitHubLatestRelease(const nlohmann::json& release, std::string_view currentVersion);
CheckResult ParseGitHubLatestReleaseBody(std::string_view body, std::string_view currentVersion);

std::filesystem::path CurrentAppBundlePath();
std::filesystem::path CurrentSiblingCliPath(const std::filesystem::path& appBundlePath);
bool IsMacArm64Supported();
bool IsWindowsX64Supported();
bool IsSelfUpdateSupported();
std::string BuildInstallHelperScript(
    int currentPid,
    const std::filesystem::path& stagedApp,
    const std::filesystem::path& stagedCli,
    const std::filesystem::path& targetApp,
    const std::filesystem::path& targetCli);

} // namespace ComputerCpp::Updater
