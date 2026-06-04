#pragma once

#include "computer_cpp/Platform.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <utility>

namespace ComputerCpp {

struct ResolvedClickTarget {
    double x = 0.0;
    double y = 0.0;
    std::optional<Platform::Bounds> rect;
    std::string kind = "point";
};

std::optional<std::pair<double, double>> PointFromTarget(const std::string& session, const nlohmann::json& params);
std::optional<ResolvedClickTarget> ClickTargetFromParams(const std::string& session, const nlohmann::json& params);
std::optional<std::pair<double, double>> ScrollAnchorPoint(const std::string& session, const nlohmann::json& params);

} // namespace ComputerCpp
