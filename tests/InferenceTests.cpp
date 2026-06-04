#include "TestSupport.h"

#include "computer_cpp/AppConfig.h"
#include "computer_cpp/Image.h"
#include "computer_cpp/InferenceClient.h"
#include "InferenceConfig.h"

#include <cassert>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void TestInferenceRequiresKeyForMainFabric() {
    auto response = ComputerCpp::Inference::ChatCompletion({
        {"baseUrl", "https://inference.example.test/v1"},
        {"model", "qwen36-27b"},
        {"apiKey", ""},
        {"messages", nlohmann::json::array({
            {{"role", "user"}, {"content", "ping"}}
        })}
    });
    assert(response["ok"] == false);
    assert(response["code"] == "missing_api_key");
}

void TestInferenceRejectsInvalidTimeout() {
    auto response = ComputerCpp::Inference::ChatCompletion({
        {"baseUrl", "https://inference.example.test/v1"},
        {"model", "qwen36-27b"},
        {"apiKey", ""},
        {"timeoutMs", "soon"},
        {"messages", nlohmann::json::array({
            {{"role", "user"}, {"content", "ping"}}
        })}
    });
    assert(response["ok"] == false);
    assert(response["code"] == "invalid_llm_request");

    std::string error;
    auto body = ComputerCpp::Inference::BuildChatRequestBody({
        {"timeoutMs", 2.5},
        {"messages", nlohmann::json::array({
            {{"role", "user"}, {"content", "ping"}}
        })}
    }, &error);
    assert(body.empty());
    assert(error == "llm_chat requires integer timeoutMs");
}

void TestInferenceLocalBaseUrlPolicy() {
    using ComputerCpp::Inference::BaseUrlAllowsMissingApiKey;

    assert(BaseUrlAllowsMissingApiKey("http://localhost:8000/v1"));
    assert(BaseUrlAllowsMissingApiKey("http://127.0.0.1:8000/v1"));
    assert(BaseUrlAllowsMissingApiKey("http://10.1.2.3/v1"));
    assert(BaseUrlAllowsMissingApiKey("http://172.16.0.4/v1"));
    assert(BaseUrlAllowsMissingApiKey("http://172.31.255.255/v1"));
    assert(BaseUrlAllowsMissingApiKey("http://192.168.4.5/v1"));
    assert(BaseUrlAllowsMissingApiKey("http://[::1]:8000/v1"));
    assert(!BaseUrlAllowsMissingApiKey("http://172.15.0.4/v1"));
    assert(!BaseUrlAllowsMissingApiKey("http://172.32.0.4/v1"));
    assert(!BaseUrlAllowsMissingApiKey("http://172.16x.0.4/v1"));
    assert(!BaseUrlAllowsMissingApiKey("https://inference.example.test/v1"));
}

void TestInferenceConfigFileProfiles() {
    ComputerCpp::AppConfig appConfig = ComputerCpp::DefaultAppConfig();
    appConfig.defaultProfile = "main";
    appConfig.providers["openrouter"].apiKey.clear();
    appConfig.providers["openrouter"].apiKey = "or-test-key";
    appConfig.profiles["main"].provider = "openrouter";
    appConfig.profiles["main"].model = "openrouter/auto";
    appConfig.profiles["main"].temperature = 0.1;
    appConfig.profiles["main"].maxOutputTokens = 333;
    appConfig.profiles["main"].params["temperature"] = 0.1;
    appConfig.profiles["main"].params["max_output_tokens"] = 333;
    appConfig.profiles["main"].openRouterProvider = {{"allow_fallbacks", false}, {"order", nlohmann::json::array({"openai"})}};
    std::string saveError;
    assert(ComputerCpp::SaveAppConfig(appConfig, &saveError));
    assert(saveError.empty());

    std::string error;
    auto config = ComputerCpp::Inference::ResolveConfig(nlohmann::json::object(), &error);
    assert(error.empty());
    assert(config.profile == "main");
    assert(config.provider == "openrouter");
    assert(config.baseUrl == "https://openrouter.ai/api/v1");
    assert(config.model == "openrouter/auto");
    assert(config.apiKey == "or-test-key");
    assert(config.defaultParams["temperature"] == 0.1);
    assert(config.defaultParams["max_output_tokens"] == 333);

    auto body = ComputerCpp::Inference::BuildChatRequestBody({
        {"messages", nlohmann::json::array({
            {{"role", "user"}, {"content", "ping"}}
        })}
    }, &error);
    assert(error.empty());
    assert(body["model"] == "openrouter/auto");
    assert(body["temperature"] == 0.1);
    assert(body["max_tokens"] == 333);
    assert(body["provider"]["allow_fallbacks"] == false);

    config = ComputerCpp::Inference::ResolveConfig({
        {"provider", "openai-compatible"},
        {"baseUrl", "https://generic.example.test/v1"},
        {"model", "explicit-model"},
        {"apiKey", "explicit-key"}
    }, &error);
    assert(error.empty());
    assert(config.provider == "openai-compatible");
    assert(config.baseUrl == "https://generic.example.test/v1");
    assert(config.model == "explicit-model");
    assert(config.apiKey == "explicit-key");
}

void TestInferenceRequestBodyNormalizesImages() {
    fs::path dir = ComputerCpp::Tests::MakeTempHome();
    fs::path imagePath = dir / "inference-image.png";
    auto image = ComputerCpp::Image::MakeRgbImage(24, 16, std::vector<uint8_t>(static_cast<size_t>(24 * 16 * 3), 128));
    assert(ComputerCpp::Image::WritePngRgb(imagePath.string(), image));

    std::string error;
    auto body = ComputerCpp::Inference::BuildChatRequestBody({
        {"model", "unit-model"},
        {"messages", nlohmann::json::array({
            {
                {"role", "user"},
                {"content", nlohmann::json::array({
                    {{"type", "text"}, {"text", "inspect"}},
                    {{"type", "image_path"}, {"path", imagePath.string()}}
                })}
            }
        })}
    }, &error);
    assert(error.empty());
    assert(body["model"] == "unit-model");
    const auto& content = body["messages"][0]["content"];
    assert(content.size() == 2);
    assert(content[1]["type"] == "image_url");
    std::string url = content[1]["image_url"]["url"];
    assert(url.rfind("data:image/png;base64,", 0) == 0);
    assert(body["stream"] == false);

    error.clear();
    auto appended = ComputerCpp::Inference::BuildChatRequestBody({
        {"model", "unit-model"},
        {"messages", nlohmann::json::array({
            {{"role", "user"}, {"content", "hello"}}
        })},
        {"imagePaths", nlohmann::json::array({imagePath.string()})}
    }, &error);
    assert(error.empty());
    const auto& appendedContent = appended["messages"][0]["content"];
    assert(appendedContent.size() == 2);
    assert(appendedContent[0]["type"] == "text");
    assert(appendedContent[0]["text"] == "hello");
    assert(appendedContent[1]["type"] == "image_url");

    error.clear();
    auto invalid = ComputerCpp::Inference::BuildChatRequestBody({{"messages", nlohmann::json::object()}}, &error);
    assert(invalid.empty());
    assert(error == "llm_chat requires messages array");
}

void TestInferenceRequestBodyForwardsChatOptions() {
    std::string error;
    auto body = ComputerCpp::Inference::BuildChatRequestBody({
        {"model", "unit-model"},
        {"messages", nlohmann::json::array({
            {{"role", "user"}, {"content", "ping"}}
        })},
        {"temperature", 0.25},
        {"top_p", 0.9},
        {"seed", 42},
        {"response_format", "json"}
    }, &error);

    assert(error.empty());
    assert(body["temperature"] == 0.25);
    assert(body["top_p"] == 0.9);
    assert(body["seed"] == 42);
    assert(body["response_format"]["type"] == "json_object");
}

} // namespace

namespace ComputerCpp::Tests {

void RunInferenceTests() {
    TestInferenceRequiresKeyForMainFabric();
    TestInferenceRejectsInvalidTimeout();
    TestInferenceLocalBaseUrlPolicy();
    TestInferenceConfigFileProfiles();
    TestInferenceRequestBodyNormalizesImages();
    TestInferenceRequestBodyForwardsChatOptions();
}

} // namespace ComputerCpp::Tests
