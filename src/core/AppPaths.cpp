#include "computer_cpp/AppPaths.h"

#include <cstdlib>
#include <stdexcept>
#include <string>

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

}
