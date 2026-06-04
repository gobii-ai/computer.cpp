#pragma once

#include "computer_cpp/Platform.h"
#include "computer_cpp/Timeline.h"

#include <nlohmann/json.hpp>

namespace ComputerCpp {

nlohmann::json BoundsToJson(const Platform::Bounds& bounds);
nlohmann::json AppToJson(const Platform::AppInfo& app);
nlohmann::json WindowToJson(const Platform::WindowInfo& window);
nlohmann::json FocusedToJson(const Platform::FocusedElementInfo& focused);
nlohmann::json RefToJson(const Platform::RefRecord& ref);
nlohmann::json TimelineEventToJson(const TimelineEvent& event);
nlohmann::json TimelineFrameToJson(const TimelineFrame& frame);

} // namespace ComputerCpp
