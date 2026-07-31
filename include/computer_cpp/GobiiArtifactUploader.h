#pragma once

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <span>
#include <string>

namespace ComputerCpp {

class GobiiHttpTransport;

struct GobiiArtifactReference {
    std::string id;
    std::string mimeType;
};

class GobiiArtifactUploader {
public:
    virtual ~GobiiArtifactUploader() = default;
    virtual void Configure(
        const std::string& baseUrl,
        const std::string& accessToken) = 0;
    virtual void ClearAuthentication() = 0;
    virtual bool Available() const = 0;
    virtual bool Upload(
        const std::string& requestId,
        std::span<const std::uint8_t> bytes,
        const std::string& mimeType,
        GobiiArtifactReference& reference,
        std::string& error) = 0;
};

std::unique_ptr<GobiiArtifactUploader>
CreateDisabledGobiiArtifactUploader();

std::unique_ptr<GobiiArtifactUploader>
CreateGobiiArtifactUploader(
    std::shared_ptr<GobiiHttpTransport> transport);

bool PrepareGobiiMcpImages(
    nlohmann::json& jsonRpcResponse,
    const std::string& requestId,
    GobiiArtifactUploader& uploader,
    std::string& error);

} // namespace ComputerCpp
