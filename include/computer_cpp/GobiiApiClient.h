#pragma once

#include <map>
#include <memory>
#include <string>

namespace ComputerCpp {

struct GobiiHttpRequest {
    std::string method = "POST";
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
    long timeoutMs = 20000;
    size_t responseLimit = 1024 * 1024;
};

struct GobiiHttpResponse {
    long status = 0;
    std::string body;
    std::string error;
};

class GobiiHttpTransport {
public:
    virtual ~GobiiHttpTransport() = default;
    virtual GobiiHttpResponse Send(const GobiiHttpRequest& request) = 0;
};

std::unique_ptr<GobiiHttpTransport> CreateCurlGobiiHttpTransport();
bool GobiiWebSocketRuntimeSupported(std::string* error = nullptr);

} // namespace ComputerCpp
