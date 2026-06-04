#include "CliOutput.h"

#include <cstdint>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace ComputerCpp::Cli {
namespace {

void PrintStateCompact(const json& data) {
    std::cout << "Session: " << data.value("session", "default") << "\n";
    auto app = data.value("frontmostApp", json::object());
    if (app.value("available", false)) {
        std::cout << "Frontmost: " << app.value("name", "") << " [" << app.value("bundleId", "") << "]"
                  << " pid=" << app.value("pid", -1) << "\n";
    } else {
        std::cout << "Frontmost: unknown\n";
    }
    auto perms = data.value("permissions", json::object());
    std::cout << "Permissions: accessibility=" << (perms.value("accessibility", false) ? "yes" : "no")
              << " screen_capture=" << (perms.value("screenCapture", false) ? "yes" : "no") << "\n";
    auto screen = data.value("screen", json::object());
    std::cout << "Screen: " << screen.value("width", 0) << "x" << screen.value("height", 0) << "\n";
    auto cursor = data.value("cursor", json::object());
    std::cout << "Cursor: " << cursor.value("x", 0.0) << "," << cursor.value("y", 0.0) << "\n";
}

} // namespace

void PrintCompact(const std::string& method, const json& response) {
    if (!response.value("ok", false)) {
        std::cerr << "Error: " << response.value("error", "unknown error") << "\n";
        return;
    }
    json data = response.value("data", json::object());
    if (method == "snapshot") {
        std::cout << data.value("text", "");
        return;
    }
    if (method == "state") {
        PrintStateCompact(data);
        return;
    }
    if (method == "permissions") {
        std::cout << "accessibility=" << (data.value("accessibility", false) ? "yes" : "no")
                  << " screen_capture=" << (data.value("screenCapture", false) ? "yes" : "no") << "\n";
        return;
    }
    if (method == "screenshot") {
        std::cout << data.value("path", "") << "\n";
        return;
    }
    if (method == "image_info") {
        std::cout << data.value("path", "") << "\n";
        std::cout << "size " << data.value("width", 0) << "x" << data.value("height", 0) << "\n";
        return;
    }
    if (method == "image_split") {
        std::cout << data.value("path", "") << "\n";
        std::cout << "chunks " << data.value("chunks", json::array()).size()
                  << "  chunk_height " << data.value("chunkHeight", 0)
                  << "  overlap " << data.value("overlap", 0) << "\n";
        return;
    }
    if (method == "clipboard_read") {
        std::cout << data.value("text", "") << "\n";
        return;
    }
    if (method.rfind("control_session_", 0) == 0) {
        auto session = data.value("session", json::object());
        if (method == "control_session_status" && data.value("available", false) && session.empty()) {
            std::cout << "available\n";
            return;
        }
        if (session.empty()) {
            std::cout << "ok\n";
            return;
        }
        std::cout << "state=" << session.value("state", "") << " scope=" << session.value("scope", "")
                  << " owner=" << session.value("owner", "")
                  << " expires_at_ms=" << session.value("expiresAtMs", static_cast<int64_t>(0)) << "\n";
        if (session.contains("token")) {
            std::cout << "token=" << session.value("token", "") << "\n";
        }
        return;
    }
    if (data.empty()) {
        std::cout << "ok\n";
    } else {
        std::cout << data.dump(2) << "\n";
    }
}

json ControlSessionCliError(const ControlSessionResult& result) {
    json response = {
        {"ok", false},
        {"error", result.error.empty() ? "control session error" : result.error},
        {"code", result.code.empty() ? "control_session_error" : result.code},
    };
    json data = json::object();
    if (!result.record.token.empty()) {
        data["session"] = ControlSessionRecordToJson(result.record, false);
    }
    if (result.holder.has_value()) {
        data["holder"] = ControlSessionRecordToJson(*result.holder, false);
    }
    if (!data.empty()) {
        response["data"] = data;
    }
    return response;
}

json ControlSessionCliOk(const ControlSessionResult& result, bool includeToken) {
    json data = json::object();
    if (!result.record.scope.empty()) {
        data["session"] = ControlSessionRecordToJson(result.record, includeToken);
        data["available"] = result.record.state != "active";
    } else {
        data["available"] = true;
    }
    if (result.holder.has_value()) {
        data["holder"] = ControlSessionRecordToJson(*result.holder, false);
    }
    if (result.released) {
        data["released"] = true;
    }
    return {{"ok", true}, {"data", data}};
}

int PrintSessionCliResult(
    const CliOptions& options,
    const std::string& method,
    const ControlSessionResult& result,
    bool includeToken
) {
    json response = result.ok ? ControlSessionCliOk(result, includeToken) : ControlSessionCliError(result);
    if (options.jsonOutput) {
        std::cout << response.dump(2) << "\n";
    } else {
        PrintCompact(method, response);
    }
    return result.ok ? 0 : 1;
}

} // namespace ComputerCpp::Cli
