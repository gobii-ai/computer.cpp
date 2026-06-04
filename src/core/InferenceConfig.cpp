#include "InferenceConfig.h"

#include "computer_cpp/AppConfig.h"
#include "computer_cpp/AppPaths.h"
#include "computer_cpp/InferenceClient.h"
#include "computer_cpp/StringUtils.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

namespace ComputerCpp::Inference {
namespace {

constexpr std::string_view kOpenRouterBaseUrl = "https://openrouter.ai/api/v1";
std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string HostFromUrl(const std::string& url) {
    std::string value = url;
    auto scheme = value.find("://");
    if (scheme != std::string::npos) {
        value = value.substr(scheme + 3);
    }
    auto slash = value.find('/');
    if (slash != std::string::npos) {
        value = value.substr(0, slash);
    }
    auto at = value.rfind('@');
    if (at != std::string::npos) {
        value = value.substr(at + 1);
    }
    if (value.size() >= 2 && value.front() == '[') {
        auto close = value.find(']');
        if (close != std::string::npos) {
            return Lower(value.substr(1, close - 1));
        }
    }
    auto colon = value.find(':');
    if (colon != std::string::npos) {
        value = value.substr(0, colon);
    }
    return Lower(value);
}

bool IsLocalUrl(const std::string& url) {
    std::string host = HostFromUrl(url);
    if (host.empty() || host == "localhost" || host == "::1" || host == "0:0:0:0:0:0:0:1") {
        return true;
    }
    if (host.rfind("127.", 0) == 0 || host.rfind("10.", 0) == 0 || host.rfind("192.168.", 0) == 0) {
        return true;
    }
    if (host.rfind("172.", 0) == 0) {
        std::istringstream in(host);
        std::string first;
        std::string second;
        if (std::getline(in, first, '.') && std::getline(in, second, '.')) {
            int octet = 0;
            auto* begin = second.data();
            auto* end = begin + second.size();
            auto [ptr, ec] = std::from_chars(begin, end, octet);
            return ec == std::errc{} && ptr == end && octet >= 16 && octet <= 31;
        }
    }
    return false;
}

bool IsOpenRouterUrl(const std::string& url) {
    return HostFromUrl(url) == "openrouter.ai";
}

std::string NormalizeBaseUrl(std::string baseUrl) {
    baseUrl = Trim(baseUrl);
    while (!baseUrl.empty() && baseUrl.back() == '/') {
        baseUrl.pop_back();
    }
    if (baseUrl.empty()) {
        baseUrl = "http://127.0.0.1:8000/v1";
    }
    return baseUrl;
}

std::optional<std::string> StringParam(const nlohmann::json& params, std::initializer_list<const char*> keys, std::string* error) {
    for (const char* key : keys) {
        if (!params.contains(key)) {
            continue;
        }
        const auto& value = params.at(key);
        if (!value.is_string()) {
            if (error) {
                *error = std::string("llm_chat requires string ") + key;
            }
            return std::string();
        }
        return value.get<std::string>();
    }
    return std::nullopt;
}

bool ContainsAny(const nlohmann::json& params, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        if (params.contains(key)) {
            return true;
        }
    }
    return false;
}

std::optional<long> LongParam(const nlohmann::json& params, const char* key, long fallback) {
    if (!params.contains(key)) {
        return fallback;
    }
    const auto& value = params.at(key);
    if (!value.is_number()) {
        return std::nullopt;
    }
    double number = value.get<double>();
    if (!std::isfinite(number) ||
        number < static_cast<double>(std::numeric_limits<long>::min()) ||
        number > static_cast<double>(std::numeric_limits<long>::max()) ||
        std::trunc(number) != number) {
        return std::nullopt;
    }
    return static_cast<long>(number);
}

std::string ConfiguredApiKey(const LlmProviderConfig& provider) {
    return provider.apiKey;
}

} // namespace

bool IsMissingKey(const std::string& key) {
    std::string lowered = Lower(Trim(key));
    return lowered.empty() || lowered == "empty" || lowered == "none" || lowered == "null";
}

std::string ChatUrl(const std::string& baseUrl) {
    if (baseUrl.size() >= 17 && baseUrl.substr(baseUrl.size() - 17) == "/chat/completions") {
        return baseUrl;
    }
    return baseUrl + "/chat/completions";
}

bool BaseUrlAllowsMissingApiKey(const std::string& url) {
    return IsLocalUrl(NormalizeBaseUrl(url));
}

InferenceConfig ResolveConfig(const nlohmann::json& params, std::string* error) {
    InferenceConfig config;
    std::string configError;
    AppConfig appConfig = LoadAppConfig(&configError);
    if (!configError.empty()) {
        if (error) {
            *error = configError;
        }
        return config;
    }

    auto profileParam = StringParam(params, {"profile"}, error);
    if (error && !error->empty()) {
        return config;
    }
    config.profile = profileParam.value_or(appConfig.defaultProfile);
    if (IsBlank(config.profile)) {
        config.profile = appConfig.defaultProfile;
    }
    auto profileIt = appConfig.profiles.find(config.profile);
    if (profileIt == appConfig.profiles.end()) {
        if (error) {
            *error = "llm_chat profile '" + config.profile + "' was not found in " + ConfigPath().string();
        }
        return config;
    }
    const LlmProfileConfig& profile = profileIt->second;
    auto providerIt = appConfig.providers.find(profile.provider);
    if (providerIt == appConfig.providers.end()) {
        if (error) {
            *error = "llm_chat profile '" + config.profile + "' references unknown provider '" + profile.provider + "'";
        }
        return config;
    }
    LlmProviderConfig provider = providerIt->second;

    if (params.contains("provider") && params.at("provider").is_string()) {
        std::string providerError;
        std::string overrideType = NormalizeLlmProviderType(params.at("provider").get<std::string>(), &providerError);
        if (!providerError.empty()) {
            if (error) {
                *error = "llm_chat " + providerError;
            }
            return config;
        }
        provider.type = overrideType;
        if (!ContainsAny(params, {"baseUrl", "base_url"})) {
            provider.baseUrl = provider.type == "openrouter" ? std::string(kOpenRouterBaseUrl) : provider.baseUrl;
        }
    } else if (params.contains("provider") && !params.at("provider").is_object()) {
        if (error) {
            *error = "llm_chat provider must be a string provider type or an OpenRouter provider preferences object";
        }
        return config;
    }

    auto baseUrlParam = StringParam(params, {"baseUrl", "base_url"}, error);
    if (error && !error->empty()) {
        return config;
    }
    config.baseUrl = NormalizeBaseUrl(baseUrlParam.value_or(provider.baseUrl));
    bool openRouterProvider = provider.type == "openrouter" || IsOpenRouterUrl(config.baseUrl);
    config.provider = openRouterProvider ? "openrouter" : "openai-compatible";

    auto modelParam = StringParam(params, {"model"}, error);
    if (error && !error->empty()) {
        return config;
    }
    config.model = modelParam.value_or(profile.model);
    if (IsBlank(config.model)) {
        config.model = openRouterProvider ? "openrouter/auto" : "qwen36-27b";
    }

    config.apiKey = ConfiguredApiKey(provider);
    auto apiKeyParam = StringParam(params, {"apiKey", "api_key"}, error);
    if (error && !error->empty()) {
        return config;
    }
    if (apiKeyParam.has_value()) {
        config.apiKey = *apiKeyParam;
    }

    config.defaultParams = profile.params.is_object() ? profile.params : nlohmann::json::object();
    config.openRouterProvider = profile.openRouterProvider.is_object() ? profile.openRouterProvider : nlohmann::json::object();

    long timeoutDefault = profile.timeoutMs.value_or(180000);
    auto timeoutMs = LongParam(params, "timeoutMs", timeoutDefault);
    if (!timeoutMs) {
        if (error) {
            *error = "llm_chat requires integer timeoutMs";
        }
        return config;
    }
    config.timeoutMs = std::clamp<long>(*timeoutMs, 1, 900000);
    return config;
}

} // namespace ComputerCpp::Inference
