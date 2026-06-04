#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace ComputerCpp::Inference {

struct InferenceConfig {
    std::string profile;
    std::string provider;
    std::string baseUrl;
    std::string model;
    std::string apiKey;
    long timeoutMs = 180000;
    nlohmann::json defaultParams = nlohmann::json::object();
    nlohmann::json openRouterProvider = nlohmann::json::object();
};

InferenceConfig ResolveConfig(const nlohmann::json& params, std::string* error = nullptr);
std::string ChatUrl(const std::string& baseUrl);
bool IsMissingKey(const std::string& key);

} // namespace ComputerCpp::Inference
