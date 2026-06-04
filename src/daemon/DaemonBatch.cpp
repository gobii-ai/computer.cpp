#include "DaemonBatch.h"

#include "computer_cpp/StringUtils.h"

#include "DaemonParsing.h"
#include "DaemonProtocol.h"

#include <utility>

namespace ComputerCpp {
namespace {

bool IsBlockedBatchMethod(const std::string& method) {
    return method == "batch" || method == "shutdown";
}

bool ShouldPropagateBatchControl(const std::string& method) {
    return method.rfind("control_session_", 0) != 0;
}

nlohmann::json StepParamsWithBatchControl(
    const nlohmann::json& step,
    const std::string& method,
    const std::string& controlSession,
    const std::string& controlScope) {
    nlohmann::json params = step.contains("params") ? step.at("params") : nlohmann::json::object();
    if (!ShouldPropagateBatchControl(method)) {
        return params;
    }
    if (!controlSession.empty() &&
        !params.contains("controlSession") &&
        !params.contains("controlSessionToken")) {
        params["controlSession"] = controlSession;
    }
    if (!controlScope.empty() && !params.contains("controlScope")) {
        params["controlScope"] = controlScope;
    }
    return params;
}

std::string BatchStepResultId(const nlohmann::json& step, const std::string& fallbackStepId) {
    if (!step.is_object() || !step.contains("id") || !step.at("id").is_string()) {
        return fallbackStepId;
    }
    std::string id = step.at("id").get<std::string>();
    return IsBlank(id) ? fallbackStepId : id;
}

} // namespace

nlohmann::json RunBatchCommand(
    const std::string& session,
    const nlohmann::json& request,
    const nlohmann::json& params,
    const DaemonRequestDispatcher& dispatch) {
    if (auto unknown = UnknownParam(params, {
        "steps", "stopOnError", "controlSession", "controlSessionToken", "controlScope"
    })) {
        return Error("unknown batch parameter: " + *unknown, "invalid_batch");
    }

    nlohmann::json steps = params.value("steps", nlohmann::json::array());
    if (!steps.is_array()) {
        return Error("batch requires params.steps array", "invalid_batch");
    }
    auto stopOnError = BoolParam(params, "stopOnError", true);
    if (!stopOnError) {
        return Error("batch requires boolean stopOnError", "invalid_batch");
    }

    const std::string controlSession = ControlSessionTokenFromRequest(request, params);
    const std::string controlScope = ControlScopeFromParams(params);
    nlohmann::json results = nlohmann::json::array();
    size_t failedCount = 0;
    bool stoppedOnError = false;

    const auto appendResult = [&](nlohmann::json result, const std::string& id) {
        result["id"] = id;
        if (!result.value("ok", false)) {
            ++failedCount;
        }
        results.push_back(std::move(result));
    };

    for (size_t i = 0; i < steps.size(); ++i) {
        const std::string fallbackStepId = "step-" + std::to_string(i + 1);
        const nlohmann::json& step = steps[i];
        if (!step.is_object()) {
            appendResult(Error("batch step must be an object", "invalid_batch_step"), fallbackStepId);
            if (*stopOnError) {
                stoppedOnError = true;
                break;
            }
            continue;
        }
        const std::string resultStepId = BatchStepResultId(step, fallbackStepId);
        if (auto unknown = UnknownParam(step, {"id", "method", "params"})) {
            appendResult(Error("unknown batch step parameter: " + *unknown, "invalid_batch_step"), resultStepId);
            if (*stopOnError) {
                stoppedOnError = true;
                break;
            }
            continue;
        }

        auto stepMethodParam = StringParam(step, "method", "");
        auto stepIdParam = StringParam(step, "id", fallbackStepId);
        if (!stepMethodParam || !stepIdParam) {
            appendResult(Error("batch step requires string id and method", "invalid_batch_step"), resultStepId);
            if (*stopOnError) {
                stoppedOnError = true;
                break;
            }
            continue;
        }
        if (IsBlank(*stepMethodParam) || IsBlank(*stepIdParam)) {
            appendResult(Error("batch step requires non-empty id and method", "invalid_batch_step"), resultStepId);
            if (*stopOnError) {
                stoppedOnError = true;
                break;
            }
            continue;
        }
        if (step.contains("params") && !step.at("params").is_object()) {
            appendResult(Error("batch step params must be an object", "invalid_batch_step"), *stepIdParam);
            if (*stopOnError) {
                stoppedOnError = true;
                break;
            }
            continue;
        }

        const std::string stepMethod = *stepMethodParam;
        if (IsBlockedBatchMethod(stepMethod)) {
            appendResult(Error("batch step method is not allowed: " + stepMethod, "invalid_batch_step"), *stepIdParam);
            if (*stopOnError) {
                stoppedOnError = true;
                break;
            }
            continue;
        }

        nlohmann::json stepRequest = {
            {"id", *stepIdParam},
            {"method", stepMethod},
            {"params", StepParamsWithBatchControl(step, stepMethod, controlSession, controlScope)}
        };
        appendResult(dispatch(session, stepRequest), *stepIdParam);
        if (!results.back().value("ok", false) && *stopOnError) {
            stoppedOnError = true;
            break;
        }
    }

    return Ok({
        {"results", results},
        {"requested", steps.size()},
        {"executed", results.size()},
        {"failed", failedCount},
        {"stoppedOnError", stoppedOnError}
    });
}

} // namespace ComputerCpp
