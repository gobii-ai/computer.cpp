#include "computer_cpp/GobiiArtifactUploader.h"

#include "computer_cpp/GobiiApiClient.h"
#include "computer_cpp/Sha256.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace ComputerCpp {
namespace {

class DisabledUploader final : public GobiiArtifactUploader {
public:
    void Configure(
        const std::string&,
        const std::string&) override {}
    void ClearAuthentication() override {}
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

std::string ArtifactEndpoint(std::string baseUrl) {
    while (!baseUrl.empty() && baseUrl.back() == '/') {
        baseUrl.pop_back();
    }
    return baseUrl + "/api/computer/v1/artifacts/";
}

bool IsSha256(std::string_view value) {
    return value.size() == 64 &&
        std::all_of(
            value.begin(),
            value.end(),
            [](unsigned char ch) {
                return (ch >= '0' && ch <= '9') ||
                    (ch >= 'a' && ch <= 'f');
            });
}

class HttpArtifactUploader final : public GobiiArtifactUploader {
public:
    explicit HttpArtifactUploader(
        std::shared_ptr<GobiiHttpTransport> transport)
        : transport_(std::move(transport)) {}

    void Configure(
        const std::string& baseUrl,
        const std::string& accessToken) override {
        std::lock_guard lock(mutex_);
        endpoint_ = ArtifactEndpoint(baseUrl);
        accessToken_ = accessToken;
    }

    void ClearAuthentication() override {
        std::lock_guard lock(mutex_);
        endpoint_.clear();
        accessToken_.clear();
    }

    bool Available() const override {
        std::lock_guard lock(mutex_);
        return transport_ && !endpoint_.empty() &&
            !accessToken_.empty();
    }

    bool Upload(
        const std::string&,
        std::span<const std::uint8_t> bytes,
        const std::string& mimeType,
        GobiiArtifactReference& reference,
        std::string& error) override {
        error.clear();
        if (bytes.empty() || bytes.size() > 8 * 1024 * 1024) {
            error = "Gobii artifact is empty or too large";
            return false;
        }
        if (mimeType != "image/png" &&
            mimeType != "image/jpeg") {
            error = "Gobii artifact type is unsupported";
            return false;
        }
        std::shared_ptr<GobiiHttpTransport> transport;
        std::string endpoint;
        std::string accessToken;
        {
            std::lock_guard lock(mutex_);
            transport = transport_;
            endpoint = endpoint_;
            accessToken = accessToken_;
        }
        if (!transport || endpoint.empty() || accessToken.empty()) {
            error = "Gobii artifact upload is not authenticated";
            return false;
        }

        std::string boundary =
            "----------------computer-cpp-gobii-artifact";
        const std::string byteString(
            reinterpret_cast<const char*>(bytes.data()),
            bytes.size());
        while (byteString.find(boundary) != std::string::npos) {
            boundary.push_back('x');
        }
        const std::string extension =
            mimeType == "image/png" ? ".png" : ".jpg";
        std::string body;
        body.reserve(bytes.size() + 256);
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"file\"; "
            "filename=\"artifact" + extension + "\"\r\n";
        body += "Content-Type: " + mimeType + "\r\n\r\n";
        body.append(byteString);
        body += "\r\n--" + boundary + "--\r\n";

        GobiiHttpRequest request;
        request.url = endpoint;
        request.headers = {
            {"Accept", "application/json"},
            {"Authorization", "Bearer " + accessToken},
            {"Content-Type", "multipart/form-data; boundary=" +
                boundary},
        };
        request.body = std::move(body);
        request.timeoutMs = 20000;
        request.responseLimit = 64 * 1024;
        const GobiiHttpResponse response = transport->Send(request);
        if (!response.error.empty()) {
            error = "Gobii artifact upload failed: " +
                response.error;
            return false;
        }
        if (response.status != 201) {
            error = "Gobii artifact upload returned HTTP " +
                std::to_string(response.status);
            return false;
        }
        const nlohmann::json payload = nlohmann::json::parse(
            response.body, nullptr, false);
        if (payload.is_discarded() || !payload.is_object() ||
            !payload.contains("artifact_id") ||
            !payload["artifact_id"].is_string() ||
            payload["artifact_id"].get_ref<
                const std::string&>().empty() ||
            payload["artifact_id"].get_ref<
                const std::string&>().size() > 128 ||
            !payload.contains("mime_type") ||
            !payload["mime_type"].is_string() ||
            payload["mime_type"].get<std::string>() != mimeType ||
            !payload.contains("byte_count") ||
            !payload["byte_count"].is_number_unsigned() ||
            payload["byte_count"].get<size_t>() != bytes.size() ||
            !payload.contains("sha256") ||
            !payload["sha256"].is_string() ||
            !IsSha256(payload["sha256"].get_ref<
                const std::string&>()) ||
            payload["sha256"].get<std::string>() !=
                Sha256Hex(byteString) ||
            !payload.contains("expires_at") ||
            !payload["expires_at"].is_string() ||
            payload["expires_at"].get_ref<
                const std::string&>().empty()) {
            error = "Gobii artifact upload response is invalid";
            return false;
        }
        reference.id =
            payload["artifact_id"].get<std::string>();
        reference.mimeType = mimeType;
        return true;
    }

private:
    std::shared_ptr<GobiiHttpTransport> transport_;
    mutable std::mutex mutex_;
    std::string endpoint_;
    std::string accessToken_;
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

std::unique_ptr<GobiiArtifactUploader>
CreateGobiiArtifactUploader(
    std::shared_ptr<GobiiHttpTransport> transport) {
    return std::make_unique<HttpArtifactUploader>(
        std::move(transport));
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
            {"_gobii_artifact", {
                {"id", reference.id},
            }},
        };
    }
    return true;
}

} // namespace ComputerCpp
