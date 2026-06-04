#include "computer_cpp/InferenceClient.h"
#include "CurlHandle.h"
#include "InferenceConfig.h"

#include <curl/curl.h>

#include <array>
#include <string>
#include <string_view>
#include <utility>

using json = nlohmann::json;

namespace ComputerCpp::Inference {
namespace {

struct AssistantMessage {
    std::string content;
    std::string finishReason;
    std::string reasoningContent;
    json message = json::object();
};

constexpr auto kForwardedChatParams = std::to_array<std::string_view>({
    "models",
    "max_tokens",
    "max_completion_tokens",
    "temperature",
    "top_p",
    "top_k",
    "min_p",
    "top_a",
    "tools",
    "tool_choice",
    "parallel_tool_calls",
    "chat_template_kwargs",
    "frequency_penalty",
    "presence_penalty",
    "repetition_penalty",
    "seed",
    "logit_bias",
    "logprobs",
    "top_logprobs",
    "stop",
    "reasoning",
    "reasoning_effort",
    "include_reasoning",
    "verbosity",
    "web_search_options",
});

void ApplyResponseFormat(json& body, const json& value) {
    if (value.is_string() && value == "json") {
        body["response_format"] = {{"type", "json_object"}};
    } else {
        body["response_format"] = value;
    }
}

void ApplyChatOptions(json& body, const json& options) {
    if (!options.is_object()) {
        return;
    }
    if (options.contains("max_output_tokens") &&
        !options.contains("max_tokens") &&
        !options.contains("max_completion_tokens")) {
        body["max_tokens"] = options["max_output_tokens"];
    }
    for (const auto key : kForwardedChatParams) {
        const std::string keyString(key);
        if (options.contains(keyString)) {
            body[keyString] = options[keyString];
        }
    }
    if (options.contains("response_format")) {
        ApplyResponseFormat(body, options["response_format"]);
    }
}

json BuildRequestBody(const json& params, const InferenceConfig& config, std::string* error) {
    json body;
    body["model"] = config.model;
    body["messages"] = BuildMessages(params, error);
    if (error && !error->empty()) {
        return json::object();
    }

    ApplyChatOptions(body, config.defaultParams);
    if (config.provider == "openrouter" && !config.openRouterProvider.empty()) {
        body["provider"] = config.openRouterProvider;
    }
    ApplyChatOptions(body, params);
    if (params.contains("provider") && params["provider"].is_object()) {
        body["provider"] = params["provider"];
    }
    if (params.contains("openRouterProvider") && params["openRouterProvider"].is_object()) {
        body["provider"] = params["openRouterProvider"];
    }
    body["stream"] = false;
    return body;
}

size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

json Error(const std::string& message, const std::string& code) {
    return {
        {"ok", false},
        {"error", message},
        {"code", code}
    };
}

json Ok(json data) {
    return {
        {"ok", true},
        {"data", std::move(data)}
    };
}

std::string Truncate(std::string value, size_t limit = 4000) {
    if (value.size() <= limit) {
        return value;
    }
    value.resize(limit);
    value += "...";
    return value;
}

AssistantMessage ExtractAssistantMessage(const json& response) {
    AssistantMessage out;
    if (!response.contains("choices") || !response["choices"].is_array() || response["choices"].empty()) {
        return out;
    }

    const auto& choice = response["choices"][0];
    if (choice.contains("finish_reason") && choice["finish_reason"].is_string()) {
        out.finishReason = choice["finish_reason"].get<std::string>();
    }
    if (choice.contains("message")) {
        out.message = choice["message"];
        if (out.message.contains("content") && out.message["content"].is_string()) {
            out.content = out.message["content"].get<std::string>();
        } else if (out.message.contains("content") && out.message["content"].is_array()) {
            for (const auto& item : out.message["content"]) {
                if (item.is_string()) {
                    out.content += item.get<std::string>();
                } else if (item.is_object() && item.contains("text") && item["text"].is_string()) {
                    out.content += item["text"].get<std::string>();
                }
            }
        }
        if (out.message.contains("reasoning_content") && out.message["reasoning_content"].is_string()) {
            out.reasoningContent = out.message["reasoning_content"].get<std::string>();
        }
        return out;
    }

    if (choice.contains("text") && choice["text"].is_string()) {
        out.content = choice["text"].get<std::string>();
        out.message = {{"role", "assistant"}, {"content", out.content}};
    }
    return out;
}

} // namespace

json BuildChatRequestBody(const json& params, std::string* error) {
    InferenceConfig config = ResolveConfig(params, error);
    if (error && !error->empty()) {
        return json::object();
    }
    return BuildRequestBody(params, config, error);
}

json ResolveChatConfig(const json& params) {
    std::string error;
    InferenceConfig config = ResolveConfig(params, &error);
    if (!error.empty()) {
        return Error(error, "invalid_llm_config");
    }
    return Ok({
        {"profile", config.profile},
        {"provider", config.provider},
        {"baseUrl", config.baseUrl},
        {"model", config.model},
        {"hasApiKey", !IsMissingKey(config.apiKey)},
        {"timeoutMs", config.timeoutMs},
        {"defaultParams", config.defaultParams},
        {"openRouterProvider", config.openRouterProvider},
    });
}

json ChatCompletion(const json& params) {
    std::string configError;
    InferenceConfig config = ResolveConfig(params, &configError);
    if (!configError.empty()) {
        return Error(configError, "invalid_llm_request");
    }
    if (IsMissingKey(config.apiKey) && !BaseUrlAllowsMissingApiKey(config.baseUrl)) {
        return Error(
            "main-fabric inference requires an API key before model calls (base_url=" +
                config.baseUrl + ", model=" + config.model + ")",
            "missing_api_key"
        );
    }

    std::string buildError;
    json body = BuildChatRequestBody(params, &buildError);
    if (!buildError.empty()) {
        return Error(buildError, "invalid_llm_request");
    }

    CurlHandle curl;
    if (!curl.valid()) {
        return Error("could not initialize libcurl", "curl_init_failed");
    }

    std::string responseBody;
    std::string requestBody = body.dump();
    std::string url = ChatUrl(config.baseUrl);

    CurlHeaders headers;
    if (!headers.append("Content-Type: application/json")) {
        return Error("could not allocate libcurl headers", "curl_init_failed");
    }
    if (!IsMissingKey(config.apiKey)) {
        if (!headers.append("Authorization: Bearer " + config.apiKey)) {
            return Error("could not allocate libcurl headers", "curl_init_failed");
        }
    }

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, requestBody.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(requestBody.size()));
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, config.timeoutMs);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "computer.cpp/0.1");

    CURLcode code = curl_easy_perform(curl.get());
    long httpCode = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &httpCode);

    if (code != CURLE_OK) {
        return Error(curl_easy_strerror(code), "curl_failed");
    }
    if (httpCode < 200 || httpCode >= 300) {
        auto error = Error("inference endpoint returned HTTP " + std::to_string(httpCode), "inference_http_error");
        error["data"] = {
            {"httpStatus", httpCode},
            {"body", Truncate(responseBody)}
        };
        return error;
    }

    json parsed = json::parse(responseBody, nullptr, false);
    if (parsed.is_discarded()) {
        auto error = Error("inference endpoint returned invalid JSON", "inference_bad_json");
        error["data"] = {{"body", Truncate(responseBody)}};
        return error;
    }

    auto assistant = ExtractAssistantMessage(parsed);

    return Ok({
        {"profile", config.profile},
        {"provider", config.provider},
        {"baseUrl", config.baseUrl},
        {"model", config.model},
        {"content", assistant.content},
        {"finishReason", assistant.finishReason},
        {"message", assistant.message},
        {"reasoningContent", assistant.reasoningContent},
        {"usage", parsed.value("usage", json::object())},
        {"raw", parsed}
    });
}

}
