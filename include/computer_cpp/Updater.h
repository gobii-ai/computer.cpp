#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace ComputerCpp::Updater {

struct ReleaseAsset {
    std::string name;
    std::string browserDownloadUrl;
    int64_t size = 0;
};

struct ReleaseInfo {
    std::string tagName;
    std::string version;
    std::string htmlUrl;
    std::string body;
    bool hasCompatibleAsset = false;
    ReleaseAsset asset;
};

enum class CheckStatus {
    UpdateAvailable,
    UpToDate,
    NoCompatibleAsset,
    UnsupportedPlatform,
    NetworkError,
    InvalidResponse,
};

struct CheckResult {
    CheckStatus status = CheckStatus::InvalidResponse;
    std::string currentVersion;
    std::string latestVersion;
    std::string message;
    long httpStatus = 0;
    ReleaseInfo release;
};

struct DownloadResult {
    bool ok = false;
    std::string error;
    std::filesystem::path zipPath;
};

struct StageResult {
    bool ok = false;
    std::string error;
    std::filesystem::path zipPath;
    std::filesystem::path rootDir;
    std::filesystem::path appBundlePath;
    std::filesystem::path cliPath;
};

struct InstallResult {
    bool ok = false;
    bool manualInstallRequired = false;
    std::string error;
    std::filesystem::path helperPath;
    std::filesystem::path zipPath;
};

using ProgressCallback = std::function<bool(int64_t downloaded, int64_t total)>;

std::string CurrentVersion();
CheckResult CheckForUpdate();

DownloadResult DownloadReleaseAsset(const ReleaseInfo& release, ProgressCallback progress = {});
StageResult StageDownloadedUpdate(const ReleaseInfo& release, const std::filesystem::path& zipPath, bool verifySignature = true);
InstallResult LaunchInstallAndRelaunch(const StageResult& staged, int currentPid);
bool RevealInFinder(const std::filesystem::path& path);

} // namespace ComputerCpp::Updater
