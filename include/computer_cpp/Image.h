#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ComputerCpp::Image {

struct RgbImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb;

    bool valid() const;
};

RgbImage MakeRgbImage(int width, int height, std::vector<uint8_t> rgb);
RgbImage CropRgb(const std::vector<uint8_t>& screenRgb, int screenWidth, int screenHeight, int left, int top, int width, int height);
bool WritePngRgb(const std::string& path, const RgbImage& image);
std::optional<RgbImage> ReadImageRgb(const std::string& path);

}
