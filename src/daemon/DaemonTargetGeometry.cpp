#include "DaemonTargetGeometry.h"

#include "computer_cpp/StringUtils.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace ComputerCpp {

std::optional<double> ParseDoubleStrict(std::string_view value) {
    double parsed = 0.0;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc() || ptr != value.data() + value.size() || !std::isfinite(parsed)) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<double> NumberFromJson(const nlohmann::json& value) {
    if (value.is_number()) {
        double number = value.get<double>();
        return std::isfinite(number) ? std::optional<double>(number) : std::nullopt;
    }
    if (value.is_string()) {
        return ParseDoubleStrict(value.get<std::string>());
    }
    return std::nullopt;
}

std::optional<Platform::Bounds> RectFromLTRB(double left, double top, double right, double bottom) {
    if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(right) || !std::isfinite(bottom)) {
        return std::nullopt;
    }
    if (right < left) {
        std::swap(left, right);
    }
    if (bottom < top) {
        std::swap(top, bottom);
    }
    if (right - left < 1.0 || bottom - top < 1.0) {
        return std::nullopt;
    }
    Platform::Bounds bounds;
    bounds.available = true;
    bounds.x = left;
    bounds.y = top;
    bounds.width = right - left;
    bounds.height = bottom - top;
    return bounds;
}

std::optional<Platform::Bounds> RectFromJson(const nlohmann::json& rect) {
    if (rect.is_array() && rect.size() >= 4) {
        auto left = NumberFromJson(rect[0]);
        auto top = NumberFromJson(rect[1]);
        auto right = NumberFromJson(rect[2]);
        auto bottom = NumberFromJson(rect[3]);
        if (left && top && right && bottom) {
            return RectFromLTRB(*left, *top, *right, *bottom);
        }
        return std::nullopt;
    }
    if (!rect.is_object()) {
        return std::nullopt;
    }

    auto value = [&](std::initializer_list<const char*> keys) -> std::optional<double> {
        for (const char* key : keys) {
            if (rect.contains(key)) {
                return NumberFromJson(rect.at(key));
            }
        }
        return std::nullopt;
    };

    auto left = value({"left", "l"});
    auto top = value({"top", "t"});
    auto right = value({"right", "r"});
    auto bottom = value({"bottom", "b"});
    if (left && top && right && bottom) {
        return RectFromLTRB(*left, *top, *right, *bottom);
    }

    auto x = value({"x", "left", "l"});
    auto y = value({"y", "top", "t"});
    auto width = value({"width", "w"});
    auto height = value({"height", "h"});
    if (x && y && width && height) {
        return RectFromLTRB(*x, *y, *x + *width, *y + *height);
    }
    return std::nullopt;
}

std::optional<Platform::Bounds> RectFromTargetString(const std::string& target) {
    std::string raw;
    if (target.rfind("rect:", 0) == 0) {
        raw = target.substr(5);
    } else if (target.rfind("box:", 0) == 0) {
        raw = target.substr(4);
    } else {
        return std::nullopt;
    }

    std::vector<double> values;
    size_t start = 0;
    while (start <= raw.size()) {
        size_t comma = raw.find(',', start);
        std::string piece = Trim(raw.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (!piece.empty()) {
            auto parsed = ParseDoubleStrict(piece);
            if (!parsed) {
                return std::nullopt;
            }
            values.push_back(*parsed);
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    if (values.size() != 4) {
        return std::nullopt;
    }
    return RectFromLTRB(values[0], values[1], values[2], values[3]);
}

std::optional<Platform::Bounds> RectFromClickParams(const nlohmann::json& params) {
    for (const char* key : {"rect", "box", "bounds"}) {
        if (params.contains(key)) {
            if (auto rect = RectFromJson(params.at(key))) {
                return rect;
            }
        }
    }
    std::string target;
    if (params.contains("target")) {
        if (!params.at("target").is_string()) {
            return std::nullopt;
        }
        target = params.at("target").get<std::string>();
    }
    if (!target.empty()) {
        return RectFromTargetString(target);
    }
    return std::nullopt;
}

std::pair<double, double> StablePointInRect(const Platform::Bounds& rect, const nlohmann::json& params) {
    auto xFractionParam = params.contains("rectClickXFraction")
        ? NumberFromJson(params.at("rectClickXFraction"))
        : std::optional<double>(0.5);
    auto yFractionParam = params.contains("rectClickYFraction")
        ? NumberFromJson(params.at("rectClickYFraction"))
        : std::optional<double>(0.5);
    double xFraction = std::clamp(xFractionParam.value_or(0.5), 0.05, 0.95);
    double yFraction = std::clamp(yFractionParam.value_or(0.5), 0.05, 0.95);
    double x = rect.x + rect.width * xFraction;
    double y = rect.y + rect.height * yFraction;

    double insetX = std::min(8.0, std::max(0.0, rect.width * 0.22));
    double insetY = std::min(6.0, std::max(0.0, rect.height * 0.22));
    if (rect.width > insetX * 2.0 + 1.0) {
        x = std::clamp(x, rect.x + insetX, rect.x + rect.width - insetX);
    }
    if (rect.height > insetY * 2.0 + 1.0) {
        y = std::clamp(y, rect.y + insetY, rect.y + rect.height - insetY);
    }
    return {x, y};
}

bool PointInsideBounds(double x, double y, const Platform::Bounds& bounds) {
    return bounds.available &&
        x >= bounds.x && x <= bounds.x + bounds.width &&
        y >= bounds.y && y <= bounds.y + bounds.height;
}

} // namespace ComputerCpp
