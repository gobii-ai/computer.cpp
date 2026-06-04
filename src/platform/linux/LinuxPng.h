#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ComputerCpp::Platform::LinuxPng {

bool ReadPngSize(const std::string& path, int& width, int& height);
bool WritePngRgb(const std::string& path, int width, int height, const std::vector<uint8_t>& rgb);
bool WritePngRgbScaled(const std::string& path, int width, int height, const std::vector<uint8_t>& rgb, int maxDimension);
std::vector<uint8_t> ResizeRgb(const std::vector<uint8_t>& src, int srcWidth, int srcHeight, int dstWidth, int dstHeight);

} // namespace ComputerCpp::Platform::LinuxPng
