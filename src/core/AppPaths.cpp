#include "computer_cpp/AppPaths.h"

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace ComputerCpp {

namespace {

fs::path HomeDir() {
    if (const char* home = std::getenv("HOME")) {
        if (*home != '\0') {
            return fs::path(home);
        }
    }
    return fs::temp_directory_path();
}

bool HasHomeOverride() {
    const char* overrideDir = std::getenv("COMPUTER_CPP_HOME");
    return overrideDir != nullptr && *overrideDir != '\0';
}

[[maybe_unused]] fs::path EnvDir(const char* name) {
    if (const char* value = std::getenv(name)) {
        if (*value != '\0') {
            return fs::path(value);
        }
    }
    return {};
}

void EnsurePrivateDirectory(const fs::path& path) {
    std::error_code ec;
#if defined(__unix__) || defined(__APPLE__)
    bool existed = fs::exists(path, ec);
#endif
    EnsureDirectory(path);
#if defined(__unix__) || defined(__APPLE__)
    if (!existed) {
        fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace, ec);
    }
#endif
}

fs::path CurrentExecutablePath() {
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string value(size, '\0');
    if (_NSGetExecutablePath(value.data(), &size) == 0) {
        return fs::path(value.c_str());
    }
#elif defined(_WIN32)
    std::wstring value(32768, L'\0');
    DWORD size = GetModuleFileNameW(
        nullptr,
        value.data(),
        static_cast<DWORD>(value.size()));
    if (size > 0 && size < value.size()) {
        value.resize(size);
        return fs::path(value);
    }
#elif defined(__linux__)
    std::vector<char> value(4096, '\0');
    const ssize_t size = ::readlink(
        "/proc/self/exe",
        value.data(),
        value.size() - 1);
    if (size > 0) {
        return fs::path(std::string(value.data(), static_cast<size_t>(size)));
    }
#endif
    return {};
}

}

void EnsureDirectory(const fs::path& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec) {
        throw std::runtime_error("failed to create directory " + path.string() + ": " + ec.message());
    }
}

fs::path AppDataDir() {
    if (const char* overrideDir = std::getenv("COMPUTER_CPP_HOME")) {
        if (*overrideDir != '\0') {
            fs::path path(overrideDir);
            EnsureDirectory(path);
            return path;
        }
    }

    fs::path path = HomeDir() / ".computer.cpp";
    EnsurePrivateDirectory(path);
    return path;
}

fs::path AppLogPath() {
    return AppDataDir() / "computer.cpp.log";
}

fs::path ConfigDir() {
    if (HasHomeOverride()) {
        return AppDataDir();
    }

#ifdef __APPLE__
    fs::path path = HomeDir() / "Library" / "Application Support" / "computer.cpp";
#elif defined(_WIN32)
    fs::path root = EnvDir("LOCALAPPDATA");
    if (root.empty()) {
        root = EnvDir("APPDATA");
    }
    if (root.empty()) {
        root = HomeDir() / "AppData" / "Local";
    }
    fs::path path = root / "ComputerCpp";
#else
    fs::path root = EnvDir("XDG_CONFIG_HOME");
    if (root.empty() || root.is_relative()) {
        root = HomeDir() / ".config";
    }
    fs::path path = root / "computer.cpp";
#endif
    EnsurePrivateDirectory(path);
    return path;
}

fs::path ConfigPath() {
    return ConfigDir() / "config.toml";
}

fs::path RecordingDir() {
    fs::path path = AppDataDir() / "recordings";
    EnsurePrivateDirectory(path);
    return path;
}

fs::path SessionDir(const std::string& session) {
    fs::path path = AppDataDir() / "sessions" / session;
    EnsureDirectory(path);
    return path;
}

fs::path RefStorePath(const std::string& session) {
    return SessionDir(session) / "refs.json";
}

fs::path DefaultArtifactDir() {
    fs::path path = AppDataDir() / "artifacts";
    EnsureDirectory(path);
    return path;
}

fs::path TimelineDir(const std::string& session) {
    fs::path path = SessionDir(session) / "timeline";
    EnsureDirectory(path);
    EnsureDirectory(path / "frames");
    return path;
}

fs::path TimelineDbPath(const std::string& session) {
    return TimelineDir(session) / "timeline.sqlite";
}

fs::path InstalledResourcePath(const fs::path& relative) {
    std::vector<fs::path> roots;
    if (const char* overrideDir =
            std::getenv("COMPUTER_CPP_RESOURCE_DIR")) {
        if (*overrideDir != '\0') {
            roots.emplace_back(overrideDir);
        }
    }

    const fs::path executable = CurrentExecutablePath();
    if (!executable.empty()) {
        const fs::path executableDir = executable.parent_path();
#if defined(__APPLE__)
        roots.push_back(executableDir.parent_path() / "Resources");
        roots.push_back(
            executableDir / "ComputerCpp.app" / "Contents" / "Resources");
#elif defined(_WIN32)
        roots.push_back(executableDir / "share" / "computer.cpp");
        roots.push_back(executableDir.parent_path() / "share" / "computer.cpp");
#else
        roots.push_back(executableDir.parent_path() / "share" / "computer.cpp");
#endif
    }
#ifdef COMPUTER_CPP_SOURCE_DIR
    roots.emplace_back(
        fs::path(COMPUTER_CPP_SOURCE_DIR) / "resources");
#endif

    for (const auto& root : roots) {
        const fs::path candidate = root / relative;
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec) && !ec) {
            return candidate;
        }
    }
    return {};
}

}
