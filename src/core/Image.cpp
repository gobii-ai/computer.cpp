#include "computer_cpp/Image.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

#include <zlib.h>

namespace ComputerCpp::Image {
namespace {

constexpr std::array<uint8_t, 8> kPngSignature {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

void PutU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(value & 0xff));
}

uint32_t ReadU32(const std::vector<uint8_t>& data, size_t offset) {
    return (static_cast<uint32_t>(data[offset]) << 24) |
        (static_cast<uint32_t>(data[offset + 1]) << 16) |
        (static_cast<uint32_t>(data[offset + 2]) << 8) |
        static_cast<uint32_t>(data[offset + 3]);
}

void AppendChunk(std::vector<uint8_t>& png, const char type[4], const std::vector<uint8_t>& payload) {
    PutU32(png, static_cast<uint32_t>(payload.size()));
    const size_t typeOffset = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), payload.begin(), payload.end());
    const uLong crc = crc32(0L, png.data() + typeOffset, static_cast<uInt>(4 + payload.size()));
    PutU32(png, static_cast<uint32_t>(crc));
}

bool WriteFileBytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::filesystem::path outputPath(path);
    if (!outputPath.parent_path().empty()) {
        std::filesystem::create_directories(outputPath.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

std::optional<std::vector<uint8_t>> ReadFileBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

uint8_t Paeth(uint8_t left, uint8_t up, uint8_t upLeft) {
    const int p = static_cast<int>(left) + static_cast<int>(up) - static_cast<int>(upLeft);
    const int pa = std::abs(p - static_cast<int>(left));
    const int pb = std::abs(p - static_cast<int>(up));
    const int pc = std::abs(p - static_cast<int>(upLeft));
    if (pa <= pb && pa <= pc) return left;
    if (pb <= pc) return up;
    return upLeft;
}

std::optional<RgbImage> DecodePngRgb(const std::vector<uint8_t>& png) {
    if (png.size() < 33 || !std::equal(kPngSignature.begin(), kPngSignature.end(), png.begin())) {
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<uint8_t> idat;
    size_t offset = kPngSignature.size();
    while (offset + 12 <= png.size()) {
        const uint32_t length = ReadU32(png, offset);
        offset += 4;
        if (offset + 4 + length + 4 > png.size()) {
            return std::nullopt;
        }
        const std::string type(reinterpret_cast<const char*>(png.data() + offset), 4);
        offset += 4;
        const size_t payloadOffset = offset;
        offset += length;
        offset += 4; // CRC is ignored; zlib inflation and bounds checks catch corrupt data for this use.

        if (type == "IHDR") {
            if (length != 13) {
                return std::nullopt;
            }
            width = static_cast<int>(ReadU32(png, payloadOffset));
            height = static_cast<int>(ReadU32(png, payloadOffset + 4));
            const uint8_t bitDepth = png[payloadOffset + 8];
            const uint8_t colorType = png[payloadOffset + 9];
            const uint8_t compression = png[payloadOffset + 10];
            const uint8_t filter = png[payloadOffset + 11];
            const uint8_t interlace = png[payloadOffset + 12];
            if (width <= 0 || height <= 0 || bitDepth != 8 || compression != 0 || filter != 0 || interlace != 0) {
                return std::nullopt;
            }
            if (colorType == 2) {
                channels = 3;
            } else if (colorType == 6) {
                channels = 4;
            } else {
                return std::nullopt;
            }
        } else if (type == "IDAT") {
            idat.insert(
                idat.end(),
                png.begin() + static_cast<std::vector<uint8_t>::difference_type>(payloadOffset),
                png.begin() + static_cast<std::vector<uint8_t>::difference_type>(payloadOffset + length));
        } else if (type == "IEND") {
            break;
        }
    }
    if (width <= 0 || height <= 0 || channels == 0 || idat.empty()) {
        return std::nullopt;
    }

    const size_t stride = static_cast<size_t>(width * channels);
    const size_t rawSize = (stride + 1) * static_cast<size_t>(height);
    std::vector<uint8_t> raw(rawSize);
    uLongf destLen = static_cast<uLongf>(raw.size());
    if (uncompress(raw.data(), &destLen, idat.data(), static_cast<uLong>(idat.size())) != Z_OK || destLen != raw.size()) {
        return std::nullopt;
    }

    std::vector<uint8_t> rgb(static_cast<size_t>(width * height * 3));
    std::vector<uint8_t> previous(stride, 0);
    std::vector<uint8_t> reconstructed(stride, 0);
    size_t src = 0;
    for (int y = 0; y < height; ++y) {
        const uint8_t filter = raw[src++];
        for (size_t x = 0; x < stride; ++x) {
            const uint8_t value = raw[src + x];
            const uint8_t left = x >= static_cast<size_t>(channels) ? reconstructed[x - static_cast<size_t>(channels)] : 0;
            const uint8_t up = previous[x];
            const uint8_t upLeft = x >= static_cast<size_t>(channels) ? previous[x - static_cast<size_t>(channels)] : 0;
            switch (filter) {
                case 0: reconstructed[x] = value; break;
                case 1: reconstructed[x] = static_cast<uint8_t>(value + left); break;
                case 2: reconstructed[x] = static_cast<uint8_t>(value + up); break;
                case 3: reconstructed[x] = static_cast<uint8_t>(value + static_cast<uint8_t>((static_cast<int>(left) + static_cast<int>(up)) / 2)); break;
                case 4: reconstructed[x] = static_cast<uint8_t>(value + Paeth(left, up, upLeft)); break;
                default: return std::nullopt;
            }
        }
        for (int x = 0; x < width; ++x) {
            const size_t decoded = static_cast<size_t>(x * channels);
            const size_t out = static_cast<size_t>((y * width + x) * 3);
            rgb[out] = reconstructed[decoded];
            rgb[out + 1] = reconstructed[decoded + 1];
            rgb[out + 2] = reconstructed[decoded + 2];
        }
        previous.swap(reconstructed);
        std::fill(reconstructed.begin(), reconstructed.end(), 0);
        src += stride;
    }
    return MakeRgbImage(width, height, std::move(rgb));
}

} // namespace

bool RgbImage::valid() const {
    return width > 0 && height > 0 && rgb.size() == static_cast<size_t>(width * height * 3);
}

RgbImage MakeRgbImage(int width, int height, std::vector<uint8_t> rgb) {
    RgbImage image;
    image.width = width;
    image.height = height;
    image.rgb = std::move(rgb);
    if (!image.valid()) {
        image.width = 0;
        image.height = 0;
        image.rgb.clear();
    }
    return image;
}

RgbImage CropRgb(
    const std::vector<uint8_t>& screenRgb,
    int screenWidth,
    int screenHeight,
    int left,
    int top,
    int width,
    int height
) {
    if (screenWidth <= 0 || screenHeight <= 0 || screenRgb.size() != static_cast<size_t>(screenWidth * screenHeight * 3)) {
        return {};
    }
    width = std::clamp(width, 1, screenWidth);
    height = std::clamp(height, 1, screenHeight);
    left = std::clamp(left, 0, std::max(0, screenWidth - width));
    top = std::clamp(top, 0, std::max(0, screenHeight - height));
    RgbImage crop;
    crop.width = width;
    crop.height = height;
    crop.rgb.resize(static_cast<size_t>(width * height * 3));
    for (int y = 0; y < height; ++y) {
        size_t src = static_cast<size_t>(((top + y) * screenWidth + left) * 3);
        size_t dst = static_cast<size_t>(y * width * 3);
        std::copy(
            screenRgb.begin() + static_cast<long>(src),
            screenRgb.begin() + static_cast<long>(src + static_cast<size_t>(width * 3)),
            crop.rgb.begin() + static_cast<long>(dst)
        );
    }
    return crop;
}

bool WritePngRgb(const std::string& path, const RgbImage& image) {
    if (!image.valid()) {
        return false;
    }

    std::vector<uint8_t> scanlines;
    scanlines.reserve(static_cast<size_t>((image.width * 3 + 1) * image.height));
    for (int y = 0; y < image.height; ++y) {
        scanlines.push_back(0);
        const auto* row = image.rgb.data() + static_cast<size_t>(y * image.width * 3);
        scanlines.insert(scanlines.end(), row, row + static_cast<size_t>(image.width * 3));
    }

    std::vector<uint8_t> compressed(compressBound(static_cast<uLong>(scanlines.size())));
    uLongf compressedSize = static_cast<uLongf>(compressed.size());
    if (compress2(compressed.data(), &compressedSize, scanlines.data(), static_cast<uLong>(scanlines.size()), Z_BEST_SPEED) != Z_OK) {
        return false;
    }
    compressed.resize(static_cast<size_t>(compressedSize));

    std::vector<uint8_t> png(kPngSignature.begin(), kPngSignature.end());
    std::vector<uint8_t> ihdr;
    PutU32(ihdr, static_cast<uint32_t>(image.width));
    PutU32(ihdr, static_cast<uint32_t>(image.height));
    ihdr.push_back(8);
    ihdr.push_back(2);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    AppendChunk(png, "IHDR", ihdr);
    AppendChunk(png, "IDAT", compressed);
    AppendChunk(png, "IEND", {});

    return WriteFileBytes(path, png);
}

std::optional<RgbImage> ReadImageRgb(const std::string& path) {
    auto bytes = ReadFileBytes(path);
    if (!bytes) {
        return std::nullopt;
    }
    return DecodePngRgb(*bytes);
}

}
