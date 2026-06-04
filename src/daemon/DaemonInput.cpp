#include "DaemonInput.h"

#include "computer_cpp/Platform.h"
#include "computer_cpp/StringUtils.h"

#include "DaemonClick.h"
#include "DaemonJson.h"
#include "DaemonObservedEvent.h"
#include "DaemonParsing.h"
#include "DaemonProtocol.h"
#include "DaemonTargetResolve.h"

#include <algorithm>
#include <string>
#include <utility>

namespace ComputerCpp {

namespace {

using json = nlohmann::json;

} // namespace

json RunClickCommand(const std::string& session, const json& params) {
    if (auto unknown = UnknownParam(params, {
        "target", "x", "y", "rect", "box", "bounds", "rectClickXFraction", "rectClickYFraction",
        "button", "motion", "clickMode", "hoverSafe", "clickCount", "durationMs", "steps",
        "clickDurationMs", "clickSteps", "preClickSettleMs", "clickHoldMs",
        "parkBeforeClick", "parkDurationMs", "parkSteps", "parkXFraction", "parkYFraction",
        "controlSession", "controlSessionToken", "controlScope"
    })) {
        return Error("unknown click parameter: " + *unknown, "invalid_click");
    }

    auto targetParam = StringParam(params, "target", "");
    auto rectClickXFraction = NumberParam(params, "rectClickXFraction", 0.5);
    auto rectClickYFraction = NumberParam(params, "rectClickYFraction", 0.5);
    if (!targetParam || !rectClickXFraction || !rectClickYFraction) {
        return Error("click requires string target and numeric rect click fractions", "invalid_click");
    }
    if (params.contains("target") && IsBlank(*targetParam)) {
        return Error("click target must be non-empty when provided", "invalid_click");
    }
    if (*rectClickXFraction < 0.05 || *rectClickXFraction > 0.95 ||
        *rectClickYFraction < 0.05 || *rectClickYFraction > 0.95) {
        return Error("click rect fractions must be between 0.05 and 0.95", "invalid_click");
    }
    auto target = ClickTargetFromParams(session, params);
    if (!target.has_value()) {
        return Error("could not resolve click target", "target_not_found");
    }
    auto buttonParam = StringParam(params, "button", "left");
    auto motionParam = StringParam(params, "motion", "natural");
    if (!buttonParam || !motionParam) {
        return Error("click requires string button/motion", "invalid_click");
    }
    std::string button = *buttonParam;
    std::string motion = *motionParam;
    if (IsBlank(button) || IsBlank(motion)) {
        return Error("click requires non-empty button/motion", "invalid_click");
    }
    auto clickCountParam = IntParam(params, "clickCount", 1);
    if (!clickCountParam) {
        return Error("click requires integer clickCount", "invalid_click");
    }
    if (*clickCountParam < 1 || *clickCountParam > 5) {
        return Error("click clickCount must be between 1 and 5", "invalid_click");
    }
    if (motion == "instant") {
        bool ok = Platform::Click(target->x, target->y, button, *clickCountParam);
        json data = {
            {"x", target->x},
            {"y", target->y},
            {"clicked", ok},
            {"motion", motion},
            {"targetKind", target->kind}
        };
        if (target->rect.has_value()) {
            data["rect"] = BoundsToJson(*target->rect);
        }
        return Ok(data);
    }
    auto clickOptions = ClickActivationOptionsFromParams(params);
    if (!clickOptions.valid) {
        return Error(clickOptions.error.empty() ? "invalid click activation options" : clickOptions.error, "invalid_click");
    }
    if (clickOptions.hoverSafe && clickOptions.parkBeforeClick) {
        int screenWidth = 0;
        int screenHeight = 0;
        Platform::GetScreenSize(screenWidth, screenHeight);
        auto parkXFractionParam = NumberParam(params, "parkXFraction", 0.82);
        auto parkYFractionParam = NumberParam(params, "parkYFraction", 0.50);
        if (!parkXFractionParam || !parkYFractionParam) {
            return Error("click requires numeric park fractions", "invalid_click");
        }
        if (*parkXFractionParam < 0.05 || *parkXFractionParam > 0.95 ||
            *parkYFractionParam < 0.05 || *parkYFractionParam > 0.95) {
            return Error("click park fractions must be between 0.05 and 0.95", "invalid_click");
        }
        double parkXFraction = *parkXFractionParam;
        double parkYFraction = *parkYFractionParam;
        clickOptions.parkX = static_cast<double>(std::max(1, screenWidth)) * parkXFraction;
        clickOptions.parkY = static_cast<double>(std::max(1, screenHeight)) * parkYFraction;
    }
    bool clicked = PerformClickActivation(target->x, target->y, button, clickOptions);
    if (!clicked) {
        return Error("click activation failed", "input_failed");
    }
    json data = {
        {"x", target->x},
        {"y", target->y},
        {"clicked", true},
        {"motion", motion},
        {"targetKind", target->kind},
        {"hoverSafe", clickOptions.hoverSafe},
        {"clickDurationMs", clickOptions.durationMs},
        {"clickSteps", clickOptions.steps},
        {"preClickSettleMs", clickOptions.preClickSettleMs},
        {"clickHoldMs", clickOptions.clickHoldMs},
        {"parkBeforeClick", clickOptions.parkBeforeClick},
        {"parkX", clickOptions.parkX},
        {"parkY", clickOptions.parkY}
    };
    if (target->rect.has_value()) {
        data["rect"] = BoundsToJson(*target->rect);
    }
    return Ok(data);
}

json RunMouseMoveCommand(const std::string& session, const json& params, const json& controlGate) {
    if (auto unknown = UnknownParam(params, {
        "x", "y", "durationMs", "steps", "observe",
        "controlSession", "controlSessionToken", "controlScope"
    })) {
        return Error("unknown mouse_move parameter: " + *unknown, "invalid_mouse_move");
    }

    auto x = NumberParam(params, "x", 0.0);
    auto y = NumberParam(params, "y", 0.0);
    if (!x || !y) {
        return Error("mouse_move requires numeric x and y", "invalid_mouse_move");
    }
    auto durationMs = IntParam(params, "durationMs", 0);
    auto steps = IntParam(params, "steps", 0);
    auto observe = BoolParam(params, "observe", false);
    if (!durationMs || !steps || !observe) {
        return Error("mouse_move requires integer durationMs/steps and boolean observe", "invalid_mouse_move");
    }
    if (*durationMs < 0 || *steps < 0) {
        return Error("mouse_move requires non-negative durationMs and steps", "invalid_mouse_move");
    }
    DaemonObservedEvent observed(*observe, session, "mouse_move", params, controlGate);
    observed.Capture("before");
    if (*durationMs > 0 || *steps > 1) {
        Platform::MoveMouseSmooth(*x, *y, *durationMs, *steps);
    } else {
        Platform::MoveMouse(*x, *y);
    }
    observed.Capture("after");
    observed.Finish();
    json data = {{"x", *x}, {"y", *y}};
    if (observed.id() != 0) {
        observed.AddTo(data);
    }
    return Ok(data);
}

json RunMouseDragCommand(const std::string& session, const json& params, const json& controlGate) {
    if (auto unknown = UnknownParam(params, {
        "from", "to", "button", "durationMs", "steps", "observe",
        "controlSession", "controlSessionToken", "controlScope"
    })) {
        return Error("unknown mouse_drag parameter: " + *unknown, "invalid_mouse_drag");
    }

    json fromParams = params;
    auto fromParam = StringParam(params, "from", "");
    auto toParam = StringParam(params, "to", "");
    if (!fromParam || !toParam) {
        return Error("mouse_drag requires string from/to", "invalid_mouse_drag");
    }
    if (IsBlank(*fromParam) || IsBlank(*toParam)) {
        return Error("mouse_drag requires non-empty from/to", "invalid_mouse_drag");
    }
    fromParams["target"] = *fromParam;
    json toParams = params;
    toParams["target"] = *toParam;
    auto from = PointFromTarget(session, fromParams);
    auto to = PointFromTarget(session, toParams);
    if (!from.has_value() || !to.has_value()) {
        return Error("could not resolve drag endpoint", "target_not_found");
    }
    auto buttonParam = StringParam(params, "button", "left");
    if (!buttonParam) {
        return Error("mouse_drag requires string button", "invalid_mouse_drag");
    }
    std::string button = *buttonParam;
    if (IsBlank(button)) {
        return Error("mouse_drag requires non-empty button", "invalid_mouse_drag");
    }
    auto durationMs = IntParam(params, "durationMs", 700);
    auto steps = IntParam(params, "steps", 32);
    auto observe = BoolParam(params, "observe", false);
    if (!durationMs || !steps || !observe) {
        return Error("mouse_drag requires integer durationMs/steps and boolean observe", "invalid_mouse_drag");
    }
    if (*durationMs < 0 || *steps < 0) {
        return Error("mouse_drag requires non-negative durationMs and steps", "invalid_mouse_drag");
    }
    DaemonObservedEvent observed(*observe, session, "mouse_drag", params, controlGate);
    observed.Capture("before");
    Platform::DragMouseSmooth(from->first, from->second, to->first, to->second, button, *durationMs, *steps);
    observed.Capture("after");
    observed.Finish();
    json data = {
        {"from", {{"x", from->first}, {"y", from->second}}},
        {"to", {{"x", to->first}, {"y", to->second}}},
        {"button", button},
        {"durationMs", *durationMs},
        {"steps", *steps}
    };
    if (observed.id() != 0) {
        observed.AddTo(data);
    }
    return Ok(data);
}

json RunMouseDownCommand(const json& params) {
    if (auto unknown = UnknownParam(params, {"button", "clickCount", "controlSession", "controlSessionToken", "controlScope"})) {
        return Error("unknown mouse_down parameter: " + *unknown, "invalid_mouse_down");
    }
    auto clickCount = IntParam(params, "clickCount", 1);
    auto button = StringParam(params, "button", "left");
    if (!clickCount || !button) {
        return Error("mouse_down requires string button and integer clickCount", "invalid_mouse_down");
    }
    if (IsBlank(*button)) {
        return Error("mouse_down requires non-empty button", "invalid_mouse_down");
    }
    if (*clickCount < 1 || *clickCount > 5) {
        return Error("mouse_down clickCount must be between 1 and 5", "invalid_mouse_down");
    }
    Platform::MouseDown(*button, *clickCount);
    return Ok();
}

json RunMouseUpCommand(const json& params) {
    if (auto unknown = UnknownParam(params, {"button", "clickCount", "controlSession", "controlSessionToken", "controlScope"})) {
        return Error("unknown mouse_up parameter: " + *unknown, "invalid_mouse_up");
    }
    auto clickCount = IntParam(params, "clickCount", 1);
    auto button = StringParam(params, "button", "left");
    if (!clickCount || !button) {
        return Error("mouse_up requires string button and integer clickCount", "invalid_mouse_up");
    }
    if (IsBlank(*button)) {
        return Error("mouse_up requires non-empty button", "invalid_mouse_up");
    }
    if (*clickCount < 1 || *clickCount > 5) {
        return Error("mouse_up clickCount must be between 1 and 5", "invalid_mouse_up");
    }
    Platform::MouseUp(*button, *clickCount);
    return Ok();
}

} // namespace ComputerCpp
