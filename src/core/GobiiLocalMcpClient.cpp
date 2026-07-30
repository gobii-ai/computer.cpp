#include "computer_cpp/GobiiLocalMcpClient.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <nlohmann/json.hpp>

namespace ComputerCpp {
namespace {

bool ValidAppName(const std::string& value) {
    if (value.empty() || value.size() > 100) return false;
    for (unsigned char ch : value) {
        if (!(std::isalnum(ch) || ch == '-' || ch == '_' ||
              ch == '.')) {
            return false;
        }
    }
    return true;
}

std::string SanitizeError(const std::string& error) {
    if (error.empty()) return "local MCP request failed";
    if (error.find('/') != std::string::npos ||
        error.find('\\') != std::string::npos) {
        return "local MCP request failed";
    }
    return error.substr(0, 300);
}

} // namespace

GobiiLocalMcpClient::GobiiLocalMcpClient(
    int port,
    std::string bearerToken,
    std::set<std::string> knownApps,
    std::shared_ptr<GobiiHttpTransport> transport
) : port_(port),
    bearerToken_(std::move(bearerToken)),
    knownApps_(std::move(knownApps)),
    transport_(std::move(transport)) {}

GobiiLocalMcpResult GobiiLocalMcpClient::Forward(
    const std::string& app,
    const nlohmann::json& payload,
    std::chrono::system_clock::time_point deadline
) {
    GobiiLocalMcpResult result;
    if (!ValidAppName(app) || !knownApps_.contains(app)) {
        result.code = "unknown_app";
        result.error = "requested app is not configured";
        return result;
    }
    if (!payload.is_object() || payload.value("jsonrpc", "") != "2.0") {
        result.code = "invalid_request";
        result.error = "payload must be a JSON-RPC 2.0 object";
        return result;
    }
    const std::string body = payload.dump();
    if (body.size() > 512 * 1024) {
        result.code = "payload_too_large";
        result.error = "local MCP request exceeded size limit";
        return result;
    }
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::system_clock::now());
    if (remaining <= std::chrono::milliseconds::zero()) {
        result.code = "deadline_exceeded";
        result.error = "request deadline has expired";
        return result;
    }
    GobiiHttpRequest request;
    request.url = "http://127.0.0.1:" +
        std::to_string(port_) + "/apps/" + app + "/mcp";
    request.headers = {
        {"Authorization", "Bearer " + bearerToken_},
        {"Content-Type", "application/json"},
        {"Accept", "application/json, text/event-stream"},
        {"MCP-Protocol-Version", "2025-11-25"},
        {"X-ComputerCpp-Control-Queue", "reject"},
        {"X-ComputerCpp-Deadline-Ms",
            std::to_string(remaining.count())},
    };
    request.body = body;
    request.timeoutMs =
        std::max<long>(1, static_cast<long>(remaining.count()));
    request.responseLimit = 8 * 1024 * 1024;
    const GobiiHttpResponse response = transport_->Send(request);
    if (!response.error.empty()) {
        result.code = response.error.find("timed out") !=
                std::string::npos
            ? "deadline_exceeded"
            : "local_server_unavailable";
        result.error = SanitizeError(response.error);
        return result;
    }
    if (response.status != 200) {
        result.code = response.status == 404
            ? "unknown_app"
            : "local_server_unavailable";
        result.error = "local MCP server rejected the request";
        return result;
    }
    const auto parsed =
        nlohmann::json::parse(response.body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        result.code = "internal_error";
        result.error = "local MCP server returned malformed JSON";
        return result;
    }
    result.ok = true;
    result.response = response.body;
    return result;
}

} // namespace ComputerCpp
