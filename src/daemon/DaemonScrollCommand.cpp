#include "DaemonInput.h"

#include "computer_cpp/AppPaths.h"
#include "computer_cpp/HumanInput.h"
#include "computer_cpp/Platform.h"
#include "computer_cpp/StringUtils.h"
#include "computer_cpp/Timeline.h"

#include "DaemonJson.h"
#include "DaemonObservedEvent.h"
#include "DaemonParsing.h"
#include "DaemonProtocol.h"
#include "DaemonTargetResolve.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>

namespace ComputerCpp {

nlohmann::json RunScrollCommand(const std::string& session, const nlohmann::json& params, const nlohmann::json& controlGate) {
    if (auto unknown = UnknownParam(params, {
        "dy", "dx", "humanize", "durationMs", "steps", "jitter",
        "maxGestureDelta", "maxScrollGestureDelta", "samples", "observe",
        "focusApp", "at", "atOffsetX", "atOffsetY", "anchor", "centerAnchor",
        "controlSession", "controlSessionToken", "controlScope"
    })) {
        return Error("unknown scroll parameter: " + *unknown, "invalid_scroll");
    }

    auto parsedDy = IntParam(params, "dy", 0);
    auto parsedDx = IntParam(params, "dx", 0);
    if (!parsedDy || !parsedDx) {
        return Error("scroll requires integer dy and dx", "invalid_scroll");
    }
    int dy = *parsedDy;
    int dx = *parsedDx;
    auto humanizeParam = BoolParam(params, "humanize", true);
    if (!humanizeParam) {
        return Error("scroll requires boolean humanize", "invalid_scroll");
    }
    bool humanize = *humanizeParam;
    auto durationMsParam = IntParam(params, "durationMs", 0);
    auto stepsParam = IntParam(params, "steps", 0);
    auto jitterParam = NumberParam(params, "jitter", 0.0);
    auto maxGestureDeltaParam = IntParam(params, {"maxGestureDelta", "maxScrollGestureDelta"}, humanize ? 180 : 0);
    auto samplesParam = IntParam(params, "samples", 3);
    if (!durationMsParam || !stepsParam || !jitterParam || !maxGestureDeltaParam || !samplesParam) {
        return Error("scroll requires numeric durationMs, steps, jitter, maxGestureDelta, and samples", "invalid_scroll");
    }
    if (*durationMsParam < 0 || *stepsParam < 0 || *jitterParam < 0.0 || *maxGestureDeltaParam < 0) {
        return Error("scroll requires non-negative durationMs, steps, jitter, and maxGestureDelta", "invalid_scroll");
    }
    if (*samplesParam < 1 || *samplesParam > 6) {
        return Error("scroll samples must be between 1 and 6", "invalid_scroll");
    }
    int durationMs = *durationMsParam;
    int steps = *stepsParam;
    double jitter = *jitterParam;
    int maxGestureDelta = *maxGestureDeltaParam;
    auto observeParam = BoolParam(params, "observe", false);
    auto focusAppParam = StringParam(params, "focusApp", "");
    auto atParam = StringParam(params, "at", "");
    auto anchorParam = BoolParam(params, "anchor", true);
    auto centerAnchorParam = BoolParam(params, "centerAnchor", false);
    if (!observeParam || !focusAppParam || !atParam || !anchorParam || !centerAnchorParam) {
        return Error("scroll requires boolean observe/anchor/centerAnchor and string focusApp/at", "invalid_scroll");
    }
    bool observe = *observeParam;
    int samples = *samplesParam;
    std::string focusApp = *focusAppParam;
    if (params.contains("focusApp") && IsBlank(focusApp)) {
        return Error("scroll focusApp must be non-empty when provided", "invalid_scroll");
    }
    std::string at = *atParam;
    if (params.contains("at") && IsBlank(at)) {
        return Error("scroll at target must be non-empty when provided", "invalid_scroll");
    }
    auto frontmost = Platform::GetFrontmostApp();
    if (!focusApp.empty() && (!frontmost.available || !ContainsCaseInsensitive(frontmost.name, focusApp))) {
        auto error = Error("frontmost app did not match scroll focus guard", "focus_guard_failed");
        error["data"] = {
            {"expected", focusApp},
            {"frontmost", AppToJson(frontmost)}
        };
        return error;
    }
    std::optional<std::pair<double, double>> anchor = ScrollAnchorPoint(session, params);
    if (params.contains("at") && !anchor.has_value()) {
        auto error = Error("could not resolve scroll anchor target", "target_not_found");
        error["data"] = {{"target", at}};
        return error;
    }
    DaemonObservedEvent observed(observe, session, "scroll", params, controlGate);
    observed.Capture("before");
    if (anchor.has_value()) {
        double cursorX = 0.0;
        double cursorY = 0.0;
        Platform::GetCursorPosition(cursorX, cursorY);
        double distance = std::hypot(anchor->first - cursorX, anchor->second - cursorY);
        if (distance > 12.0) {
            int moveDuration = std::clamp(static_cast<int>(distance * 1.1), 180, 950);
            int moveSteps = std::clamp(static_cast<int>(distance / 12.0), 12, 70);
            Platform::MoveMouseSmooth(anchor->first, anchor->second, moveDuration, moveSteps);
            std::this_thread::sleep_for(std::chrono::milliseconds(90));
        }
    }
    bool explicitGesture = durationMs > 0 || steps > 1 || jitter > 0.0;
    bool humanizedGesture = false;
    if (!explicitGesture && humanize && std::max(std::abs(dy), std::abs(dx)) >= 96) {
        auto plan = HumanInput::PlanScrollGesture(dy, dx, 0, 0);
        durationMs = plan.durationMs;
        steps = plan.steps;
        jitter = 0.035;
        humanizedGesture = true;
        explicitGesture = true;
    }
    nlohmann::json scrollClusters = nlohmann::json::array();
    auto performScroll = [&]() {
        int distance = std::max(std::abs(dy), std::abs(dx));
        if (humanize && maxGestureDelta > 0 && distance > maxGestureDelta) {
            auto clusters = HumanInput::PlanScrollClusters(dy, dx, durationMs, steps, maxGestureDelta);
            for (const auto& cluster : clusters) {
                Platform::ScrollGesture(cluster.deltaY, cluster.deltaX, cluster.durationMs, cluster.steps, jitter > 0.0 ? jitter : 0.035);
                scrollClusters.push_back({
                    {"dy", cluster.deltaY},
                    {"dx", cluster.deltaX},
                    {"durationMs", cluster.durationMs},
                    {"steps", cluster.steps},
                    {"pauseAfterMs", cluster.pauseAfterMs}
                });
                if (cluster.pauseAfterMs > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(cluster.pauseAfterMs));
                }
            }
            humanizedGesture = true;
            return;
        }
        if (explicitGesture) {
            Platform::ScrollGesture(dy, dx, durationMs, steps, jitter);
        } else {
            Platform::Scroll(dy, dx);
        }
    };
    if (observe && explicitGesture) {
        int distance = std::max(std::abs(dy), std::abs(dx));
        int effectiveDuration = durationMs > 0 ? durationMs : std::clamp(distance * 5, 450, 6500);
        int effectiveSteps = steps > 1 ? steps : std::clamp(distance / 4, 10, 120);
        std::thread sampler([&]() {
            int intervalMs = std::max(80, effectiveDuration / (samples + 1));
            for (int i = 1; i <= samples; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
                observed.Capture("sample-" + std::to_string(i));
            }
        });
        int savedDuration = durationMs;
        int savedSteps = steps;
        durationMs = effectiveDuration;
        steps = effectiveSteps;
        performScroll();
        durationMs = savedDuration;
        steps = savedSteps;
        sampler.join();
    } else {
        performScroll();
    }
    observed.Capture("after");
    observed.Finish();
    nlohmann::json data = {
        {"dy", dy},
        {"dx", dx},
        {"durationMs", durationMs},
        {"steps", steps},
        {"jitter", jitter},
        {"humanized", humanizedGesture},
        {"maxGestureDelta", maxGestureDelta}
    };
    if (!scrollClusters.empty()) {
        data["clusters"] = scrollClusters;
    }
    if (anchor.has_value()) {
        data["anchor"] = {{"x", anchor->first}, {"y", anchor->second}};
    }
    if (!focusApp.empty()) {
        data["focusApp"] = focusApp;
        data["frontmost"] = AppToJson(frontmost);
    }
    if (observed.id() != 0) {
        observed.AddTo(data);
        data["db"] = TimelineDbPath(session).string();
    }
    return Ok(data);
}

} // namespace ComputerCpp
