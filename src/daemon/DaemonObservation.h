#pragma once

#include "computer_cpp/Platform.h"
#include "computer_cpp/Timeline.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace ComputerCpp {

bool IsObservationCommand(const std::string& method);
nlohmann::json RunObservationCommand(const std::string& session, const std::string& method, const nlohmann::json& params);
bool BoundsCenterInside(const Platform::Bounds& bounds, const Platform::Bounds& container);
std::optional<int64_t> ParseEventId(const std::string& value);
std::optional<TimelineFrame> CaptureObservedFrame(const std::string& session, int64_t eventId, const std::string& label);

} // namespace ComputerCpp
