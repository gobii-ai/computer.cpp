#include "LinuxPng.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>

namespace ComputerCpp::Platform::LinuxPng {
namespace {

constexpr int kRgbChannels = 3;

uint32_t Crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xffffffffu;
}

uint32_t Adler32(const std::vector<uint8_t>& data) {
    uint32_t a = 1;
    uint32_t b = 0;
    for (uint8_t byte : data) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void PutU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(value & 0xff));
}

void AppendChunk(std::vector<uint8_t>& png, const char type[4], const std::vector<uint8_t>& payload) {
    PutU32(png, static_cast<uint32_t>(payload.size()));
    const size_t typeOffset = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), payload.begin(), payload.end());
    PutU32(png, Crc32(png.data() + typeOffset, 4 + payload.size()));
}

bool HasRgbByteCount(int width, int height, const std::vector<uint8_t>& rgb) {
    return width > 0 &&
        height > 0 &&
        rgb.size() == static_cast<size_t>(width * height * kRgbChannels);
}

} // namespace

bool ReadPngSize(const std::string& path, int& width, int& height) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }

    std::array<unsigned char, 24> header {};
    in.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (in.gcount() != static_cast<std::streamsize>(header.size())) {
        return false;
    }

    constexpr std::array<unsigned char, 8> kSignature {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (!std::equal(kSignature.begin(), kSignature.end(), header.begin())) {
        return false;
    }

    width = static_cast<int>((header[16] << 24) | (header[17] << 16) | (header[18] << 8) | header[19]);
    height = static_cast<int>((header[20] << 24) | (header[21] << 16) | (header[22] << 8) | header[23]);
    return width > 0 && height > 0;
}

bool WritePngRgb(const std::string& path, int width, int height, const std::vector<uint8_t>& rgb) {
    if (!HasRgbByteCount(width, height, rgb)) {
        return false;
    }

    std::vector<uint8_t> scanlines;
    scanlines.reserve(static_cast<size_t>((width * kRgbChannels + 1) * height));
    for (int y = 0; y < height; ++y) {
        scanlines.push_back(0);
        const auto* row = rgb.data() + static_cast<size_t>(y * width * kRgbChannels);
        scanlines.insert(scanlines.end(), row, row + static_cast<size_t>(width * kRgbChannels));
    }

    std::vector<uint8_t> zlib;
    zlib.push_back(0x78);
    zlib.push_back(0x01);
    for (size_t offset = 0; offset < scanlines.size();) {
        const size_t blockSize = std::min<size_t>(65535, scanlines.size() - offset);
        const bool final = offset + blockSize == scanlines.size();
        zlib.push_back(final ? 0x01 : 0x00);
        const uint16_t len = static_cast<uint16_t>(blockSize);
        const uint16_t nlen = static_cast<uint16_t>(~len);
        zlib.push_back(static_cast<uint8_t>(len & 0xff));
        zlib.push_back(static_cast<uint8_t>((len >> 8) & 0xff));
        zlib.push_back(static_cast<uint8_t>(nlen & 0xff));
        zlib.push_back(static_cast<uint8_t>((nlen >> 8) & 0xff));
        zlib.insert(
            zlib.end(),
            scanlines.begin() + static_cast<std::vector<uint8_t>::difference_type>(offset),
            scanlines.begin() + static_cast<std::vector<uint8_t>::difference_type>(offset + blockSize)
        );
        offset += blockSize;
    }
    PutU32(zlib, Adler32(scanlines));

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<uint8_t> ihdr;
    PutU32(ihdr, static_cast<uint32_t>(width));
    PutU32(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8);
    ihdr.push_back(2);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    AppendChunk(png, "IHDR", ihdr);
    AppendChunk(png, "IDAT", zlib);
    AppendChunk(png, "IEND", {});

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return out.good();
}

std::vector<uint8_t> ResizeRgb(const std::vector<uint8_t>& src, int srcWidth, int srcHeight, int dstWidth, int dstHeight) {
    std::vector<uint8_t> dst(static_cast<size_t>(dstWidth * dstHeight * kRgbChannels));
    for (int y = 0; y < dstHeight; ++y) {
        const int sy = std::min(srcHeight - 1, static_cast<int>((static_cast<int64_t>(y) * srcHeight) / dstHeight));
        for (int x = 0; x < dstWidth; ++x) {
            const int sx = std::min(srcWidth - 1, static_cast<int>((static_cast<int64_t>(x) * srcWidth) / dstWidth));
            const size_t srcOffset = static_cast<size_t>((sy * srcWidth + sx) * kRgbChannels);
            const size_t dstOffset = static_cast<size_t>((y * dstWidth + x) * kRgbChannels);
            dst[dstOffset] = src[srcOffset];
            dst[dstOffset + 1] = src[srcOffset + 1];
            dst[dstOffset + 2] = src[srcOffset + 2];
        }
    }
    return dst;
}

bool WritePngRgbScaled(const std::string& path, int width, int height, const std::vector<uint8_t>& rgb, int maxDimension) {
    if (maxDimension <= 0 || std::max(width, height) <= maxDimension) {
        return WritePngRgb(path, width, height, rgb);
    }

    const double scale = static_cast<double>(maxDimension) / static_cast<double>(std::max(width, height));
    const int scaledWidth = std::max(1, static_cast<int>(std::round(static_cast<double>(width) * scale)));
    const int scaledHeight = std::max(1, static_cast<int>(std::round(static_cast<double>(height) * scale)));
    return WritePngRgb(path, scaledWidth, scaledHeight, ResizeRgb(rgb, width, height, scaledWidth, scaledHeight));
}

} // namespace ComputerCpp::Platform::LinuxPng
