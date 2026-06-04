#include "DaemonControlSession.h"

#include "computer_cpp/ControlSession.h"
#include "computer_cpp/StringUtils.h"

#include "DaemonParsing.h"
#include "DaemonProtocol.h"

namespace ComputerCpp {

bool IsControlSessionCommand(const std::string& method) {
    return method == "control_session_acquire" ||
        method == "control_session_resume" ||
        method == "control_session_renew" ||
        method == "control_session_release" ||
        method == "control_session_status" ||
        method == "control_session_metrics" ||
        method == "control_session_events";
}

nlohmann::json RunControlSessionCommand(const std::string& session, const std::string& method, const nlohmann::json& params) {
    if (method == "control_session_acquire" || method == "control_session_resume") {
        if (auto unknown = UnknownParam(params, {"scope", "daemonSession", "owner", "purpose", "ttlMs", "waitMs", "maxRuntimeMs"})) {
            return Error("unknown control session acquire parameter: " + *unknown, "invalid_control_session");
        }
        auto ttlMs = Int64Param(params, "ttlMs", static_cast<int64_t>(10 * 60 * 1000));
        auto waitMs = Int64Param(params, "waitMs", static_cast<int64_t>(0));
        auto maxRuntimeMs = Int64Param(params, "maxRuntimeMs", static_cast<int64_t>(0));
        if (!ttlMs || !waitMs || !maxRuntimeMs) {
            return Error("control session acquire requires integer ttlMs, waitMs, and maxRuntimeMs", "invalid_control_session");
        }
        auto scope = StringParam(params, "scope", std::string(kDefaultControlScope));
        auto daemonSession = StringParam(params, "daemonSession", session);
        auto owner = StringParam(params, "owner", "");
        auto purpose = StringParam(params, "purpose", "");
        if (!scope || !daemonSession || !owner || !purpose) {
            return Error("control session acquire requires string scope, daemonSession, owner, and purpose", "invalid_control_session");
        }
        if (IsBlank(*scope) || IsBlank(*daemonSession)) {
            return Error("control session acquire requires non-empty scope and daemonSession", "invalid_control_session");
        }
        ControlSessionAcquireOptions options;
        options.scope = *scope;
        options.daemonSession = *daemonSession;
        options.owner = *owner;
        options.purpose = *purpose;
        options.ttlMs = *ttlMs;
        options.waitMs = *waitMs;
        options.maxRuntimeMs = *maxRuntimeMs;
        auto result = method == "control_session_resume"
            ? AcquireOrResumeControlSession(options)
            : AcquireControlSession(options);
        return result.ok ? ControlSessionResultOk(result, true) : ControlSessionError(result);
    }

    if (method == "control_session_renew") {
        if (auto unknown = UnknownParam(params, {"token", "ttlMs"})) {
            return Error("unknown control session renew parameter: " + *unknown, "invalid_control_session");
        }
        auto ttlMs = Int64Param(params, "ttlMs", static_cast<int64_t>(10 * 60 * 1000));
        if (!ttlMs) {
            return Error("control session renew requires integer ttlMs", "invalid_control_session");
        }
        auto token = StringParam(params, "token", "");
        if (!token) {
            return Error("control session renew requires string token", "invalid_control_session");
        }
        if (IsBlank(*token)) {
            return Error("control session renew token must be non-empty", "invalid_control_session");
        }
        auto result = RenewControlSession(*token, *ttlMs);
        return result.ok ? ControlSessionResultOk(result, true) : ControlSessionError(result);
    }

    if (method == "control_session_release") {
        if (auto unknown = UnknownParam(params, {"token"})) {
            return Error("unknown control session release parameter: " + *unknown, "invalid_control_session");
        }
        auto token = StringParam(params, "token", "");
        if (!token) {
            return Error("control session release requires string token", "invalid_control_session");
        }
        if (IsBlank(*token)) {
            return Error("control session release token must be non-empty", "invalid_control_session");
        }
        auto result = ReleaseControlSession(*token);
        return result.ok ? ControlSessionResultOk(result, true) : ControlSessionError(result);
    }

    if (method == "control_session_status") {
        if (auto unknown = UnknownParam(params, {"scope", "token"})) {
            return Error("unknown control session status parameter: " + *unknown, "invalid_control_session");
        }
        auto scope = StringParam(params, "scope", std::string(kDefaultControlScope));
        auto token = StringParam(params, "token", "");
        if (!scope || !token) {
            return Error("control session status requires string scope and token", "invalid_control_session");
        }
        if (IsBlank(*scope)) {
            return Error("control session status scope must be non-empty", "invalid_control_session");
        }
        if (params.contains("token") && IsBlank(*token)) {
            return Error("control session status token must be non-empty when provided", "invalid_control_session");
        }
        auto result = GetControlSessionStatus(*scope, *token);
        return result.ok ? ControlSessionResultOk(result, false) : ControlSessionError(result);
    }

    if (method == "control_session_metrics") {
        if (auto unknown = UnknownParam(params, {"scope", "staleAfterMs", "longRunningAfterMs", "eventLimit"})) {
            return Error("unknown control session metrics parameter: " + *unknown, "invalid_control_session");
        }
        auto staleAfterMs = Int64Param(params, "staleAfterMs", static_cast<int64_t>(60 * 1000));
        auto longRunningAfterMs = Int64Param(params, "longRunningAfterMs", static_cast<int64_t>(30 * 60 * 1000));
        auto eventLimit = IntParam(params, "eventLimit", 20);
        if (!staleAfterMs || !longRunningAfterMs || !eventLimit) {
            return Error("control session metrics requires integer staleAfterMs, longRunningAfterMs, and eventLimit", "invalid_control_session");
        }
        if (*staleAfterMs < 0 || *longRunningAfterMs < 0) {
            return Error("control session metrics thresholds must be non-negative", "invalid_control_session");
        }
        if (*eventLimit <= 0) {
            return Error("control session metrics eventLimit must be positive", "invalid_control_session");
        }
        auto scope = StringParam(params, "scope", std::string(kDefaultControlScope));
        if (!scope) {
            return Error("control session metrics requires string scope", "invalid_control_session");
        }
        if (IsBlank(*scope)) {
            return Error("control session metrics scope must be non-empty", "invalid_control_session");
        }
        return Ok(ControlSessionMetricsJson(
            *scope,
            *staleAfterMs,
            *longRunningAfterMs,
            *eventLimit
        ));
    }

    if (method == "control_session_events") {
        if (auto unknown = UnknownParam(params, {"scope", "limit"})) {
            return Error("unknown control session events parameter: " + *unknown, "invalid_control_session");
        }
        auto limit = IntParam(params, "limit", 50);
        if (!limit) {
            return Error("control session events requires integer limit", "invalid_control_session");
        }
        if (*limit <= 0) {
            return Error("control session events limit must be positive", "invalid_control_session");
        }
        auto scope = StringParam(params, "scope", std::string(kDefaultControlScope));
        if (!scope) {
            return Error("control session events requires string scope", "invalid_control_session");
        }
        if (IsBlank(*scope)) {
            return Error("control session events scope must be non-empty", "invalid_control_session");
        }
        nlohmann::json events = nlohmann::json::array();
        for (const auto& event : RecentControlSessionEvents(
            *scope,
            *limit
        )) {
            events.push_back(ControlSessionEventToJson(event));
        }
        return Ok({{"events", events}});
    }

    return Error("unknown control session method: " + method, "unknown_method");
}

}
