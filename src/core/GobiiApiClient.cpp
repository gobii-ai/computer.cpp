#include "computer_cpp/GobiiApiClient.h"

#include "CurlHandle.h"

#include <algorithm>
#include <curl/curl.h>
#include <memory>
#include <string_view>

namespace ComputerCpp {
namespace {

struct ResponseBuffer {
    std::string body;
    size_t limit = 0;
    bool exceeded = false;
};

size_t WriteResponse(
    char* data,
    size_t size,
    size_t count,
    void* userData
) {
    auto* buffer = static_cast<ResponseBuffer*>(userData);
    const size_t bytes = size * count;
    if (bytes > buffer->limit ||
        buffer->body.size() > buffer->limit - bytes) {
        buffer->exceeded = true;
        return 0;
    }
    buffer->body.append(data, bytes);
    return bytes;
}

class CurlGobiiHttpTransport final : public GobiiHttpTransport {
public:
    GobiiHttpResponse Send(const GobiiHttpRequest& request) override {
        GobiiHttpResponse response;
        CurlHandle curl;
        if (!curl.valid()) {
            response.error = "could not initialize HTTP transport";
            response.errorType = GobiiHttpResponse::ErrorType::Transport;
            return response;
        }
        ResponseBuffer buffer;
        buffer.limit = request.responseLimit;
        CurlHeaders headers;
        for (const auto& [name, value] : request.headers) {
            if (!headers.append(name + ": " + value)) {
                response.error = "could not prepare HTTP headers";
                response.errorType =
                    GobiiHttpResponse::ErrorType::Transport;
                return response;
            }
        }
        curl_easy_setopt(curl.get(), CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, request.method.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, request.body.data());
        curl_easy_setopt(
            curl.get(),
            CURLOPT_POSTFIELDSIZE_LARGE,
            static_cast<curl_off_t>(request.body.size()));
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, request.timeoutMs);
        curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 10000L);
        curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteResponse);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &buffer);
        const CURLcode code = curl_easy_perform(curl.get());
        if (code != CURLE_OK) {
            response.error = buffer.exceeded
                ? "HTTP response exceeded size limit"
                : curl_easy_strerror(code);
            response.errorType =
                code == CURLE_OPERATION_TIMEDOUT
                ? GobiiHttpResponse::ErrorType::Timeout
                : GobiiHttpResponse::ErrorType::Transport;
            return response;
        }
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.status);
        response.body = std::move(buffer.body);
        return response;
    }
};

} // namespace

std::unique_ptr<GobiiHttpTransport> CreateCurlGobiiHttpTransport() {
    return std::make_unique<CurlGobiiHttpTransport>();
}

bool GobiiWebSocketRuntimeSupported(std::string* error) {
    if (error) {
        error->clear();
    }
#if LIBCURL_VERSION_NUM < 0x075600
    if (error) {
        *error = "libcurl headers do not provide WebSocket support";
    }
    return false;
#else
    const curl_version_info_data* info =
        curl_version_info(CURLVERSION_NOW);
    if (!info) {
        if (error) {
            *error = "could not inspect installed libcurl";
        }
        return false;
    }
    bool hasWs = false;
    bool hasWss = false;
    if (info->protocols) {
        for (const char* const* protocol = info->protocols;
             *protocol;
             ++protocol) {
            hasWs = hasWs || std::string_view(*protocol) == "ws";
            hasWss = hasWss || std::string_view(*protocol) == "wss";
        }
    }
    if (!hasWs || !hasWss) {
        if (error) {
            *error =
                "installed libcurl does not enable ws/wss protocols";
        }
        return false;
    }
#ifdef CURL_VERSION_WEBSOCKETS
    if ((info->features & CURL_VERSION_WEBSOCKETS) == 0) {
        if (error) {
            *error =
                "installed libcurl does not provide WebSocket support";
        }
        return false;
    }
#else
    if (info->version_num < 0x075600) {
        if (error) {
            *error =
                "installed libcurl predates WebSocket support";
        }
        return false;
    }
#endif
    return true;
#endif
}

} // namespace ComputerCpp
