#include "DaemonClick.h"

#include "computer_cpp/Platform.h"
#include "computer_cpp/StringUtils.h"

#include "DaemonParsing.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace ComputerCpp {
namespace {

bool IsHoverSafeClickMode(const std::string& mode) {
    return mode == "hover_safe" || mode == "hover-safe" ||
        mode == "link_safe" || mode == "link-safe";
}

} // namespace

ClickActivationOptions ClickActivationOptionsFromParams(const nlohmann::json& params) {
    ClickActivationOptions options;
    auto motion = StringParam(params, "motion", "natural");
    auto clickMode = StringParam(params, "clickMode", motion.value_or("natural"));
    auto hoverSafe = BoolParam(params, "hoverSafe", false);
    auto parkBeforeClick = BoolParam(params, "parkBeforeClick", false);
    if (!motion || !clickMode || !hoverSafe || !parkBeforeClick) {
        options.valid = false;
        options.error = "click requires string motion/clickMode and boolean hoverSafe/parkBeforeClick";
        return options;
    }
    if (IsBlank(*motion) || IsBlank(*clickMode)) {
        options.valid = false;
        options.error = "click requires non-empty motion/clickMode";
        return options;
    }
    options.mode = *clickMode;
    options.hoverSafe = *hoverSafe || IsHoverSafeClickMode(options.mode);
    auto durationMs = IntParam(params, {"clickDurationMs", "durationMs"}, options.hoverSafe ? 95 : 420);
    auto steps = IntParam(params, {"clickSteps", "steps"}, options.hoverSafe ? 5 : 24);
    auto clickCount = IntParam(params, "clickCount", 1);
    auto preClickSettleMs = IntParam(params, "preClickSettleMs", options.hoverSafe ? 0 : -1);
    auto clickHoldMs = IntParam(params, "clickHoldMs", options.hoverSafe ? 58 : -1);
    auto parkDurationMs = IntParam(params, "parkDurationMs", 130);
    auto parkSteps = IntParam(params, "parkSteps", 6);
    options.valid = durationMs && steps && clickCount && preClickSettleMs && clickHoldMs && parkDurationMs && parkSteps;
    if (!options.valid) {
        options.error = "click requires integer click timing options";
        return options;
    }
    if (*clickCount < 1 || *clickCount > 5) {
        options.valid = false;
        options.error = "click clickCount must be between 1 and 5";
        return options;
    }
    if (*durationMs < 0 || *durationMs > 5000 ||
        *steps < 0 || *steps > 120 ||
        *preClickSettleMs < -1 || *preClickSettleMs > 5000 ||
        *clickHoldMs < -1 || *clickHoldMs > 5000 ||
        *parkDurationMs < 0 || *parkDurationMs > 1200 ||
        *parkSteps < 0 || *parkSteps > 80) {
        options.valid = false;
        options.error = "click timing options out of range";
        return options;
    }
    options.durationMs = *durationMs;
    options.steps = *steps;
    options.clickCount = *clickCount;
    options.preClickSettleMs = *preClickSettleMs;
    options.clickHoldMs = *clickHoldMs;
    options.parkBeforeClick = *parkBeforeClick;
    options.parkDurationMs = *parkDurationMs;
    options.parkSteps = *parkSteps;
    return options;
}

bool PerformClickActivation(double screenX,
                            double screenY,
                            const std::string& button,
                            const ClickActivationOptions& options) {
    if (!options.hoverSafe) {
        return Platform::ClickSmooth(screenX, screenY, button, options.clickCount, options.durationMs, options.steps);
    }

    if (options.parkBeforeClick && std::isfinite(options.parkX) && std::isfinite(options.parkY)) {
        Platform::MoveMouseSmooth(options.parkX, options.parkY, options.parkDurationMs, options.parkSteps);
    }
    Platform::MoveMouseSmooth(screenX, screenY, options.durationMs, options.steps);
    if (options.preClickSettleMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(options.preClickSettleMs));
    }
    Platform::MouseDown(button, options.clickCount);
    if (options.clickHoldMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(options.clickHoldMs));
    }
    Platform::MouseUp(button, options.clickCount);
    return true;
}

} // namespace ComputerCpp
