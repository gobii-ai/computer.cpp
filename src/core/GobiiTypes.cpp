#include "computer_cpp/GobiiTypes.h"

#include <ctime>
#include <iomanip>
#include <sstream>

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
    const std::string suffix = value.substr(19);
    if (suffix != "Z") {
        if (suffix.size() < 3 ||
            suffix.front() != '.' ||
            suffix.back() != 'Z') {
            return std::nullopt;
        }
        for (size_t index = 1; index + 1 < suffix.size(); ++index) {
            if (suffix[index] < '0' || suffix[index] > '9') {
                return std::nullopt;
            }
        }
    }
#if defined(_WIN32)
    const time_t utc = _mkgmtime(&parsed);
#else
    const time_t utc = timegm(&parsed);
#endif
    if (utc < 0) {
        return std::nullopt;
    }
    return std::chrono::system_clock::from_time_t(utc);
}

} // namespace ComputerCpp
