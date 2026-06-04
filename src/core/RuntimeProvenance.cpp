#include "computer_cpp/RuntimeProvenance.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifndef COMPUTER_CPP_BUILD_GIT_SHA
#define COMPUTER_CPP_BUILD_GIT_SHA "unknown"
#endif

#ifndef COMPUTER_CPP_BUILD_TIMESTAMP
#define COMPUTER_CPP_BUILD_TIMESTAMP "unknown"
#endif

using json = nlohmann::json;

namespace ComputerCpp {

namespace {

constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

std::string Hex64(uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

std::string AbsolutePath(const std::string& path) {
    if (path.empty()) {
        return "";
    }
    std::error_code ec;
    auto absolute = std::filesystem::absolute(path, ec);
    if (ec) {
        return path;
    }
    auto canonical = std::filesystem::weakly_canonical(absolute, ec);
    return ec ? absolute.string() : canonical.string();
}

} // namespace

std::string CurrentExecutablePath() {
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        buffer.resize(std::char_traits<char>::length(buffer.c_str()));
        return AbsolutePath(buffer);
    }
    return "";
#elif defined(_WIN32)
    std::array<char, 32768> buffer{};
    DWORD length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size()) {
        return AbsolutePath(std::string(buffer.data(), length));
    }
    return "";
#else
    std::array<char, 4096> buffer{};
    ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length > 0) {
        return AbsolutePath(std::string(buffer.data(), static_cast<size_t>(length)));
    }
    return "";
#endif
}

std::string ExecutableFingerprint(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return "unavailable";
    }
    uint64_t hash = kFnvOffset;
    uint64_t size = 0;
    std::array<char, 65536> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        std::streamsize count = in.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<size_t>(i)]);
            hash *= kFnvPrime;
        }
        size += static_cast<uint64_t>(std::max<std::streamsize>(0, count));
    }
    if (size == 0) {
        return "unavailable";
    }
    return "fnv1a64:" + Hex64(hash) + ":size:" + std::to_string(size);
}

RuntimeProvenance RuntimeProvenanceForExecutable(const std::string& path) {
    RuntimeProvenance provenance;
#if defined(_WIN32)
    provenance.pid = static_cast<int>(GetCurrentProcessId());
#else
    provenance.pid = static_cast<int>(getpid());
#endif
    provenance.executablePath = AbsolutePath(path);
    provenance.executableFingerprint = ExecutableFingerprint(provenance.executablePath);
    provenance.buildGitSha = COMPUTER_CPP_BUILD_GIT_SHA;
    provenance.buildTimestamp = COMPUTER_CPP_BUILD_TIMESTAMP;
    provenance.featureMapSchemaVersion = kFeatureMapSchemaVersion;
    return provenance;
}

RuntimeProvenance CurrentRuntimeProvenance() {
    return RuntimeProvenanceForExecutable(CurrentExecutablePath());
}

json RuntimeProvenanceToJson(const RuntimeProvenance& provenance) {
    return {
        {"pid", provenance.pid},
        {"executablePath", provenance.executablePath},
        {"executableFingerprint", provenance.executableFingerprint},
        {"buildGitSha", provenance.buildGitSha},
        {"buildTimestamp", provenance.buildTimestamp},
        {"featureMapSchemaVersion", provenance.featureMapSchemaVersion}
    };
}

}
