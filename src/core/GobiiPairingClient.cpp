#include "computer_cpp/GobiiPairingClient.h"
#include "computer_cpp/GobiiTypes.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace ComputerCpp {
namespace {

bool RequiredString(
    const json& value,
    const char* name,
    std::string& output,
    std::string& error
) {
    if (!value.contains(name) || !value[name].is_string() ||
        value[name].get_ref<const std::string&>().empty()) {
        error = std::string("response field '") + name +
            "' must be a non-empty string";
        return false;
    }
    output = value[name].get<std::string>();
    return true;
}

bool ParseToken(
    const json& value,
    GobiiTokenResponse& token,
    std::string& error
) {
    if (!value.is_object() ||
        !RequiredString(value, "device_id", token.deviceId, error) ||
        !RequiredString(
            value,
            "device_refresh_token",
            token.deviceRefreshToken,
            error) ||
        !RequiredString(
            value,
            "relay_access_token",
            token.relayAccessToken,
            error) ||
        !RequiredString(value, "relay_url", token.relayUrl, error)) {
        return false;
    }
    std::string expiresAt;
    if (!RequiredString(
            value,
            "relay_access_token_expires_at",
            expiresAt,
            error)) {
        return false;
    }
    const auto parsed = ParseGobiiTimestamp(expiresAt);
    if (!parsed) {
        error = "relay_access_token_expires_at is not a valid timestamp";
        return false;
    }
    token.relayAccessTokenExpiresAt = *parsed;
    if (token.relayAccessTokenExpiresAt <=
        std::chrono::system_clock::now()) {
        error = "relay access token is already expired";
        return false;
    }
    if (token.relayUrl.rfind("wss://", 0) != 0) {
#if defined(COMPUTER_CPP_GOBII_DEV_INLINE_IMAGES)
        if (token.relayUrl.rfind("ws://", 0) != 0) {
            error = "relay_url must use wss";
            return false;
        }
#else
        error = "relay_url must use wss";
        return false;
#endif
    }
    if (!value.contains("agent") || !value["agent"].is_object() ||
        !RequiredString(value["agent"], "id", token.agentId, error) ||
        !RequiredString(value["agent"], "name", token.agentName, error)) {
        return false;
    }
    return true;
}

GobiiHttpRequest JsonRequest(
    std::string url,
    const json& body,
    const std::string& bearer = {}
) {
    GobiiHttpRequest request;
    request.url = std::move(url);
    request.headers["Content-Type"] = "application/json";
    request.headers["Accept"] = "application/json";
    if (!bearer.empty()) {
        request.headers["Authorization"] = "Bearer " + bearer;
    }
    request.body = body.dump();
    return request;
}

bool ParseObject(
    const GobiiHttpResponse& response,
    json& value,
    std::string& error
) {
    if (!response.error.empty()) {
        error = "Gobii request failed: " + response.error;
        return false;
    }
    value = json::parse(response.body, nullptr, false);
    if (value.is_discarded() || !value.is_object()) {
        error = "Gobii returned malformed JSON";
        return false;
    }
    return true;
}

} // namespace

GobiiPairingClient::GobiiPairingClient(
    std::string baseUrl,
    std::shared_ptr<GobiiHttpTransport> transport
) : baseUrl_(std::move(baseUrl)), transport_(std::move(transport)) {
    while (!baseUrl_.empty() && baseUrl_.back() == '/') {
        baseUrl_.pop_back();
    }
}

std::string GobiiPairingClient::Endpoint(const char* path) const {
    return baseUrl_ + path;
}

bool GobiiPairingClient::CreatePairing(
    const GobiiPairingRequest& request,
    GobiiPairingSession& session,
    std::string& error
) {
    error.clear();
    json body = {
        {"device_name", request.deviceName},
        {"platform", request.platform},
        {"architecture", request.architecture},
        {"client_version", request.clientVersion},
        {"relay_protocol_version", 1},
        {"capabilities", {
            "mcp", "catalog_updates", "pause"
        }},
    };
    json value;
    const GobiiHttpResponse response = transport_->Send(JsonRequest(
        Endpoint("/api/computers/pairings/"),
        body));
    if (!ParseObject(response, value, error)) {
        return false;
    }
    if (response.status != 200 && response.status != 201) {
        error = value.value("detail", "Gobii rejected the pairing request");
        return false;
    }
    if (!RequiredString(value, "pairing_id", session.pairingId, error) ||
        !RequiredString(value, "device_code", session.deviceCode, error) ||
        !RequiredString(value, "user_code", session.userCode, error) ||
        !RequiredString(
            value,
            "verification_uri",
            session.verificationUri,
            error) ||
        !RequiredString(
            value,
            "verification_uri_complete",
            session.verificationUriComplete,
            error) ||
        !value.contains("expires_in") ||
        !value["expires_in"].is_number_integer() ||
        !value.contains("interval") ||
        !value["interval"].is_number_integer()) {
        if (error.empty()) {
            error = "pairing response is missing expiry or polling interval";
        }
        return false;
    }
    session.expiresInSeconds = value["expires_in"].get<int>();
    session.intervalSeconds = value["interval"].get<int>();
    if (session.expiresInSeconds <= 0 ||
        session.intervalSeconds <= 0 ||
        session.intervalSeconds > 60) {
        error = "pairing response contains invalid timing values";
        return false;
    }
    if (session.verificationUriComplete.rfind("https://", 0) != 0) {
        error = "verification_uri_complete must use https";
        return false;
    }
    return true;
}

GobiiPairingPollResult GobiiPairingClient::Poll(
    const GobiiPairingSession& session
) {
    GobiiPairingPollResult result;
    const GobiiHttpResponse response = transport_->Send(JsonRequest(
        Endpoint("/api/computers/pairings/token/"),
        {
            {"pairing_id", session.pairingId},
            {"device_code", session.deviceCode},
        }));
    json value;
    if (!ParseObject(response, value, result.error)) {
        return result;
    }
    if (response.status == 200) {
        if (ParseToken(value, result.token, result.error)) {
            result.state = GobiiPairingPollState::Approved;
        }
        return result;
    }
    const std::string code = value.value("error", "");
    if (code == "authorization_pending") {
        result.state = GobiiPairingPollState::Pending;
    } else if (code == "slow_down") {
        result.state = GobiiPairingPollState::SlowDown;
    } else if (code == "access_denied") {
        result.state = GobiiPairingPollState::AccessDenied;
    } else if (code == "expired_token") {
        result.state = GobiiPairingPollState::Expired;
    } else {
        result.error = value.value("error_description", "pairing failed");
    }
    return result;
}

bool GobiiPairingClient::Refresh(
    const std::string& refreshToken,
    GobiiTokenResponse& token,
    std::string& error
) {
    error.clear();
    const GobiiHttpResponse response = transport_->Send(JsonRequest(
        Endpoint("/api/computers/token/refresh/"),
        json::object(),
        refreshToken));
    json value;
    if (!ParseObject(response, value, error)) {
        return false;
    }
    if (response.status != 200) {
        error = value.value("detail", "Gobii token refresh failed");
        return false;
    }
    return ParseToken(value, token, error);
}

bool GobiiPairingClient::Revoke(
    const std::string& refreshToken,
    std::string& error
) {
    error.clear();
    const GobiiHttpResponse response = transport_->Send(JsonRequest(
        Endpoint("/api/computers/revoke/"),
        json::object(),
        refreshToken));
    if (!response.error.empty()) {
        error = "Gobii revoke failed: " + response.error;
        return false;
    }
    if (response.status < 200 || response.status >= 300) {
        error = "Gobii rejected the revoke request";
        return false;
    }
    return true;
}

} // namespace ComputerCpp
