#include "DaemonJson.h"

#include <string>

using json = nlohmann::json;

namespace ComputerCpp {

json BoundsToJson(const Platform::Bounds& bounds) {
    return {
        {"available", bounds.available},
        {"x", bounds.x},
        {"y", bounds.y},
        {"width", bounds.width},
        {"height", bounds.height}
    };
}

json AppToJson(const Platform::AppInfo& app) {
    return {
        {"available", app.available},
        {"pid", app.pid},
        {"name", app.name},
        {"bundleId", app.bundleId}
    };
}

json WindowToJson(const Platform::WindowInfo& window) {
    return {
        {"available", window.available},
        {"id", window.id},
        {"title", window.title},
        {"appClass", window.appClass},
        {"pid", window.pid},
        {"active", window.active},
        {"bounds", BoundsToJson(window.bounds)}
    };
}

json FocusedToJson(const Platform::FocusedElementInfo& focused) {
    return {
        {"available", focused.available},
        {"acceptsTextInput", focused.acceptsTextInput},
        {"valueSettable", focused.valueSettable},
        {"role", focused.role},
        {"subrole", focused.subrole},
        {"title", focused.title},
        {"description", focused.description},
        {"value", focused.value},
        {"bounds", BoundsToJson(focused.bounds)}
    };
}

json RefToJson(const Platform::RefRecord& ref) {
    return {
        {"ref", ref.ref},
        {"displayRef", "@" + ref.ref},
        {"kind", ref.kind},
        {"source", ref.source},
        {"role", ref.role},
        {"name", ref.name},
        {"value", ref.value},
        {"app", ref.app},
        {"pid", ref.pid},
        {"bounds", BoundsToJson(ref.bounds)},
        {"confidence", ref.confidence}
    };
}

json TimelineEventToJson(const TimelineEvent& event) {
    return {
        {"id", event.id},
        {"eventRef", "@ev" + std::to_string(event.id)},
        {"type", event.type},
        {"app", event.app},
        {"bundleId", event.bundleId},
        {"startedAtMs", event.startedAtMs},
        {"endedAtMs", event.endedAtMs},
        {"params", json::parse(event.paramsJson, nullptr, false)}
    };
}

json TimelineFrameToJson(const TimelineFrame& frame) {
    return {
        {"id", frame.id},
        {"frameRef", "@f" + std::to_string(frame.id)},
        {"eventId", frame.eventId},
        {"label", frame.label},
        {"path", frame.path.string()},
        {"capturedAtMs", frame.capturedAtMs}
    };
}

} // namespace ComputerCpp
