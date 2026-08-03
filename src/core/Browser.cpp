#include "computer_cpp/Browser.h"

#include "computer_cpp/AppPaths.h"
#include "computer_cpp/StringUtils.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace ComputerCpp {
namespace {

fs::path HomeDir() {
    if (const char* home = std::getenv("HOME")) {
        if (*home) return fs::path(home);
    }
    return fs::temp_directory_path();
}

bool IsExecutableFile(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec;
}

[[maybe_unused]] std::string FindOnPath(const std::vector<std::string>& names) {
    const char* raw = std::getenv("PATH");
    if (!raw) return {};
#if defined(_WIN32)
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    std::string pathValue(raw);
    size_t start = 0;
    while (start <= pathValue.size()) {
        size_t end = pathValue.find(separator, start);
        std::string directory = pathValue.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        for (const auto& name : names) {
            fs::path candidate = fs::path(directory) / name;
            if (IsExecutableFile(candidate)) return candidate.string();
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return {};
}

fs::path BrowserStorageRoot() {
    if (const char* overrideDir = std::getenv("COMPUTER_CPP_HOME")) {
        if (*overrideDir) return AppDataDir();
    }
#if defined(_WIN32)
    if (const char* local = std::getenv("LOCALAPPDATA")) {
        return fs::path(local) / "computer.cpp";
    }
#elif defined(__APPLE__)
    return HomeDir() / "Library" / "Application Support" / "computer.cpp";
#else
    if (const char* state = std::getenv("XDG_STATE_HOME")) {
        if (*state) return fs::path(state) / "computer.cpp";
    }
    return HomeDir() / ".local" / "state" / "computer.cpp";
#endif
    return AppDataDir();
}

void MakePrivate(const fs::path& path) {
    EnsureDirectory(path);
#if defined(__unix__) || defined(__APPLE__)
    std::error_code ec;
    fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace, ec);
#endif
}

BrowserDescriptor BuildDescriptor(const std::string& id) {
    BrowserDescriptor out;
    out.id = id;
    out.recommended = id == "chrome";
#if defined(__APPLE__)
    if (id == "chrome") {
        out.displayName = "Google Chrome";
        out.applicationName = "Google Chrome";
    } else if (id == "edge") {
        out.displayName = "Microsoft Edge";
        out.applicationName = "Microsoft Edge";
    } else if (id == "brave") {
        out.displayName = "Brave Browser";
        out.applicationName = "Brave Browser";
    } else if (id == "chromium") {
        out.displayName = "Chromium";
        out.applicationName = "Chromium";
    }
    for (const auto& root : {fs::path("/Applications"), HomeDir() / "Applications"}) {
        fs::path bundle = root / (out.applicationName + ".app");
        std::error_code ec;
        if (fs::is_directory(bundle, ec) && !ec) {
            out.executable = bundle.string();
            out.installed = true;
            break;
        }
    }
#elif defined(_WIN32)
    std::vector<fs::path> relative;
    std::vector<std::string> names;
    if (id == "chrome") {
        out.displayName = "Google Chrome"; out.applicationName = "Google Chrome";
        relative = {"Google/Chrome/Application/chrome.exe"}; names = {"chrome.exe"};
    } else if (id == "edge") {
        out.displayName = "Microsoft Edge"; out.applicationName = "Microsoft Edge";
        relative = {"Microsoft/Edge/Application/msedge.exe"}; names = {"msedge.exe"};
    } else if (id == "brave") {
        out.displayName = "Brave Browser"; out.applicationName = "Brave Browser";
        relative = {"BraveSoftware/Brave-Browser/Application/brave.exe"}; names = {"brave.exe"};
    } else if (id == "chromium") {
        out.displayName = "Chromium"; out.applicationName = "Chromium";
        relative = {"Chromium/Application/chrome.exe"}; names = {"chromium.exe"};
    }
    for (const char* variable : {"PROGRAMFILES", "PROGRAMFILES(X86)", "LOCALAPPDATA"}) {
        if (const char* root = std::getenv(variable)) {
            for (const auto& suffix : relative) {
                fs::path candidate = fs::path(root) / suffix;
                if (IsExecutableFile(candidate)) out.executable = candidate.string();
            }
        }
        if (!out.executable.empty()) break;
    }
    if (out.executable.empty()) out.executable = FindOnPath(names);
    out.installed = !out.executable.empty();
#else
    std::vector<std::string> names;
    if (id == "chrome") {
        out.displayName = "Google Chrome"; out.applicationName = "Google Chrome";
        names = {"google-chrome", "google-chrome-stable"};
    } else if (id == "edge") {
        out.displayName = "Microsoft Edge"; out.applicationName = "Microsoft Edge";
        names = {"microsoft-edge", "microsoft-edge-stable"};
    } else if (id == "brave") {
        out.displayName = "Brave Browser"; out.applicationName = "Brave Browser";
        names = {"brave-browser", "brave"};
    } else if (id == "chromium") {
        out.displayName = "Chromium"; out.applicationName = "Chromium";
        names = {"chromium", "chromium-browser"};
    }
    out.executable = FindOnPath(names);
    out.installed = !out.executable.empty();
#endif
    return out;
}

} // namespace

std::string NormalizeBrowserId(const std::string& value) {
    const std::string lower = Lowercase(Trim(value));
    if (lower == "chrome" || lower.find("google chrome") != std::string::npos) return "chrome";
    if (lower == "edge" || lower.find("microsoft edge") != std::string::npos || lower.find("msedge") != std::string::npos) return "edge";
    if (lower == "brave" || lower.find("brave browser") != std::string::npos) return "brave";
    if (lower == "chromium" || lower.find("chromium") != std::string::npos) return "chromium";
    return lower;
}

BrowserDescriptor DescribeBrowser(const std::string& browserId) {
    return BuildDescriptor(NormalizeBrowserId(browserId));
}

std::vector<BrowserDescriptor> BrowserCatalog() {
    std::vector<BrowserDescriptor> out;
    for (const char* id : {"chrome", "edge", "brave", "chromium"}) {
        out.push_back(BuildDescriptor(id));
    }
    return out;
}

fs::path ManagedBrowserDataDir(const std::string& browserId, const std::string& profile) {
    const std::string id = NormalizeBrowserId(browserId);
    if (id == "chrome" && profile == "default") {
        if (const char* configured = std::getenv("COMPUTER_CPP_CHROME_USER_DATA_DIR")) {
            if (*configured) {
                fs::path path(configured);
                return path;
            }
        }
        fs::path legacy = BrowserStorageRoot() / "chrome-cdp";
        return legacy;
    }
    fs::path path = BrowserStorageRoot() / "browser-profiles" / id / profile;
    return path;
}

void PrepareManagedBrowserDataDir(const fs::path& path) {
    MakePrivate(path);
}

} // namespace ComputerCpp
