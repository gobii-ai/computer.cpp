#include "DaemonImage.h"

#include "computer_cpp/AppPaths.h"
#include "computer_cpp/Image.h"
#include "computer_cpp/StringUtils.h"

#include "DaemonParsing.h"
#include "DaemonProtocol.h"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>

using json = nlohmann::json;

namespace ComputerCpp {

json RunImageInfo(const json& params) {
    if (auto unknown = UnknownParam(params, {"path", "controlSession", "controlSessionToken", "controlScope"})) {
        return Error("unknown image info parameter: " + *unknown, "invalid_image_info");
    }
    auto pathParam = StringParam(params, "path", "");
    if (!pathParam) {
        return Error("image info path must be a string", "invalid_image_info");
    }
    std::string path = *pathParam;
    if (IsBlank(path)) {
        return Error("image info requires a path", "invalid_image_info");
    }
    auto image = Image::ReadImageRgb(path);
    if (!image.has_value() || !image->valid()) {
        return Error("could not read image: " + path, "image_read_failed");
    }
    return Ok({
        {"path", path},
        {"width", image->width},
        {"height", image->height}
    });
}

json RunImageSplit(const json& params) {
    if (auto unknown = UnknownParam(params, {
        "path", "outDir", "outputDir", "chunkHeight", "overlap", "prefix",
        "controlSession", "controlSessionToken", "controlScope"
    })) {
        return Error("unknown image split parameter: " + *unknown, "invalid_image_split");
    }
    auto pathParam = StringParam(params, "path", "");
    if (!pathParam) {
        return Error("image split path must be a string", "invalid_image_split");
    }
    std::string path = *pathParam;
    if (IsBlank(path)) {
        return Error("image split requires a path", "invalid_image_split");
    }
    std::filesystem::path inputPath(path);
    auto outDirParam = StringParam(params, "outDir", "");
    auto outputDirParam = StringParam(params, "outputDir", inputPath.parent_path().string());
    if (!outDirParam || !outputDirParam) {
        return Error("image split output directory must be a string", "invalid_image_split");
    }
    if ((params.contains("outDir") && IsBlank(*outDirParam)) ||
        (params.contains("outputDir") && IsBlank(*outputDirParam))) {
        return Error("image split output directory must be non-empty when provided", "invalid_image_split");
    }
    auto chunkHeightParam = IntParam(params, "chunkHeight", 1500);
    auto overlapParam = IntParam(params, "overlap", 160);
    if (!chunkHeightParam || !overlapParam) {
        return Error("image split requires integer chunkHeight and overlap", "invalid_image_split");
    }
    if (*chunkHeightParam <= 0) {
        return Error("image split chunkHeight must be positive", "invalid_image_split");
    }
    if (*overlapParam < 0) {
        return Error("image split overlap must be non-negative", "invalid_image_split");
    }
    if (*overlapParam >= *chunkHeightParam) {
        return Error("image split overlap must be smaller than chunkHeight", "invalid_image_split");
    }
    std::filesystem::path outDir = outDirParam->empty() ? *outputDirParam : *outDirParam;
    if (outDir.empty()) {
        outDir = ".";
    }
    auto prefixParam = StringParam(params, "prefix", "chunk");
    if (!prefixParam) {
        return Error("image split prefix must be a string", "invalid_image_split");
    }
    std::string prefix = *prefixParam;
    if (IsBlank(prefix)) {
        return Error("image split prefix must be non-empty", "invalid_image_split");
    }
    auto image = Image::ReadImageRgb(path);
    if (!image.has_value() || !image->valid()) {
        return Error("could not read image: " + path, "image_read_failed");
    }
    EnsureDirectory(outDir);
    int chunkHeight = std::clamp(*chunkHeightParam, 1, std::max(1, image->height));
    int overlap = std::clamp(*overlapParam, 0, std::max(0, chunkHeight - 1));
    int step = std::max(1, chunkHeight - overlap);
    json chunks = json::array();
    int index = 1;
    for (int y = 0; y < image->height; y += step) {
        int height = std::min(chunkHeight, image->height - y);
        if (height <= 0) {
            break;
        }
        auto crop = Image::CropRgb(image->rgb, image->width, image->height, 0, y, image->width, height);
        if (!crop.valid()) {
            return Error("failed to crop image chunk", "image_crop_failed");
        }
        std::ostringstream name;
        name << prefix << "-";
        if (index < 10) name << "0";
        name << index << ".png";
        std::filesystem::path chunkPath = outDir / name.str();
        if (!Image::WritePngRgb(chunkPath.string(), crop)) {
            return Error("failed to write image chunk: " + chunkPath.string(), "artifact_write_failed");
        }
        chunks.push_back({
            {"path", chunkPath.string()},
            {"index", index},
            {"x", 0},
            {"y", y},
            {"width", crop.width},
            {"height", crop.height}
        });
        if (y + height >= image->height) {
            break;
        }
        ++index;
    }
    return Ok({
        {"path", path},
        {"width", image->width},
        {"height", image->height},
        {"chunkHeight", chunkHeight},
        {"overlap", overlap},
        {"chunks", chunks}
    });
}

} // namespace ComputerCpp
