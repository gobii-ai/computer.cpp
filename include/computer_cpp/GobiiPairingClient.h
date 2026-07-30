#pragma once

#include "computer_cpp/GobiiApiClient.h"

#include <chrono>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>

namespace ComputerCpp {

struct GobiiPairingRequest {
    std::string deviceName;
    std::string platform;
    std::string architecture;
    std::string clientVersion;
};

struct GobiiPairingSession {
    std::string pairingId;
    std::string deviceCode;
    std::string userCode;
    std::string verificationUri;
    std::string verificationUriComplete;
    int expiresInSeconds = 0;
    int intervalSeconds = 0;
};

struct GobiiTokenResponse {
    std::string deviceId;
    std::string deviceRefreshToken;
    std::string relayAccessToken;
    std::chrono::system_clock::time_point relayAccessTokenExpiresAt;
    std::string relayUrl;
    std::string agentId;
    std::string agentName;
};

enum class GobiiPairingPollState {
    Approved,
    Pending,
    SlowDown,
    AccessDenied,
    Expired,
    Error,
};

struct GobiiPairingPollResult {
    GobiiPairingPollState state = GobiiPairingPollState::Error;
    GobiiTokenResponse token;
    std::string error;
};

class GobiiPairingClient {
public:
    GobiiPairingClient(
        std::string baseUrl,
        std::shared_ptr<GobiiHttpTransport> transport);

    bool CreatePairing(
        const GobiiPairingRequest& request,
        GobiiPairingSession& session,
        std::string& error);
    GobiiPairingPollResult Poll(const GobiiPairingSession& session);
    bool Refresh(
        const std::string& refreshToken,
        GobiiTokenResponse& token,
        std::string& error);
    bool Revoke(const std::string& refreshToken, std::string& error);

private:
    std::string Endpoint(const char* path) const;

    std::string baseUrl_;
    std::shared_ptr<GobiiHttpTransport> transport_;
};

} // namespace ComputerCpp
