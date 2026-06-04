#include "DaemonScreenshot.h"

#include "computer_cpp/AppPaths.h"
#include "computer_cpp/Platform.h"
#include "computer_cpp/StringUtils.h"

#include "DaemonArtifacts.h"
#include "DaemonJson.h"
#include "DaemonParsing.h"
#include "DaemonProtocol.h"
#include "DaemonTargetGeometry.h"

#include <algorithm>
#include <optional>

using json = nlohmann::json;

namespace ComputerCpp {
namespace {

std::optional<Platform::Bounds> ScreenshotRegionFromParams(const json& params) {
    Platform::Bounds region;
    region.available = true;

    auto optionalNumber = [&](const char* key, double fallback) -> std::optional<double> {
        if (!params.contains(key)) {
            return fallback;
        }
        return NumberFromJson(params.at(key));
    };

    auto x = optionalNumber("x", 0.0);
    auto y = optionalNumber("y", 0.0);
    auto width = optionalNumber("width", 0.0);
    auto height = optionalNumber("height", 0.0);
    if (!x || !y || !width || !height || *width <= 0.0 || *height <= 0.0) {
        return std::nullopt;
    }

    region.x = *x;
    region.y = *y;
    region.width = *width;
    region.height = *height;
    return region;
}

} // namespace

json RunScreenshotCommand(const json& params) {
    if (auto unknown = UnknownParam(params, {
        "path", "frontmostWindowOnly", "maxDimension", "x", "y", "width", "height",
        "controlSession", "controlSessionToken", "controlScope"
    })) {
        return Error("unknown screenshot parameter: " + *unknown, "invalid_screenshot");
    }

    auto pathParam = StringParam(params, "path", "");
    if (!pathParam) {
        return Error("screenshot path must be a string", "invalid_screenshot");
    }
    std::string path = *pathParam;
    if (path.empty()) {
        path = (DefaultArtifactDir() / ("screenshot-" + TimestampForPath() + ".png")).string();
    } else if (IsBlank(path)) {
        return Error("screenshot path must be non-empty", "invalid_screenshot");
    }

    auto frontmostWindowOnlyParam = BoolParam(params, "frontmostWindowOnly", false);
    if (!frontmostWindowOnlyParam) {
        return Error("screenshot frontmostWindowOnly must be boolean", "invalid_screenshot");
    }
    bool frontmostWindowOnly = *frontmostWindowOnlyParam;
    auto maxDimensionParam = IntParam(params, "maxDimension", 0);
    if (!maxDimensionParam) {
        return Error("screenshot maxDimension must be an integer", "invalid_screenshot_region");
    }
    if (*maxDimensionParam < 0 || *maxDimensionParam > 8192) {
        return Error("screenshot maxDimension must be between 0 and 8192", "invalid_screenshot_region");
    }
    int maxDimension = *maxDimensionParam;
    bool hasRegion = params.contains("x") || params.contains("y") || params.contains("width") || params.contains("height");

    Platform::Bounds region;
    if (hasRegion) {
        auto parsedRegion = ScreenshotRegionFromParams(params);
        if (!parsedRegion) {
            return Error("screenshot region requires positive width and height", "invalid_screenshot_region");
        }
        region = *parsedRegion;
    }

    bool ok = false;
    if (hasRegion && maxDimension > 0) {
        ok = Platform::SaveScreenshotRegionScaled(path, region, maxDimension);
    } else if (hasRegion) {
        ok = Platform::SaveScreenshotRegion(path, region);
    } else if (maxDimension > 0) {
        ok = frontmostWindowOnly
            ? Platform::SaveScreenshotRegionScaled(path, Platform::GetFrontmostWindowBounds(), maxDimension)
            : Platform::SaveScreenshotScaled(path, maxDimension);
    } else {
        ok = frontmostWindowOnly
            ? Platform::SaveScreenshotRegion(path, Platform::GetFrontmostWindowBounds())
            : Platform::SaveScreenshot(path);
    }

    if (!ok) {
        return Error("screenshot capture failed; Screen Recording permission may be missing", "permission_or_capture_failed");
    }

    json data = {{"path", path}};
    if (maxDimension > 0) {
        data["maxDimension"] = maxDimension;
    }
    if (frontmostWindowOnly) {
        data["frontmostWindowBounds"] = BoundsToJson(Platform::GetFrontmostWindowBounds());
    }
    if (hasRegion) {
        data["region"] = BoundsToJson(region);
    }
    return Ok(data);
}

} // namespace ComputerCpp
