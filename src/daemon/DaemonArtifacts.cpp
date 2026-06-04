#include "DaemonArtifacts.h"

#include <chrono>
#include <ctime>

namespace ComputerCpp {

std::string TimestampForPath() {
    auto now = std::chrono::system_clock::now();
    auto secs = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &secs);
#else
    localtime_r(&secs, &tm);
#endif
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &tm);
    return buffer;
}

}
