#include "computer_cpp/GobiiPairingClient.h"
#include "computer_cpp/GobiiTypes.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <limits>

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
    bool requireAgent,
    GobiiTokenResponse& token,
    std::string& error
) {
    if (!value.is_object() ||
        !RequiredString(value, "device_id", token.deviceId, error) ||
        !RequiredString(
            value,
            "refresh_token",
            token.deviceRefreshToken,
            error) ||
        !RequiredString(
            value,
            "access_token",
            token.relayAccessToken,
            error) ||
        !RequiredString(value, "relay_url", token.relayUrl, error)) {
        return false;
    }
    if (!value.contains("expires_in") ||
        !value["expires_in"].is_number_integer()) {
        error = "response field 'expires_in' must be an integer";
        return false;
    }
    const auto expiresIn = value["expires_in"].get<long long>();
    if (expiresIn <= 0 ||
        expiresIn > std::chrono::seconds::max().count()) {
        error = "response field 'expires_in' must be positive";
        return false;
    }
    if (!value.contains("token_type") ||
        !value["token_type"].is_string() ||
        value["token_type"].get<std::string>() != "Bearer") {
        error = "response field 'token_type' must be 'Bearer'";
        return false;
    }
    token.relayAccessTokenExpiresAt =
        std::chrono::system_clock::now() +
        std::chrono::seconds(expiresIn);
    if (!IsGobiiEndpointUrlAllowed(
            token.relayUrl, "wss", "ws")) {
        error = "relay_url must use wss";
        return false;
    }
    if (requireAgent &&
        !RequiredString(value, "agent_id", token.agentId, error)) {
        return false;
    }
    return true;
}

bool IsUuid(const std::string& value) {
    if (value.size() != 36) return false;
    for (size_t i = 0; i < value.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (value[i] != '-') return false;
            continue;
        }
        const unsigned char ch =
            static_cast<unsigned char>(value[i]);
        if (!std::isxdigit(ch)) return false;
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
        error =
            response.status < 200 || response.status >= 300
            ? "Gobii request returned HTTP " +
                std::to_string(response.status)
            : "Gobii returned malformed JSON";
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

std::string GobiiPairingClient::Endpoint(
    const std::string& path
) const {
    return baseUrl_ + path;
}

bool GobiiPairingClient::CreatePairing(
    const GobiiPairingRequest& request,
    GobiiPairingSession& session,
    std::string& error
) {
    error.clear();
    json apps = json::array();
    for (const auto& app : request.apps) {
        apps.push_back({
            {"key", app.key},
            {"display_name", app.displayName},
            {"schema_sha256", app.schemaSha256},
            {"type", app.type},
        });
    }
    json body = {
        {"machine_id", request.machineId},
        {"display_name", request.deviceName},
        {"platform", request.platform},
        {"architecture", request.architecture},
        {"client_version", request.clientVersion},
        {"protocol_version", 1},
        {"apps", std::move(apps)},
    };
    json value;
    const GobiiHttpResponse response = transport_->Send(JsonRequest(
        Endpoint("/api/computer/v1/pairings/"),
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
        !value.contains("expires_at") ||
        !value["expires_at"].is_string() ||
        !value.contains("interval") ||
        !value["interval"].is_number_integer()) {
        if (error.empty()) {
            error = "pairing response is missing expiry or polling interval";
        }
        return false;
    }
    if (!IsUuid(session.pairingId)) {
        error = "response field 'pairing_id' must be a UUID";
        return false;
    }
    const auto expiresAt =
        ParseGobiiTimestamp(value["expires_at"].get<std::string>());
    if (!expiresAt) {
        error = "response field 'expires_at' must be a timestamp";
        return false;
    }
    const auto remaining =
        std::chrono::duration_cast<std::chrono::seconds>(
            *expiresAt - std::chrono::system_clock::now());
    if (remaining <= std::chrono::seconds::zero() ||
        remaining.count() > std::numeric_limits<int>::max()) {
        error = "pairing response contains an invalid expiry";
        return false;
    }
    session.expiresInSeconds =
        static_cast<int>(remaining.count());
    session.intervalSeconds = value["interval"].get<int>();
    if (session.expiresInSeconds <= 0 ||
        session.intervalSeconds <= 0 ||
        session.intervalSeconds > 60) {
        error = "pairing response contains invalid timing values";
        return false;
    }
    if (!IsGobiiEndpointUrlAllowed(
            session.verificationUriComplete,
            "https",
            "http")) {
        error = "verification_uri_complete must use https";
        return false;
    }
    return true;
}

GobiiPairingPollResult GobiiPairingClient::Poll(
    const GobiiPairingSession& session
) {
    GobiiPairingPollResult result;
    if (!IsUuid(session.pairingId)) {
        result.error = "pairing_id must be a UUID";
        return result;
    }
    const GobiiHttpResponse response = transport_->Send(JsonRequest(
        Endpoint(
            "/api/computer/v1/pairings/" +
            session.pairingId +
            "/exchange/"),
        {
            {"device_code", session.deviceCode},
        }));
    json value;
    if (!ParseObject(response, value, result.error)) {
        return result;
    }
    if (response.status == 200) {
        if (ParseToken(value, true, result.token, result.error)) {
            result.state = GobiiPairingPollState::Approved;
        }
        return result;
    }
    const std::string code = value.value("error", "");
    if (code == "authorization_pending") {
        result.state = GobiiPairingPollState::Pending;
    } else if (code == "slow_down") {
        result.state = GobiiPairingPollState::SlowDown;
        if (value.contains("interval") &&
            value["interval"].is_number_integer()) {
            result.intervalSeconds =
                value["interval"].get<int>();
        }
    } else if (code == "access_denied") {
        result.state = GobiiPairingPollState::AccessDenied;
    } else if (code == "expired" ||
               code == "expired_token") {
        result.state = GobiiPairingPollState::Expired;
    } else {
        result.error = value.value("error_description", "pairing failed");
    }
    return result;
}

bool GobiiPairingClient::Refresh(
    const std::string& refreshToken,
    const std::string& clientVersion,
    GobiiTokenResponse& token,
    std::string& error
) {
    error.clear();
    const GobiiHttpResponse response = transport_->Send(JsonRequest(
        Endpoint("/api/computer/v1/tokens/refresh/"),
        {
            {"refresh_token", refreshToken},
            {"client_version", clientVersion},
            {"protocol_version", 1},
        }));
    json value;
    if (!ParseObject(response, value, error)) {
        return false;
    }
    if (response.status != 200) {
        error = value.value(
            "error_description",
            value.value(
                "error",
                "Gobii token refresh failed"));
        return false;
    }
    return ParseToken(value, false, token, error);
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
