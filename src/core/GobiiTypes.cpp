#include "computer_cpp/GobiiTypes.h"

#include <ctime>
#include <cctype>
#include <iomanip>
#include <sstream>

#if defined(COMPUTER_CPP_GOBII_UNSAFE_RELEASE_CONFIGURATION)
#error "Gobii development features are forbidden in release-like builds"
#endif

namespace ComputerCpp {

const char* GobiiConnectionStateName(GobiiConnectionState state) {
    switch (state) {
        case GobiiConnectionState::Disconnected: return "disconnected";
        case GobiiConnectionState::Pairing: return "pairing";
        case GobiiConnectionState::PairingPending: return "pairing_pending";
        case GobiiConnectionState::Connecting: return "connecting";
        case GobiiConnectionState::Connected: return "connected";
        case GobiiConnectionState::Paused: return "paused";
        case GobiiConnectionState::PermissionsRequired: return "permissions_required";
        case GobiiConnectionState::AuthenticationExpired: return "authentication_expired";
        case GobiiConnectionState::UpdateRequired: return "update_required";
        case GobiiConnectionState::Error: return "error";
    }
    return "error";
}

bool IsGobiiLoopbackUrl(
    std::string_view value,
    std::string_view scheme
) {
    const std::string prefix = std::string(scheme) + "://";
    if (!value.starts_with(prefix)) {
        return false;
    }
    std::string_view authority = value.substr(prefix.size());
    const size_t path = authority.find_first_of("/?#");
    if (path != std::string_view::npos) {
        authority = authority.substr(0, path);
    }
    if (authority.empty() ||
        authority.find('@') != std::string_view::npos) {
        return false;
    }
    std::string_view host;
    std::string_view port;
    if (authority.front() == '[') {
        const size_t close = authority.find(']');
        if (close == std::string_view::npos) {
            return false;
        }
        host = authority.substr(1, close - 1);
        const std::string_view remainder =
            authority.substr(close + 1);
        if (!remainder.empty()) {
            if (remainder.front() != ':') {
                return false;
            }
            port = remainder.substr(1);
        }
    } else {
        const size_t colon = authority.rfind(':');
        if (colon == std::string_view::npos) {
            host = authority;
        } else {
            host = authority.substr(0, colon);
            port = authority.substr(colon + 1);
        }
    }
    if (host != "127.0.0.1" &&
        host != "localhost" &&
        host != "::1") {
        return false;
    }
    if (!port.empty()) {
        for (const unsigned char ch : port) {
            if (!std::isdigit(ch)) {
                return false;
            }
        }
    } else if (authority.ends_with(':')) {
        return false;
    }
    return true;
}

bool IsGobiiEndpointUrlAllowed(
    std::string_view value,
    std::string_view secureScheme,
    std::string_view loopbackScheme
) {
    const std::string securePrefix =
        std::string(secureScheme) + "://";
    if (value.starts_with(securePrefix) &&
        value.size() > securePrefix.size()) {
        return true;
    }
    return IsGobiiLoopbackUrl(
        value, loopbackScheme);
}

std::optional<std::chrono::system_clock::time_point>
ParseGobiiTimestamp(const std::string& value) {
    if (value.size() < 20 || value[10] != 'T') {
        return std::nullopt;
    }
    std::tm parsed{};
    std::istringstream input(value.substr(0, 19));
    input >> std::get_time(&parsed, "%Y-%m-%dT%H:%M:%S");
    if (input.fail()) {
        return std::nullopt;
    }
    size_t suffixIndex = 19;
    if (suffixIndex < value.size() && value[suffixIndex] == '.') {
        const size_t fractionStart = ++suffixIndex;
        while (suffixIndex < value.size() &&
               std::isdigit(static_cast<unsigned char>(
                   value[suffixIndex]))) {
            ++suffixIndex;
        }
        if (suffixIndex == fractionStart) {
            return std::nullopt;
        }
    }
    int offsetSeconds = 0;
    if (suffixIndex < value.size() && value[suffixIndex] == 'Z') {
        if (suffixIndex + 1 != value.size()) {
            return std::nullopt;
        }
    } else {
        if (suffixIndex + 6 != value.size() ||
            (value[suffixIndex] != '+' &&
             value[suffixIndex] != '-') ||
            value[suffixIndex + 3] != ':' ||
            !std::isdigit(static_cast<unsigned char>(
                value[suffixIndex + 1])) ||
            !std::isdigit(static_cast<unsigned char>(
                value[suffixIndex + 2])) ||
            !std::isdigit(static_cast<unsigned char>(
                value[suffixIndex + 4])) ||
            !std::isdigit(static_cast<unsigned char>(
                value[suffixIndex + 5]))) {
            return std::nullopt;
        }
        const int hours =
            (value[suffixIndex + 1] - '0') * 10 +
            (value[suffixIndex + 2] - '0');
        const int minutes =
            (value[suffixIndex + 4] - '0') * 10 +
            (value[suffixIndex + 5] - '0');
        if (hours > 23 || minutes > 59) {
            return std::nullopt;
        }
        offsetSeconds = (hours * 60 + minutes) * 60;
        if (value[suffixIndex] == '-') {
            offsetSeconds = -offsetSeconds;
        }
    }
#if defined(_WIN32)
    const time_t utc = _mkgmtime(&parsed);
#else
    const time_t utc = timegm(&parsed);
#endif
    return std::chrono::system_clock::from_time_t(utc) -
        std::chrono::seconds(offsetSeconds);
}

} // namespace ComputerCpp
