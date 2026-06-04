#pragma once

#include <curl/curl.h>

#include <string>

namespace ComputerCpp {

class CurlHandle {
public:
    CurlHandle() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_ = curl_easy_init();
    }

    ~CurlHandle() {
        if (curl_) {
            curl_easy_cleanup(curl_);
        }
    }

    CurlHandle(const CurlHandle&) = delete;
    CurlHandle& operator=(const CurlHandle&) = delete;

    CURL* get() const {
        return curl_;
    }

    bool valid() const {
        return curl_ != nullptr;
    }

private:
    CURL* curl_ = nullptr;
};

class CurlHeaders {
public:
    CurlHeaders() = default;

    ~CurlHeaders() {
        if (headers_) {
            curl_slist_free_all(headers_);
        }
    }

    CurlHeaders(const CurlHeaders&) = delete;
    CurlHeaders& operator=(const CurlHeaders&) = delete;

    bool append(const std::string& header) {
        curl_slist* updated = curl_slist_append(headers_, header.c_str());
        if (!updated) {
            return false;
        }
        headers_ = updated;
        return true;
    }

    curl_slist* get() const {
        return headers_;
    }

private:
    curl_slist* headers_ = nullptr;
};

} // namespace ComputerCpp
