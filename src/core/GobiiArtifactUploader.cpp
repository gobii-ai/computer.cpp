#include "computer_cpp/GobiiArtifactUploader.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <vector>

namespace ComputerCpp {
namespace {

class DisabledUploader final : public GobiiArtifactUploader {
public:
    bool Available() const override { return false; }
    bool Upload(
        const std::string&,
        std::span<const std::uint8_t>,
        const std::string&,
        GobiiArtifactReference&,
        std::string& error
    ) override {
        error = "Gobii artifact upload is not configured";
        return false;
    }
};

int Base64Value(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

bool DecodeBase64(
    const std::string& encoded,
    std::vector<std::uint8_t>& decoded
) {
    decoded.clear();
    int value = 0;
    int bits = -8;
    for (unsigned char ch : encoded) {
        if (std::isspace(ch)) continue;
        if (ch == '=') break;
        const int digit = Base64Value(ch);
        if (digit < 0) return false;
        value = (value << 6) | digit;
        bits += 6;
        if (bits >= 0) {
            decoded.push_back(
                static_cast<std::uint8_t>((value >> bits) & 0xff));
            bits -= 8;
        }
    }
    return true;
}

} // namespace

std::unique_ptr<GobiiArtifactUploader>
CreateDisabledGobiiArtifactUploader() {
    return std::make_unique<DisabledUploader>();
}

bool PrepareGobiiMcpImages(
    nlohmann::json& response,
    const std::string& requestId,
    GobiiArtifactUploader& uploader,
    std::string& error
) {
    error.clear();
    if (!response.is_object() ||
        !response.contains("result") ||
        !response["result"].is_object() ||
        !response["result"].contains("content") ||
        !response["result"]["content"].is_array()) {
        return true;
    }
    for (auto& item : response["result"]["content"]) {
        if (!item.is_object() || item.value("type", "") != "image") {
            continue;
        }
        if (!item.contains("data") || !item["data"].is_string() ||
            !item.contains("mimeType") ||
            !item["mimeType"].is_string()) {
            error = "local MCP returned malformed image content";
            return false;
        }
        const std::string data = item["data"].get<std::string>();
        const std::string mime = item["mimeType"].get<std::string>();
#if defined(COMPUTER_CPP_GOBII_DEV_INLINE_IMAGES)
        if (data.size() <= 128 * 1024) {
            continue;
        }
#endif
        if (!uploader.Available()) {
            error = "artifact_upload_unavailable";
            return false;
        }
        std::vector<std::uint8_t> bytes;
        if (!DecodeBase64(data, bytes) || bytes.size() > 8 * 1024 * 1024) {
            error = "local MCP image is invalid or too large";
            return false;
        }
        GobiiArtifactReference reference;
        if (!uploader.Upload(
                requestId, bytes, mime, reference, error)) {
            return false;
        }
        item = {
            {"type", "resource"},
            {"resource", {
                {"uri", "gobii-artifact://" + reference.id},
                {"mimeType", reference.mimeType},
            }},
        };
    }
    return true;
}

} // namespace ComputerCpp
