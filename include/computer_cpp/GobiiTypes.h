#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace ComputerCpp {

enum class GobiiConnectionState {
    Disconnected,
    Pairing,
    PairingPending,
    Connecting,
    Connected,
    Paused,
    PermissionsRequired,
    AuthenticationExpired,
    UpdateRequired,
    Error,
};

struct GobiiConnectionStatus {
    GobiiConnectionState state = GobiiConnectionState::Disconnected;
    std::string deviceId;
    std::string deviceName;
    std::string agentId;
    std::string pairingCode;
    std::optional<std::chrono::system_clock::time_point> lastConnectedAt;
    std::optional<std::chrono::system_clock::time_point> lastHeartbeatAt;
    std::string lastError;
    std::string installedVersion;
    std::string requiredVersion;
    std::string currentOperationName;
    std::string currentRequestId;
    int reconnectAttempt = 0;
};

const char* GobiiConnectionStateName(GobiiConnectionState state);
bool IsGobiiLoopbackUrl(
    std::string_view value,
    std::string_view scheme);
bool IsGobiiEndpointUrlAllowed(
    std::string_view value,
    std::string_view secureScheme,
    std::string_view localDevelopmentScheme);
std::optional<std::chrono::system_clock::time_point>
ParseGobiiTimestamp(const std::string& value);

} // namespace ComputerCpp
