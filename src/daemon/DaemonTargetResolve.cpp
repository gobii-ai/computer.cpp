#include "DaemonTargetResolve.h"

#include "computer_cpp/AppPaths.h"
#include "computer_cpp/Platform.h"
#include "computer_cpp/RefStore.h"
#include "computer_cpp/StringUtils.h"

#include "DaemonParsing.h"
#include "DaemonTargetGeometry.h"
#include "DaemonTargetRefs.h"
#include "DaemonTargetText.h"

namespace ComputerCpp {

using json = nlohmann::json;

std::optional<std::pair<double, double>> PointFromTarget(const std::string& session, const json& params) {
    if (params.contains("x") && params.contains("y")) {
        auto x = NumberFromJson(params["x"]);
        auto y = NumberFromJson(params["y"]);
        if (!x || !y) {
            return std::nullopt;
        }
        return std::pair<double, double>{*x, *y};
    }

    std::string target;
    if (params.contains("target")) {
        if (!params.at("target").is_string()) {
            return std::nullopt;
        }
        target = params.at("target").get<std::string>();
    }
    if (target.empty()) {
        return std::nullopt;
    }

    if (target.rfind("point:", 0) == 0) {
        std::string raw = target.substr(6);
        size_t comma = raw.find(',');
        if (comma == std::string::npos) {
            return std::nullopt;
        }
        auto x = ParseDoubleStrict(Trim(raw.substr(0, comma)));
        auto y = ParseDoubleStrict(Trim(raw.substr(comma + 1)));
        if (!x || !y) {
            return std::nullopt;
        }
        return std::pair<double, double>{*x, *y};
    }

    if (target.rfind("rect:", 0) == 0 || target.rfind("box:", 0) == 0) {
        auto rect = RectFromTargetString(target);
        if (!rect.has_value()) {
            return std::nullopt;
        }
        return StablePointInRect(*rect, params);
    }

    if (HasRemovedVisualTargetPrefix(target)) {
        return std::nullopt;
    }

    auto refs = LoadRefs(RefStorePath(session));
    auto roleTarget = ParseRoleTarget(target);
    if (roleTarget.valid) {
        for (const auto& ref : refs) {
            if (!ref.bounds.available) {
                continue;
            }
            if (NormalizeRole(ref.role) != roleTarget.role) {
                continue;
            }
            if (!roleTarget.name.empty()) {
                std::string haystack = ref.name.empty() ? ref.value : ref.name;
                if (!ContainsCaseInsensitive(haystack, roleTarget.name)) {
                    continue;
                }
            }
            return std::pair<double, double>{
                ref.bounds.x + ref.bounds.width / 2.0,
                ref.bounds.y + ref.bounds.height / 2.0
            };
        }
        return std::nullopt;
    }

    auto found = FindRef(refs, NormalizeRef(target));
    if (!found.has_value() || !found->bounds.available) {
        return std::nullopt;
    }

    return std::pair<double, double>{
        found->bounds.x + found->bounds.width / 2.0,
        found->bounds.y + found->bounds.height / 2.0
    };
}

std::optional<ResolvedClickTarget> ClickTargetFromParams(const std::string& session, const json& params) {
    if (auto rect = RectFromClickParams(params)) {
        auto point = StablePointInRect(*rect, params);
        return ResolvedClickTarget{point.first, point.second, rect, "rect"};
    }
    if (auto point = PointFromTarget(session, params)) {
        return ResolvedClickTarget{point->first, point->second, std::nullopt, "point"};
    }
    return std::nullopt;
}

std::optional<std::pair<double, double>> ScrollAnchorPoint(const std::string& session, const json& params) {
    if (params.contains("at")) {
        json targetParams = {{"target", params["at"]}};
        auto point = PointFromTarget(session, targetParams);
        if (!point.has_value()) {
            return std::nullopt;
        }
        auto offsetX = NumberParam(params, "atOffsetX", 0.0);
        auto offsetY = NumberParam(params, "atOffsetY", 0.0);
        if (!offsetX || !offsetY) {
            return std::nullopt;
        }
        return std::pair<double, double>{
            point->first + *offsetX,
            point->second + *offsetY
        };
    }

    bool anchor = true;
    if (params.contains("anchor")) {
        if (!params.at("anchor").is_boolean()) {
            return std::nullopt;
        }
        anchor = params.at("anchor").get<bool>();
    }
    if (anchor) {
        auto window = Platform::GetFrontmostWindowBounds();
        double cursorX = 0.0;
        double cursorY = 0.0;
        Platform::GetCursorPosition(cursorX, cursorY);
        bool centerAnchor = false;
        if (params.contains("centerAnchor")) {
            if (!params.at("centerAnchor").is_boolean()) {
                return std::nullopt;
            }
            centerAnchor = params.at("centerAnchor").get<bool>();
        }
        if (!centerAnchor && PointInsideBounds(cursorX, cursorY, window)) {
            return std::pair<double, double>{cursorX, cursorY};
        }
        if (window.available && window.width > 80 && window.height > 80) {
            return std::pair<double, double>{
                window.x + window.width * 0.56,
                window.y + window.height * 0.54
            };
        }
        int screenWidth = 0;
        int screenHeight = 0;
        Platform::GetScreenSize(screenWidth, screenHeight);
        if (screenWidth > 0 && screenHeight > 0) {
            return std::pair<double, double>{
                screenWidth * 0.67,
                screenHeight * 0.54
            };
        }
    }

    return std::nullopt;
}

} // namespace ComputerCpp
