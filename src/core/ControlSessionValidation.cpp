#include "computer_cpp/ControlSession.h"

#include <algorithm>
#include <cctype>

namespace ComputerCpp {
namespace {

constexpr int64_t kMinTtlMs = 1000;
constexpr int64_t kDefaultTtlMs = 10 * 60 * 1000;
constexpr int64_t kMaxTtlMs = 8 * 60 * 60 * 1000;
constexpr int64_t kMaxWaitMs = 60 * 60 * 1000;
constexpr int64_t kMaxRuntimeCapMs = 24 * 60 * 60 * 1000;

} // namespace

bool IsControlSessionScopeValid(const std::string& scope) {
    if (scope.empty() || scope.size() > 120) {
        return false;
    }
    for (unsigned char ch : scope) {
        bool ok = std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.' || ch == ':' || ch == '/';
        if (!ok) {
            return false;
        }
    }
    return scope != "." && scope != "..";
}

int64_t ClampControlSessionTtlMs(int64_t ttlMs) {
    if (ttlMs <= 0) {
        ttlMs = kDefaultTtlMs;
    }
    return std::clamp(ttlMs, kMinTtlMs, kMaxTtlMs);
}

int64_t ClampControlSessionWaitMs(int64_t waitMs) {
    return std::clamp(waitMs, static_cast<int64_t>(0), kMaxWaitMs);
}

int64_t ClampControlSessionMaxRuntimeMs(int64_t maxRuntimeMs) {
    if (maxRuntimeMs <= 0) {
        return 0;
    }
    return std::clamp(maxRuntimeMs, kMinTtlMs, kMaxRuntimeCapMs);
}

} // namespace ComputerCpp
