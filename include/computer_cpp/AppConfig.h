#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ComputerCpp {

class CredentialStore;

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
    std::optional<int> port;
};

enum class ServerAuthTokenStorage {
    None,
    LegacyPlaintext,
    System,
};

struct ServerConfig {
    std::string host = "0.0.0.0";
    int basePort = 8787;
    std::string authToken;
    ServerAuthTokenStorage authTokenStorage = ServerAuthTokenStorage::None;
    bool authTokenDirty = false;
    std::vector<std::string> allowedOrigins;
    std::map<std::string, ServerAppConfig> apps;
};

struct ServerPortPlan {
    std::map<std::string, int> ports;
    std::map<std::string, std::string> errors;
};

ServerPortPlan PlanServerPorts(
    const ServerConfig& server,
    const std::set<std::string>& appNames,
    const std::set<int>& occupiedPorts,
    const std::function<bool(int)>& portAvailable);

struct RecordingConfig {
    bool enabled = false;
    int retentionDays = 14;
};

struct AppConfig {
    int version = 2;
    std::string defaultProfile = "main";
    std::map<std::string, LlmProviderConfig> providers;
    std::map<std::string, LlmProfileConfig> profiles;
    ServerConfig server;
    RecordingConfig recording;
};

AppConfig DefaultAppConfig();
AppConfig LoadAppConfig(std::string* error = nullptr);
bool SaveAppConfig(
    const AppConfig& config,
    std::string* error = nullptr,
    CredentialStore* credentialStore = nullptr);
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
bool GenerateServerAuthToken(std::string& token, std::string* error = nullptr);
bool EnsureServerAuthToken(
    AppConfig& config,
    std::string* error = nullptr,
    CredentialStore* credentialStore = nullptr);

} // namespace ComputerCpp
