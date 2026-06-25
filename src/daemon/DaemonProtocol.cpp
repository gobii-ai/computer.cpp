#include "DaemonProtocol.h"

#include "computer_cpp/StringUtils.h"

#include "DaemonParsing.h"

#include <optional>
#include <set>
#include <utility>

namespace ComputerCpp {

json Error(const std::string& message, const std::string& code) {
    return {
        {"ok", false},
        {"error", message},
        {"code", code},
    };
}

json ErrorWithData(const std::string& message, const std::string& code, json data) {
    return {
        {"ok", false},
        {"error", message},
        {"code", code},
        {"data", std::move(data)},
    };
}

json Ok(json data) {
    return {
        {"ok", true},
        {"data", std::move(data)},
    };
}

bool MethodRequiresControlSession(const std::string& method, const json& params) {
    static const std::set<std::string> alwaysAllowed = {
        "ping",
        "capabilities",
        "schema",
        "batch",
        "browser_eval",
        "shutdown",
        "control_session_acquire",
        "control_session_resume",
        "control_session_renew",
        "control_session_release",
        "control_session_status",
        "control_session_metrics",
        "control_session_events",
    };
    if (alwaysAllowed.count(method) > 0) {
        return false;
    }
    if (method == "permissions" && (!params.contains("request") || !params["request"].is_boolean() || !params["request"].get<bool>())) {
        return false;
    }
    return true;
}

std::string ControlSessionTokenFromRequest(const json& request, const json& params) {
    for (const char* key : {"controlSession", "controlSessionToken"}) {
        if (params.contains(key) && params[key].is_string()) {
            return params[key].get<std::string>();
        }
        if (request.contains(key) && request[key].is_string()) {
            return request[key].get<std::string>();
        }
    }
    return "";
}

std::optional<std::string> InvalidControlSessionTokenError(const json& request, const json& params) {
    for (const char* key : {"controlSession", "controlSessionToken"}) {
        if (params.contains(key)) {
            if (!params[key].is_string()) {
                return "control session token must be a string when provided";
            }
            if (IsBlank(params[key].get<std::string>())) {
                return "control session token must be non-empty when provided";
            }
        }
        if (request.contains(key)) {
            if (!request[key].is_string()) {
                return "control session token must be a string when provided";
            }
            if (IsBlank(request[key].get<std::string>())) {
                return "control session token must be non-empty when provided";
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> InvalidControlScopeError(const json& params) {
    if (!params.contains("controlScope")) {
        return std::nullopt;
    }
    if (!params["controlScope"].is_string()) {
        return "control scope must be a string when provided";
    }
    if (IsBlank(params["controlScope"].get<std::string>())) {
        return "control scope must be non-empty when provided";
    }
    return std::nullopt;
}

std::string ControlScopeFromParams(const json& params) {
    auto scope = StringParam(params, "controlScope", std::string(kDefaultControlScope));
    return scope.value_or(std::string(kDefaultControlScope));
}

json ControlSessionError(const ControlSessionResult& result) {
    json error = Error(
        result.error.empty() ? "control session error" : result.error,
        result.code.empty() ? "control_session_error" : result.code);
    json data = json::object();
    if (!result.record.token.empty()) {
        data["session"] = ControlSessionRecordToJson(result.record, false);
    }
    if (result.holder.has_value()) {
        data["holder"] = ControlSessionRecordToJson(*result.holder, false);
    }
    if (!data.empty()) {
        error["data"] = data;
    }
    return error;
}

json ControlSessionResultOk(const ControlSessionResult& result, bool includeToken) {
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
    return Ok(data);
}

json RequireControlSessionForRequest(const json& request, const std::string& method, const json& params) {
    if (!MethodRequiresControlSession(method, params)) {
        return Ok();
    }
    if (auto tokenError = InvalidControlSessionTokenError(request, params)) {
        return Error(*tokenError, "invalid_control_session");
    }
    if (auto scopeError = InvalidControlScopeError(params)) {
        return Error(*scopeError, "invalid_control_session");
    }
    auto check = ValidateControlSession(ControlSessionTokenFromRequest(request, params), ControlScopeFromParams(params));
    if (!check.ok) {
        return ControlSessionError(check);
    }
    return Ok({{"session", ControlSessionRecordToJson(check.record, false)}});
}

json TimelineParamsWithControlSession(const json& params, const json& controlGate) {
    json annotated = params;
    if (controlGate.contains("data") && controlGate["data"].contains("session")) {
        annotated["controlSession"] = controlGate["data"]["session"];
    }
    return annotated;
}

} // namespace ComputerCpp
