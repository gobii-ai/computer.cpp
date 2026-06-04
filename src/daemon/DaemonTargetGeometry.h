#pragma once

#include "computer_cpp/Platform.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ComputerCpp {

std::optional<double> ParseDoubleStrict(std::string_view value);
std::optional<double> NumberFromJson(const nlohmann::json& value);
std::optional<Platform::Bounds> RectFromLTRB(double left, double top, double right, double bottom);
std::optional<Platform::Bounds> RectFromJson(const nlohmann::json& rect);
std::optional<Platform::Bounds> RectFromTargetString(const std::string& target);
std::optional<Platform::Bounds> RectFromClickParams(const nlohmann::json& params);
std::pair<double, double> StablePointInRect(const Platform::Bounds& rect, const nlohmann::json& params);
bool PointInsideBounds(double x, double y, const Platform::Bounds& bounds);

} // namespace ComputerCpp
