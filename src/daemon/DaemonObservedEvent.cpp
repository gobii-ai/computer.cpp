#include "DaemonObservedEvent.h"

#include "computer_cpp/Timeline.h"

#include "DaemonJson.h"
#include "DaemonObservation.h"
#include "DaemonProtocol.h"

#include <utility>

namespace ComputerCpp {

DaemonObservedEvent::DaemonObservedEvent(
    bool enabled,
    const std::string& session,
    std::string name,
    const nlohmann::json& params,
    const nlohmann::json& controlGate
)
    : enabled_(enabled),
      session_(session),
      eventId_(enabled ? BeginTimelineEvent(session, std::move(name), TimelineParamsWithControlSession(params, controlGate)) : 0) {
}

DaemonObservedEvent::~DaemonObservedEvent() {
    Finish();
}

int64_t DaemonObservedEvent::id() const {
    return eventId_;
}

void DaemonObservedEvent::Capture(const std::string& label) {
    if (!enabled_) {
        return;
    }
    if (auto frame = CaptureObservedFrame(session_, eventId_, label)) {
        std::lock_guard<std::mutex> lock(framesMutex_);
        frames_.push_back(TimelineFrameToJson(*frame));
    }
}

void DaemonObservedEvent::Finish() {
    if (!enabled_) {
        return;
    }
    EndTimelineEvent(session_, eventId_);
    enabled_ = false;
}

void DaemonObservedEvent::AddTo(nlohmann::json& data) const {
    data["eventId"] = eventId_;
    data["eventRef"] = "@ev" + std::to_string(eventId_);
    std::lock_guard<std::mutex> lock(framesMutex_);
    data["frames"] = frames_;
}

} // namespace ComputerCpp
