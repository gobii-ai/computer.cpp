#include "UpdaterInternal.h"

#include "CurlHandle.h"
#include "computer_cpp/AppPaths.h"
#include "computer_cpp/RuntimeProvenance.h"
#include "computer_cpp/StringUtils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#if defined(__APPLE__)
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#ifndef COMPUTER_CPP_PROJECT_VERSION
#define COMPUTER_CPP_PROJECT_VERSION "0.0.0"
#endif

#ifndef COMPUTER_CPP_GITHUB_REPOSITORY
#define COMPUTER_CPP_GITHUB_REPOSITORY "gobii-ai/computer.cpp"
#endif

using json = nlohmann::json;

namespace fs = std::filesystem;

namespace ComputerCpp::Updater {
namespace {

constexpr const char* kBundleId = "org.computercpp.app";

size_t WriteStringCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    size_t count = size * nmemb;
    out->append(ptr, count);
    return count;
}

size_t WriteFileCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::ofstream*>(userdata);
    size_t count = size * nmemb;
    out->write(ptr, static_cast<std::streamsize>(count));
    return out->good() ? count : 0;
}

struct ProgressContext {
    ProgressCallback callback;
};

int CurlProgressCallback(void* clientp, curl_off_t total, curl_off_t downloaded, curl_off_t, curl_off_t) {
    auto* context = static_cast<ProgressContext*>(clientp);
    if (!context || !context->callback) {
        return 0;
    }
    return context->callback(downloaded, total) ? 0 : 1;
}

bool AppendHeader(CurlHeaders& headers, const std::string& header, std::string* error) {
    if (headers.append(header)) {
        return true;
    }
    if (error) {
        *error = "could not allocate libcurl headers";
    }
    return false;
}

std::string HttpGet(const std::string& url, long* httpStatus, std::string* error) {
    CurlHandle curl;
    if (!curl.valid()) {
        if (error) {
            *error = "could not initialize libcurl";
        }
        return {};
    }

    CurlHeaders headers;
    if (!AppendHeader(headers, "Accept: application/vnd.github+json", error) ||
        !AppendHeader(headers, "X-GitHub-Api-Version: 2022-11-28", error)) {
        return {};
    }

    std::string body;
    std::string userAgent = "computer.cpp/" + CurrentVersion();
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 20000L);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, userAgent.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteStringCallback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &body);

    CURLcode code = curl_easy_perform(curl.get());
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if (httpStatus) {
        *httpStatus = status;
    }
    if (code != CURLE_OK) {
        if (error) {
            *error = curl_easy_strerror(code);
        }
        return {};
    }
    if (status < 200 || status >= 300) {
        if (error) {
            *error = "GitHub returned HTTP " + std::to_string(status);
        }
        return {};
    }
    return body;
}

bool RunCommand(const std::string& command) {
    return std::system(command.c_str()) == 0;
}

std::string RunCommandCapture(const std::string& command) {
    std::array<char, 256> buffer{};
    std::string output;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return {};
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        output += buffer.data();
    }
    int status = pclose(pipe);
    if (status != 0) {
        return {};
    }
    return Trim(output);
}

std::string PlistValue(const fs::path& infoPlist, const std::string& key) {
#if defined(__APPLE__)
    std::string command = "/usr/libexec/PlistBuddy -c " + ShellQuote("Print :" + key) + " " +
        ShellQuote(infoPlist.string()) + " 2>/dev/null";
    return RunCommandCapture(command);
#else
    (void)infoPlist;
    (void)key;
    return {};
#endif
}

bool IsWritableDirectory(const fs::path& dir) {
    std::error_code ec;
    if (!fs::exists(dir, ec) || ec) {
        return false;
    }
#if defined(__APPLE__)
    fs::path probe = dir / (".computer.cpp-update-write-test-" + std::to_string(getpid()));
#else
    fs::path probe = dir / ".computer.cpp-update-write-test";
#endif
    {
        std::ofstream out(probe);
        if (!out) {
            return false;
        }
        out << "test\n";
    }
    fs::remove(probe, ec);
    return true;
}

fs::path UpdateRootDir(const std::string& version) {
    return AppDataDir() / "updates" / version;
}

StageResult ValidateStagedUpdate(
    const ReleaseInfo& release,
    const fs::path& zipPath,
    const fs::path& rootDir,
    bool verifySignature) {
    StageResult result;
    result.zipPath = zipPath;
    result.rootDir = rootDir;
    result.appBundlePath = rootDir / "ComputerCpp.app";
    result.cliPath = rootDir / "computer.cpp";

    std::error_code ec;
    if (!fs::is_directory(result.appBundlePath, ec) || ec) {
        result.error = "downloaded update does not contain ComputerCpp.app";
        return result;
    }
    ec.clear();
    if (!fs::is_regular_file(result.cliPath, ec) || ec) {
        result.error = "downloaded update does not contain computer.cpp";
        return result;
    }

    fs::path infoPlist = result.appBundlePath / "Contents" / "Info.plist";
    if (!fs::is_regular_file(infoPlist, ec) || ec) {
        result.error = "downloaded update is missing ComputerCpp.app/Contents/Info.plist";
        return result;
    }

    std::string bundleId = PlistValue(infoPlist, "CFBundleIdentifier");
    if (bundleId != kBundleId) {
        result.error = "downloaded app has unexpected bundle id: " + (bundleId.empty() ? "unknown" : bundleId);
        return result;
    }
    std::string bundleVersion = PlistValue(infoPlist, "CFBundleShortVersionString");
    if (bundleVersion != release.version) {
        result.error = "downloaded app version " + (bundleVersion.empty() ? "unknown" : bundleVersion) +
            " does not match release " + release.version;
        return result;
    }

    if (verifySignature) {
#if defined(__APPLE__)
        if (!RunCommand("/usr/bin/codesign --verify --deep --strict " + ShellQuote(result.appBundlePath.string()) +
                        " >/dev/null 2>&1")) {
            result.error = "downloaded app failed code signature verification";
            return result;
        }
        if (!RunCommand("/usr/bin/codesign --verify --strict " + ShellQuote(result.cliPath.string()) +
                        " >/dev/null 2>&1")) {
            result.error = "downloaded CLI failed code signature verification";
            return result;
        }
        if (!RunCommand("/usr/sbin/spctl --assess --type execute " + ShellQuote(result.appBundlePath.string()) +
                        " >/dev/null 2>&1")) {
            result.error = "downloaded app failed Gatekeeper assessment";
            return result;
        }
#else
        result.error = "self-update validation is only supported on macOS";
        return result;
#endif
    }

    result.ok = true;
    return result;
}

} // namespace

std::string CurrentVersion() {
    return COMPUTER_CPP_PROJECT_VERSION;
}

std::string GitHubRepository() {
    return COMPUTER_CPP_GITHUB_REPOSITORY;
}

std::string LatestReleaseApiUrl() {
    return "https://api.github.com/repos/" + GitHubRepository() + "/releases/latest";
}

std::string CompatibleMacAssetName(std::string_view version) {
    return "computer.cpp-" + std::string(version) + "-macos-arm64.zip";
}

std::string ShellQuote(std::string_view value) {
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

std::optional<SemVersion> ParseSemVersion(std::string_view value) {
    std::string text = Trim(std::string(value));
    if (!text.empty() && text[0] == 'v') {
        text.erase(text.begin());
    }
    std::vector<int> parts;
    std::stringstream stream(text);
    std::string part;
    while (std::getline(stream, part, '.')) {
        if (part.empty() || !std::all_of(part.begin(), part.end(), [](unsigned char c) { return std::isdigit(c); })) {
            return std::nullopt;
        }
        try {
            parts.push_back(std::stoi(part));
        } catch (...) {
            return std::nullopt;
        }
    }
    if (parts.size() != 3) {
        return std::nullopt;
    }
    SemVersion version;
    version.major = parts[0];
    version.minor = parts[1];
    version.patch = parts[2];
    version.normalized = std::to_string(version.major) + "." + std::to_string(version.minor) + "." +
        std::to_string(version.patch);
    return version;
}

int CompareSemVersions(const SemVersion& left, const SemVersion& right) {
    if (left.major != right.major) {
        return left.major < right.major ? -1 : 1;
    }
    if (left.minor != right.minor) {
        return left.minor < right.minor ? -1 : 1;
    }
    if (left.patch != right.patch) {
        return left.patch < right.patch ? -1 : 1;
    }
    return 0;
}

std::optional<int> CompareVersionStrings(std::string_view left, std::string_view right) {
    auto parsedLeft = ParseSemVersion(left);
    auto parsedRight = ParseSemVersion(right);
    if (!parsedLeft.has_value() || !parsedRight.has_value()) {
        return std::nullopt;
    }
    return CompareSemVersions(*parsedLeft, *parsedRight);
}

CheckResult ParseGitHubLatestRelease(const json& release, std::string_view currentVersion) {
    CheckResult result;
    result.currentVersion = std::string(currentVersion);

    if (!release.is_object()) {
        result.status = CheckStatus::InvalidResponse;
        result.message = "GitHub release response was not an object";
        return result;
    }

    std::string tagName = release.value("tag_name", "");
    auto latest = ParseSemVersion(tagName);
    auto current = ParseSemVersion(currentVersion);
    if (!latest.has_value()) {
        result.status = CheckStatus::InvalidResponse;
        result.message = "GitHub latest release tag is not a semver version";
        return result;
    }
    if (!current.has_value()) {
        result.status = CheckStatus::InvalidResponse;
        result.message = "current app version is not a semver version";
        return result;
    }

    result.latestVersion = latest->normalized;
    result.release.tagName = tagName;
    result.release.version = latest->normalized;
    result.release.htmlUrl = release.value("html_url", "");
    result.release.body = release.value("body", "");

    int comparison = CompareSemVersions(*current, *latest);
    if (comparison >= 0) {
        result.status = CheckStatus::UpToDate;
        result.message = "ComputerCpp is up to date.";
        return result;
    }

    std::string expectedAssetName = CompatibleMacAssetName(latest->normalized);
    if (!release.contains("assets") || !release["assets"].is_array()) {
        result.status = CheckStatus::NoCompatibleAsset;
        result.message = "GitHub release has no assets.";
        return result;
    }

    for (const auto& asset : release["assets"]) {
        if (!asset.is_object() || asset.value("name", "") != expectedAssetName) {
            continue;
        }
        std::string url = asset.value("browser_download_url", "");
        if (url.empty()) {
            result.status = CheckStatus::InvalidResponse;
            result.message = "compatible release asset is missing browser_download_url";
            return result;
        }
        result.release.hasCompatibleAsset = true;
        result.release.asset.name = expectedAssetName;
        result.release.asset.browserDownloadUrl = url;
        result.release.asset.size = asset.value("size", 0LL);
        result.status = CheckStatus::UpdateAvailable;
        result.message = "ComputerCpp " + latest->normalized + " is available.";
        return result;
    }

    result.status = CheckStatus::NoCompatibleAsset;
    result.message = "GitHub release " + latest->normalized + " does not include " + expectedAssetName + ".";
    return result;
}

CheckResult ParseGitHubLatestReleaseBody(std::string_view body, std::string_view currentVersion) {
    json parsed = json::parse(body, nullptr, false);
    if (parsed.is_discarded()) {
        CheckResult result;
        result.status = CheckStatus::InvalidResponse;
        result.currentVersion = std::string(currentVersion);
        result.message = "GitHub returned invalid JSON.";
        return result;
    }
    try {
        return ParseGitHubLatestRelease(parsed, currentVersion);
    } catch (const std::exception& e) {
        CheckResult result;
        result.status = CheckStatus::InvalidResponse;
        result.currentVersion = std::string(currentVersion);
        result.message = std::string("Error parsing GitHub release: ") + e.what();
        return result;
    }
}

CheckResult CheckForUpdate() {
    CheckResult unsupported;
    unsupported.currentVersion = CurrentVersion();
    if (!IsMacArm64Supported()) {
        unsupported.status = CheckStatus::UnsupportedPlatform;
        unsupported.message = "Self-update is currently supported only on macOS arm64.";
        return unsupported;
    }

    long httpStatus = 0;
    std::string error;
    std::string body = HttpGet(LatestReleaseApiUrl(), &httpStatus, &error);
    if (!error.empty()) {
        CheckResult result;
        result.status = CheckStatus::NetworkError;
        result.currentVersion = CurrentVersion();
        result.httpStatus = httpStatus;
        result.message = error;
        return result;
    }
    CheckResult result = ParseGitHubLatestReleaseBody(body, CurrentVersion());
    result.httpStatus = httpStatus;
    return result;
}

DownloadResult DownloadReleaseAsset(const ReleaseInfo& release, ProgressCallback progress) {
    DownloadResult result;
    if (!release.hasCompatibleAsset || release.asset.browserDownloadUrl.empty()) {
        result.error = "release does not have a compatible downloadable asset";
        return result;
    }

    fs::path updateDir = UpdateRootDir(release.version);
    std::error_code ec;
    fs::create_directories(updateDir, ec);
    if (ec) {
        result.error = "could not create update directory: " + ec.message();
        return result;
    }

    fs::path finalPath = updateDir / release.asset.name;
    fs::path partPath = finalPath;
    partPath += ".part";
    fs::remove(partPath, ec);
    ec.clear();

    std::ofstream out(partPath, std::ios::binary);
    if (!out) {
        result.error = "could not write update download: " + partPath.string();
        return result;
    }

    CurlHandle curl;
    if (!curl.valid()) {
        result.error = "could not initialize libcurl";
        return result;
    }

    ProgressContext progressContext{std::move(progress)};
    std::string userAgent = "computer.cpp/" + CurrentVersion();
    curl_easy_setopt(curl.get(), CURLOPT_URL, release.asset.browserDownloadUrl.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, userAgent.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteFileCallback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &progressContext);

    CURLcode code = curl_easy_perform(curl.get());
    long httpStatus = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &httpStatus);
    out.close();

    if (code != CURLE_OK) {
        fs::remove(partPath, ec);
        result.error = curl_easy_strerror(code);
        return result;
    }
    if (httpStatus < 200 || httpStatus >= 300) {
        fs::remove(partPath, ec);
        result.error = "download returned HTTP " + std::to_string(httpStatus);
        return result;
    }
    if (release.asset.size > 0) {
        auto actualSize = fs::file_size(partPath, ec);
        if (ec || static_cast<int64_t>(actualSize) != release.asset.size) {
            fs::remove(partPath, ec);
            result.error = "downloaded file size did not match GitHub asset metadata";
            return result;
        }
    }

    fs::remove(finalPath, ec);
    ec.clear();
    fs::rename(partPath, finalPath, ec);
    if (ec) {
        result.error = "could not finalize update download: " + ec.message();
        return result;
    }

    result.ok = true;
    result.zipPath = finalPath;
    return result;
}

StageResult StageDownloadedUpdate(const ReleaseInfo& release, const fs::path& zipPath, bool verifySignature) {
    StageResult result;
    result.zipPath = zipPath;
    if (!IsMacArm64Supported()) {
        result.error = "self-update is currently supported only on macOS arm64";
        return result;
    }

    std::error_code ec;
    if (!fs::is_regular_file(zipPath, ec) || ec) {
        result.error = "downloaded update zip does not exist";
        return result;
    }

    fs::path updateDir = UpdateRootDir(release.version);
    fs::path extractDir = updateDir / "staging";
    fs::remove_all(extractDir, ec);
    if (ec) {
        result.error = "could not clear update staging directory: " + ec.message();
        return result;
    }
    fs::create_directories(extractDir, ec);
    if (ec) {
        result.error = "could not create update staging directory: " + ec.message();
        return result;
    }

    std::string command = "/usr/bin/ditto -x -k " + ShellQuote(zipPath.string()) + " " +
        ShellQuote(extractDir.string()) + " >/dev/null 2>&1";
    if (!RunCommand(command)) {
        result.error = "could not extract downloaded update";
        return result;
    }

    fs::path expectedRoot = extractDir / ("computer.cpp-" + release.version + "-macos-arm64");
    if (!fs::is_directory(expectedRoot, ec) || ec) {
        expectedRoot.clear();
        ec.clear();
        for (const auto& entry : fs::directory_iterator(extractDir, ec)) {
            if (ec) {
                break;
            }
            std::error_code entryEc;
            if (entry.is_directory(entryEc) && !entryEc &&
                fs::is_directory(entry.path() / "ComputerCpp.app", entryEc) && !entryEc &&
                fs::is_regular_file(entry.path() / "computer.cpp", entryEc) && !entryEc) {
                expectedRoot = entry.path();
                break;
            }
        }
    }
    if (expectedRoot.empty()) {
        result.error = "downloaded update did not contain the expected release directory";
        return result;
    }

    return ValidateStagedUpdate(release, zipPath, expectedRoot, verifySignature);
}

InstallResult LaunchInstallAndRelaunch(const StageResult& staged, int currentPid) {
    InstallResult result;
    result.zipPath = staged.zipPath;
    if (!staged.ok) {
        result.error = staged.error.empty() ? "update was not staged successfully" : staged.error;
        return result;
    }

    fs::path targetApp = CurrentAppBundlePath();
    if (targetApp.empty()) {
        result.error = "could not determine current app bundle path";
        return result;
    }
    fs::path targetParent = targetApp.parent_path();
    if (!IsWritableDirectory(targetParent)) {
        result.manualInstallRequired = true;
        result.error = "ComputerCpp is installed in a location that is not writable by this user.";
        return result;
    }
    fs::path targetCli = CurrentSiblingCliPath(targetApp);

    std::string script = BuildInstallHelperScript(currentPid, staged.appBundlePath, staged.cliPath, targetApp, targetCli);
    fs::path helperPath = UpdateRootDir(staged.rootDir.filename().string()) / "install-update.sh";
    if (staged.zipPath.has_parent_path()) {
        helperPath = staged.zipPath.parent_path() / "install-update.sh";
    }

    std::ofstream out(helperPath);
    if (!out) {
        result.error = "could not write update installer helper";
        return result;
    }
    out << script;
    out.close();

#if defined(__APPLE__)
    chmod(helperPath.c_str(), 0700);
#endif
    fs::path logPath = helperPath.parent_path() / "install-update.log";
    std::string launch = "/bin/sh " + ShellQuote(helperPath.string()) +
        " >" + ShellQuote(logPath.string()) + " 2>&1 &";
    if (!RunCommand(launch)) {
        result.error = "could not launch update installer helper";
        return result;
    }

    result.ok = true;
    result.helperPath = helperPath;
    result.logPath = logPath;
    return result;
}

fs::path CurrentAppBundlePath() {
#if defined(__APPLE__)
    fs::path executablePath(CurrentExecutablePath());
    std::string path = executablePath.string();
    size_t marker = path.find(".app/");
    if (marker == std::string::npos) {
        return {};
    }
    return fs::path(path.substr(0, marker + 4));
#else
    return {};
#endif
}

fs::path CurrentSiblingCliPath(const fs::path& appBundlePath) {
    fs::path cli = appBundlePath.parent_path() / "computer.cpp";
    std::error_code ec;
    return fs::is_regular_file(cli, ec) && !ec ? cli : fs::path();
}

bool IsMacArm64Supported() {
#if defined(__APPLE__) && defined(__aarch64__)
    return true;
#else
    return false;
#endif
}

bool RevealInFinder(const fs::path& path) {
#if defined(__APPLE__)
    if (path.empty()) {
        return false;
    }
    return RunCommand("/usr/bin/open -R " + ShellQuote(path.string()) + " >/dev/null 2>&1");
#else
    (void)path;
    return false;
#endif
}

std::string BuildInstallHelperScript(
    int currentPid,
    const fs::path& stagedApp,
    const fs::path& stagedCli,
    const fs::path& targetApp,
    const fs::path& targetCli) {
    std::ostringstream script;
    fs::path backupRoot = AppDataDir() / "updates" / "backup";
    fs::path backupApp = backupRoot / "ComputerCpp.app";
    fs::path backupCli = backupRoot / "computer.cpp";

    script << "#!/bin/sh\n";
    script << "set -u\n";
    script << "pid=" << currentPid << "\n";
    script << "staged_app=" << ShellQuote(stagedApp.string()) << "\n";
    script << "staged_cli=" << ShellQuote(stagedCli.string()) << "\n";
    script << "target_app=" << ShellQuote(targetApp.string()) << "\n";
    script << "target_cli=" << ShellQuote(targetCli.string()) << "\n";
    script << "backup_root=" << ShellQuote(backupRoot.string()) << "\n";
    script << "backup_app=" << ShellQuote(backupApp.string()) << "\n";
    script << "backup_cli=" << ShellQuote(backupCli.string()) << "\n";
    script << "echo \"waiting for ComputerCpp pid $pid to exit\"\n";
    script << "wait_count=0\n";
    script << "while kill -0 \"$pid\" 2>/dev/null && [ \"$wait_count\" -lt 50 ]; do wait_count=$((wait_count + 1)); sleep 0.2; done\n";
    script << "if kill -0 \"$pid\" 2>/dev/null; then\n";
    script << "  echo \"requesting old app shutdown\"\n";
    script << "  kill \"$pid\" 2>/dev/null || true\n";
    script << "  wait_count=0\n";
    script << "  while kill -0 \"$pid\" 2>/dev/null && [ \"$wait_count\" -lt 25 ]; do wait_count=$((wait_count + 1)); sleep 0.2; done\n";
    script << "fi\n";
    script << "if kill -0 \"$pid\" 2>/dev/null; then\n";
    script << "  echo \"forcing old app shutdown\"\n";
    script << "  kill -9 \"$pid\" 2>/dev/null || true\n";
    script << "  wait_count=0\n";
    script << "  while kill -0 \"$pid\" 2>/dev/null && [ \"$wait_count\" -lt 10 ]; do wait_count=$((wait_count + 1)); sleep 0.2; done\n";
    script << "fi\n";
    script << "if kill -0 \"$pid\" 2>/dev/null; then echo \"old app did not exit\"; exit 1; fi\n";
    script << "echo \"installing update to $target_app\"\n";
    script << "rm -rf \"$backup_root\"\n";
    script << "mkdir -p \"$backup_root\"\n";
    script << "if [ -d \"$target_app\" ]; then /usr/bin/ditto \"$target_app\" \"$backup_app\" || exit 1; fi\n";
    script << "if [ -n \"$target_cli\" ] && [ -f \"$target_cli\" ]; then /usr/bin/ditto \"$target_cli\" \"$backup_cli\" || exit 1; fi\n";
    script << "rm -rf \"$target_app\"\n";
    script << "if ! /usr/bin/ditto \"$staged_app\" \"$target_app\"; then\n";
    script << "  rm -rf \"$target_app\"\n";
    script << "  if [ -d \"$backup_app\" ]; then /usr/bin/ditto \"$backup_app\" \"$target_app\"; fi\n";
    script << "  exit 1\n";
    script << "fi\n";
    script << "if [ -n \"$target_cli\" ]; then\n";
    script << "  rm -f \"$target_cli\"\n";
    script << "  if ! /usr/bin/ditto \"$staged_cli\" \"$target_cli\"; then\n";
    script << "    rm -rf \"$target_app\"\n";
    script << "    if [ -d \"$backup_app\" ]; then /usr/bin/ditto \"$backup_app\" \"$target_app\"; fi\n";
    script << "    if [ -f \"$backup_cli\" ]; then /usr/bin/ditto \"$backup_cli\" \"$target_cli\"; fi\n";
    script << "    exit 1\n";
    script << "  fi\n";
    script << "fi\n";
    script << "echo \"relaunching $target_app\"\n";
    script << "/usr/bin/open -n \"$target_app\"\n";
    return script.str();
}

} // namespace ComputerCpp::Updater
