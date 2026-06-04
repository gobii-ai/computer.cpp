#include "DaemonObservation.h"

#include "computer_cpp/AppPaths.h"
#include "computer_cpp/Platform.h"
#include "computer_cpp/StringUtils.h"
#include "computer_cpp/Timeline.h"

#include "DaemonJson.h"
#include "DaemonParsing.h"
#include "DaemonProtocol.h"
#include "DaemonTargetText.h"

#include <array>
#include <string_view>

namespace ComputerCpp {

namespace {

struct ObservationEventResolution {
    std::optional<int64_t> eventId;
    std::string error;
    std::string code;
};

ObservationEventResolution ResolveObservationEventId(const std::string& session, const std::string& eventParam) {
    if (eventParam.empty() || eventParam == "last" || eventParam == "@last") {
        auto last = LastTimelineEventId(session);
        if (!last.has_value()) {
            return {std::nullopt, "no observed events are available", "event_not_found"};
        }
        return {*last, "", ""};
    }

    auto parsedEventId = ParseEventId(eventParam);
    if (!parsedEventId) {
        return {std::nullopt, "invalid observed event reference: " + eventParam, "invalid_event_ref"};
    }
    return {*parsedEventId, "", ""};
}

} // namespace

namespace {

using ObservationHandler = nlohmann::json (*)(const std::string&, const nlohmann::json&);

struct ObservationRoute {
    std::string_view method;
    ObservationHandler run;
};

nlohmann::json RunObserveEvents(const std::string& session, const nlohmann::json& params) {
    if (auto unknown = UnknownParam(params, {"limit", "controlSession", "controlSessionToken", "controlScope"})) {
        return Error("unknown observe events parameter: " + *unknown, "invalid_limit");
    }
    auto limit = IntParam(params, "limit", 10);
    if (!limit) {
        return Error("observe events requires integer limit", "invalid_limit");
    }
    if (*limit <= 0) {
        return Error("observe events limit must be positive", "invalid_limit");
    }

    auto events = RecentTimelineEvents(session, *limit);
    nlohmann::json data = nlohmann::json::array();
    for (const auto& event : events) {
        data.push_back(TimelineEventToJson(event));
    }
    return Ok({{"events", data}, {"db", TimelineDbPath(session).string()}});
}

nlohmann::json RunObserveFrames(const std::string& session, const nlohmann::json& params) {
    if (auto unknown = UnknownParam(params, {"event", "limit", "controlSession", "controlSessionToken", "controlScope"})) {
        return Error("unknown observe frames parameter: " + *unknown, "invalid_event_ref");
    }
    auto eventParam = StringParam(params, "event", "last");
    if (!eventParam) {
        return Error("observe frames event must be a string", "invalid_event_ref");
    }
    if (params.contains("event") && IsBlank(*eventParam)) {
        return Error("observe frames event must be non-empty", "invalid_event_ref");
    }
    auto limit = IntParam(params, "limit", 100);
    if (!limit) {
        return Error("observe frames requires integer limit", "invalid_limit");
    }
    if (*limit <= 0) {
        return Error("observe frames limit must be positive", "invalid_limit");
    }
    auto event = ResolveObservationEventId(session, *eventParam);
    if (!event.eventId) {
        return Error(event.error, event.code);
    }

    auto frames = TimelineFramesForEvent(session, *event.eventId, *limit);
    nlohmann::json data = nlohmann::json::array();
    for (const auto& frame : frames) {
        data.push_back(TimelineFrameToJson(frame));
    }
    return Ok({{"eventId", *event.eventId}, {"frames", data}});
}

constexpr auto kObservationRoutes = std::to_array<ObservationRoute>({
    {"observe_events", RunObserveEvents},
    {"observe_frames", RunObserveFrames},
});

const ObservationRoute* FindObservationRoute(std::string_view method) {
    for (const auto& route : kObservationRoutes) {
        if (method == route.method) {
            return &route;
        }
    }
    return nullptr;
}

} // namespace

bool IsObservationCommand(const std::string& method) {
    return FindObservationRoute(method) != nullptr;
}

nlohmann::json RunObservationCommand(const std::string& session, const std::string& method, const nlohmann::json& params) {
    if (const auto* route = FindObservationRoute(method)) {
        return route->run(session, params);
    }
    return Error("unknown observation method: " + method, "unknown_method");
}

bool BoundsCenterInside(const Platform::Bounds& bounds, const Platform::Bounds& container) {
    if (!bounds.available || !container.available) {
        return true;
    }
    double x = bounds.x + bounds.width / 2.0;
    double y = bounds.y + bounds.height / 2.0;
    return x >= container.x && x <= container.x + container.width &&
           y >= container.y && y <= container.y + container.height;
}

std::optional<int64_t> ParseEventId(const std::string& value) {
    std::string normalized = NormalizeRef(value);
    if (normalized.rfind("ev", 0) == 0) {
        normalized = normalized.substr(2);
    }
    return ParseIntegerStrict<int64_t>(normalized);
}

std::optional<TimelineFrame> CaptureObservedFrame(const std::string& session, int64_t eventId, const std::string& label) {
    return AddTimelineFrame(session, eventId, label);
}

} // namespace ComputerCpp
