#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ComputerCpp {

struct LlmProviderConfig {
    std::string name;
    std::string type = "openai-compatible";
    std::string baseUrl = "http://127.0.0.1:8000/v1";
    std::string apiKey;
};

struct LlmProfileConfig {
    std::string name;
    std::string provider = "local";
    std::string model = "qwen36-27b";
    std::optional<double> temperature;
    std::optional<double> topP;
    std::optional<long> maxOutputTokens;
    std::optional<long> timeoutMs;
    nlohmann::json params = nlohmann::json::object();
    nlohmann::json openRouterProvider = nlohmann::json::object();
};

struct ServerAppConfig {
    std::string name;
    std::string displayName;
    std::string path;
};

struct ServerConfig {
    std::string host = "127.0.0.1";
    int port = 8787;
    std::string authToken;
    std::vector<std::string> allowedOrigins;
    std::map<std::string, ServerAppConfig> apps;
};

bool IsValidServerAppName(const std::string& name);

struct RecordingConfig {
    bool enabled = false;
    int retentionDays = 14;
};

struct GobiiConfig {
    std::string baseUrl = "https://gobii.ai";
    std::string deviceId;
    std::string deviceName;
    std::string assignedAgentId;
    std::string assignedAgentName;
    std::string requiredVersion;
    std::string updateRequiredInstalledVersion;
    bool autoConnect = true;
    bool startAtLogin = false;
    bool paused = false;
};

struct AppConfig {
    int version = 1;
    std::string defaultProfile = "main";
    std::map<std::string, LlmProviderConfig> providers;
    std::map<std::string, LlmProfileConfig> profiles;
    ServerConfig server;
    RecordingConfig recording;
    GobiiConfig gobii;
};

AppConfig DefaultAppConfig();
AppConfig LoadAppConfig(
    std::string* error = nullptr,
    std::vector<std::string>* warnings = nullptr);
bool SaveAppConfig(const AppConfig& config, std::string* error = nullptr);
bool AppConfigExists();

nlohmann::json AppConfigToJson(const AppConfig& config, bool redactSecrets = true);
std::string AppConfigToToml(const AppConfig& config);

bool SetProviderConfig(
    AppConfig& config,
    const std::string& name,
    const std::string& type,
    const std::string& baseUrl,
    std::string* error = nullptr);

bool SetProfileDefaultParam(LlmProfileConfig& profile, const std::string& key, const std::string& rawValue, std::string* error = nullptr);
bool ImportLegacyInferenceEnv(AppConfig& config, std::string* warning = nullptr, std::string* error = nullptr);

std::string NormalizeLlmProviderType(const std::string& value, std::string* error = nullptr);
std::string GenerateServerAuthToken();
bool EnsureServerAuthToken(AppConfig& config);
bool EnsureGobiiDesktopApp(AppConfig& config, std::string* error = nullptr);

} // namespace ComputerCpp
