#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace ComputerCpp::Inference {

// Exposed for validation and tests; ChatCompletion wraps this with config and transport.
nlohmann::json BuildChatRequestBody(const nlohmann::json& params, std::string* error = nullptr);
nlohmann::json BuildMessages(const nlohmann::json& params, std::string* error = nullptr);
bool BaseUrlAllowsMissingApiKey(const std::string& url);
nlohmann::json ResolveChatConfig(const nlohmann::json& params = nlohmann::json::object());
nlohmann::json ChatCompletion(const nlohmann::json& params);

}
