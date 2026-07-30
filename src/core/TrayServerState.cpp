#include "computer_cpp/TrayServerState.h"

#include "computer_cpp/AppPaths.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <signal.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ComputerCpp {

std::filesystem::path TrayAppServerStatePath() {
    return AppDataDir() / "tray-app-server.json";
}

std::filesystem::path TrayAppServerStateDirectory() {
    return AppDataDir() / "tray-app-servers";
}

namespace {

uint64_t Fnv1a64(const std::string& value) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : value) {
        hash ^= static_cast<uint64_t>(ch);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string SafeStateName(const std::string& configName) {
    std::string prefix;
    prefix.reserve(std::min<size_t>(configName.size(), 48));
    for (unsigned char ch : configName) {
        if (prefix.size() >= 48) {
            break;
        }
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_') {
            prefix.push_back(static_cast<char>(ch));
        } else if (ch == '.' || ch == ' ') {
            prefix.push_back('-');
        }
    }
    if (prefix.empty()) {
        prefix = "app";
    }
    std::ostringstream suffix;
    suffix << std::hex << std::nouppercase << Fnv1a64(configName);
    return prefix + "-" + suffix.str() + ".json";
}

json TrayStateToJson(const TrayAppServerState& state) {
    return {
        {"version", state.version},
        {"configured", state.configured},
        {"pid", state.pid},
        {"host", state.host},
        {"port", state.port},
        {"url", state.url},
        {"appPath", state.appPath},
        {"appId", state.appId},
        {"configName", state.configName},
        {"displayName", state.displayName},
        {"startedAt", state.startedAt},
    };
}

std::optional<TrayAppServerState> TrayStateFromJson(const json& value) {
    if (!value.is_object()) {
        return std::nullopt;
    }
    TrayAppServerState state;
    state.version = value.value("version", 1);
    state.configured = value.value("configured", false);
    state.pid = value.value("pid", 0L);
    state.host = value.value("host", "");
    state.port = value.value("port", 0);
    state.url = value.value("url", "");
    state.appPath = value.value("appPath", "");
    state.appId = value.value("appId", "");
    state.configName = value.value("configName", "");
    state.displayName = value.value("displayName", "");
    state.startedAt = value.value("startedAt", "");
    if (state.pid <= 0 || state.host.empty() || state.port <= 0 ||
        state.url.empty() || (!state.configured && state.appPath.empty())) {
        return std::nullopt;
    }
    return state;
}

} // namespace

std::filesystem::path TrayAppServerStatePath(const std::string& configName) {
    return TrayAppServerStateDirectory() / SafeStateName(configName);
}

std::vector<fs::path> ListTrayAppServerStatePaths(std::string* error) {
    std::vector<fs::path> paths;
    std::error_code ec;
    const fs::path directory = TrayAppServerStateDirectory();
    if (!fs::exists(directory, ec)) {
        if (ec && error) {
            *error = "could not inspect tray server state directory: " + ec.message();
        }
        return paths;
    }
    fs::directory_iterator end;
    for (fs::directory_iterator it(directory, ec); !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && !ec && it->path().extension() == ".json") {
            paths.push_back(it->path());
        }
    }
    if (ec) {
        if (error) {
            *error = "could not list tray server state directory: " + ec.message();
        }
        std::sort(paths.begin(), paths.end());
        return paths;
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

bool SaveTrayAppServerState(const TrayAppServerState& state, const fs::path& path, std::string* error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        if (error) {
            *error = "could not create tray server state directory: " + ec.message();
        }
        return false;
    }

    fs::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out) {
            if (error) {
                *error = "could not write tray server state file: " + temp.string();
            }
            return false;
        }
        out << TrayStateToJson(state).dump(2) << "\n";
    }
    fs::rename(temp, path, ec);
    if (ec) {
        fs::remove(path, ec);
        ec.clear();
        fs::rename(temp, path, ec);
    }
    if (ec) {
        if (error) {
            *error = "could not replace tray server state file: " + ec.message();
        }
        return false;
    }
    return true;
}

std::optional<TrayAppServerState> LoadTrayAppServerState(const fs::path& path, std::string* error) {
    std::ifstream in(path);
    if (!in) {
        if (error) {
            *error = "tray server state file not found";
        }
        return std::nullopt;
    }
    json parsed = json::parse(in, nullptr, false);
    if (parsed.is_discarded()) {
        if (error) {
            *error = "tray server state file contains invalid JSON";
        }
        return std::nullopt;
    }
    auto state = TrayStateFromJson(parsed);
    if (!state && error) {
        *error = "tray server state file is incomplete";
    }
    return state;
}

bool RemoveTrayAppServerState(const fs::path& path, std::string* error) {
    std::error_code ec;
    fs::remove(path, ec);
    if (ec) {
        if (error) {
            *error = "could not remove tray server state file: " + ec.message();
        }
        return false;
    }
    return true;
}

bool RemoveTrayAppServerStateForPid(const fs::path& path, long pid, std::string* error) {
    std::string loadError;
    auto state = LoadTrayAppServerState(path, &loadError);
    if (!state) {
        return true;
    }
    if (state->pid != pid) {
        return true;
    }
    return RemoveTrayAppServerState(path, error);
}

bool IsProcessAlive(long pid) {
    if (pid <= 0) {
        return false;
    }
#if defined(__unix__) || defined(__APPLE__)
    if (::kill(static_cast<pid_t>(pid), 0) == 0) {
        return true;
    }
    return errno == EPERM;
#elif defined(_WIN32)
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (hProcess == NULL) {
        return false;
    }
    DWORD exitCode = 0;
    BOOL ok = GetExitCodeProcess(hProcess, &exitCode);
    CloseHandle(hProcess);
    return ok && exitCode == STILL_ACTIVE;
#else
    return false;
#endif
}

} // namespace ComputerCpp
