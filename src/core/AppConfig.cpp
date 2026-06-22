#include "computer_cpp/AppConfig.h"

#include "computer_cpp/AppPaths.h"
#include "computer_cpp/StringUtils.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string_view>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ComputerCpp {
namespace {

constexpr std::string_view kOpenRouterBaseUrl = "https://openrouter.ai/api/v1";

std::string EnvFirst(std::initializer_list<const char*> names) {
    for (const char* name : names) {
        if (const char* raw = std::getenv(name)) {
            std::string value = Trim(raw);
            if (!value.empty()) {
                return value;
            }
        }
    }
    return {};
}

bool IsBareTomlKey(const std::string& key) {
    if (key.empty()) {
        return false;
    }
    return std::all_of(key.begin(), key.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_' || ch == '-';
    });
}

std::string TomlStringLiteral(const std::string& value) {
    std::string out = "\"";
    for (char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    out += "\\u00";
                    constexpr char digits[] = "0123456789ABCDEF";
                    out += digits[(static_cast<unsigned char>(ch) >> 4) & 0x0F];
                    out += digits[static_cast<unsigned char>(ch) & 0x0F];
                } else {
                    out += ch;
                }
        }
    }
    out += "\"";
    return out;
}

std::string TomlKey(const std::string& key) {
    return IsBareTomlKey(key) ? key : TomlStringLiteral(key);
}

std::string TomlTablePath(std::initializer_list<std::string> parts) {
    std::string out = "[";
    bool first = true;
    for (const auto& part : parts) {
        if (!first) {
            out += ".";
        }
        out += TomlKey(part);
        first = false;
    }
    out += "]";
    return out;
}

std::string TomlKeyName(const toml::key& key) {
    return std::string(key.data(), key.length());
}

std::string TomlString(const toml::table& table, const char* key, const std::string& fallback = {}) {
    if (auto value = table[key].value<std::string>()) {
        return *value;
    }
    return fallback;
}

std::vector<std::string> TomlStringArray(const toml::table& table, const char* key) {
    std::vector<std::string> out;
    if (auto array = table[key].as_array()) {
        for (const auto& item : *array) {
            if (auto value = item.value<std::string>()) {
                out.push_back(*value);
            }
        }
    }
    return out;
}

json TomlNodeToJson(const toml::node& node) {
    if (auto value = node.as_string()) {
        return value->get();
    }
    if (auto value = node.as_boolean()) {
        return value->get();
    }
    if (auto value = node.as_integer()) {
        return value->get();
    }
    if (auto value = node.as_floating_point()) {
        return value->get();
    }
    if (auto array = node.as_array()) {
        json out = json::array();
        for (const auto& item : *array) {
            out.push_back(TomlNodeToJson(item));
        }
        return out;
    }
    if (auto table = node.as_table()) {
        json out = json::object();
        for (const auto& [key, value] : *table) {
            out[TomlKeyName(key)] = TomlNodeToJson(value);
        }
        return out;
    }
    return {};
}

std::string JsonToTomlValue(const json& value);

std::string JsonObjectToInlineToml(const json& value) {
    std::string out = "{ ";
    bool first = true;
    for (const auto& [key, item] : value.items()) {
        if (!first) {
            out += ", ";
        }
        out += TomlKey(key);
        out += " = ";
        out += JsonToTomlValue(item);
        first = false;
    }
    out += " }";
    return out;
}

std::string JsonToTomlValue(const json& value) {
    if (value.is_string()) {
        return TomlStringLiteral(value.get<std::string>());
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<long long>());
    }
    if (value.is_number_unsigned()) {
        return std::to_string(value.get<unsigned long long>());
    }
    if (value.is_number_float()) {
        std::ostringstream out;
        out << value.get<double>();
        return out.str();
    }
    if (value.is_array()) {
        std::string out = "[";
        for (size_t i = 0; i < value.size(); ++i) {
            if (i > 0) {
                out += ", ";
            }
            out += JsonToTomlValue(value[i]);
        }
        out += "]";
        return out;
    }
    if (value.is_object()) {
        return JsonObjectToInlineToml(value);
    }
    return TomlStringLiteral("");
}

bool IsAllowedProviderType(const std::string& type) {
    return type == "openrouter" || type == "openai-compatible";
}

void AddProfileScalarParams(LlmProfileConfig& profile) {
    if (profile.temperature.has_value()) {
        profile.params["temperature"] = *profile.temperature;
    }
    if (profile.topP.has_value()) {
        profile.params["top_p"] = *profile.topP;
    }
    if (profile.maxOutputTokens.has_value()) {
        profile.params["max_output_tokens"] = *profile.maxOutputTokens;
    }
}

void RestrictConfigFilePermissions(const fs::path& path) {
#if defined(__unix__) || defined(__APPLE__)
    std::error_code ec;
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    ::chmod(path.c_str(), S_IRUSR | S_IWUSR);
#else
    (void)path;
#endif
}

bool WriteConfigFile(const fs::path& path, const std::string& text, std::string* error) {
    EnsureDirectory(path.parent_path());
    fs::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out) {
            if (error) {
                *error = "could not write " + temp.string();
            }
            return false;
        }
        out << text;
    }
    RestrictConfigFilePermissions(temp);
    std::error_code ec;
    fs::rename(temp, path, ec);
    if (ec) {
        fs::remove(path, ec);
        ec.clear();
        fs::rename(temp, path, ec);
    }
    if (ec) {
        if (error) {
            *error = "could not replace " + path.string() + ": " + ec.message();
        }
        return false;
    }
    RestrictConfigFilePermissions(path);
    return true;
}

std::optional<long> ParseLong(const std::string& raw) {
    std::string value = Trim(raw);
    long out = 0;
    auto* begin = value.data();
    auto* end = begin + value.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return out;
}

std::optional<double> ParseDouble(const std::string& raw) {
    char* end = nullptr;
    std::string value = Trim(raw);
    double out = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0' || !std::isfinite(out)) {
        return std::nullopt;
    }
    return out;
}

} // namespace

std::string GenerateServerAuthToken() {
    std::array<unsigned char, 32> bytes {};
    bool filled = false;
    {
        std::ifstream random("/dev/urandom", std::ios::binary);
        if (random.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
            filled = true;
        }
    }
    if (!filled) {
        std::random_device random;
        for (unsigned char& byte : bytes) {
            byte = static_cast<unsigned char>(random() & 0xFF);
        }
    }

    static constexpr char hex[] = "0123456789abcdef";
    std::string token;
    token.reserve(bytes.size() * 2);
    for (unsigned char byte : bytes) {
        token.push_back(hex[(byte >> 4) & 0x0F]);
        token.push_back(hex[byte & 0x0F]);
    }
    return token;
}

bool EnsureServerAuthToken(AppConfig& config) {
    if (!Trim(config.server.authToken).empty()) {
        return false;
    }
    config.server.authToken = GenerateServerAuthToken();
    return true;
}

std::string NormalizeLlmProviderType(const std::string& value, std::string* error) {
    std::string provider = Lowercase(Trim(value));
    if (provider.empty() || provider == "auto") {
        return "openai-compatible";
    }
    if (provider == "openrouter" || provider == "open-router") {
        return "openrouter";
    }
    if (provider == "openai-compatible" ||
        provider == "openai_compatible" ||
        provider == "openai" ||
        provider == "compatible" ||
        provider == "generic" ||
        provider == "custom") {
        return "openai-compatible";
    }
    if (error) {
        *error = "provider type must be openrouter or openai-compatible";
    }
    return {};
}

AppConfig DefaultAppConfig() {
    AppConfig config;

    LlmProviderConfig local;
    local.name = "local";
    local.type = "openai-compatible";
    local.baseUrl = "http://127.0.0.1:8000/v1";
    config.providers[local.name] = local;

    LlmProviderConfig openrouter;
    openrouter.name = "openrouter";
    openrouter.type = "openrouter";
    openrouter.baseUrl = std::string(kOpenRouterBaseUrl);
    config.providers[openrouter.name] = openrouter;

    LlmProfileConfig main;
    main.name = "main";
    main.provider = "local";
    main.model = "qwen36-27b";
    main.timeoutMs = 180000;
    config.profiles[main.name] = main;

    LlmProfileConfig router;
    router.name = "openrouter";
    router.provider = "openrouter";
    router.model = "openrouter/auto";
    router.timeoutMs = 180000;
    config.profiles[router.name] = router;

    return config;
}

bool AppConfigExists() {
    std::error_code ec;
    return fs::exists(ConfigPath(), ec) && !ec;
}

AppConfig LoadAppConfig(std::string* error) {
    AppConfig config = DefaultAppConfig();
    fs::path path = ConfigPath();
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return config;
    }
    RestrictConfigFilePermissions(path);

    toml::table parsed;
    try {
        parsed = toml::parse_file(path.string());
    } catch (const toml::parse_error& err) {
        if (error) {
            *error = "could not parse " + path.string() + ": " + std::string(err.description());
        }
        return {};
    }

    config.providers.clear();
    config.profiles.clear();
    config.server = ServerConfig{};
    config.version = static_cast<int>(parsed["version"].value_or<int64_t>(1));
    config.defaultProfile = parsed["default_profile"].value_or("main");

    if (auto providers = parsed["providers"].as_table()) {
        for (const auto& [key, node] : *providers) {
            auto* table = node.as_table();
            if (!table) {
                continue;
            }
            LlmProviderConfig provider;
            provider.name = TomlKeyName(key);
            provider.type = NormalizeLlmProviderType(TomlString(*table, "type", "openai-compatible"));
            if (provider.type.empty()) {
                provider.type = "openai-compatible";
            }
            provider.baseUrl = TomlString(
                *table,
                "base_url",
                provider.type == "openrouter" ? std::string(kOpenRouterBaseUrl) : "http://127.0.0.1:8000/v1");
            provider.apiKey = TomlString(*table, "api_key");
            config.providers[provider.name] = provider;
        }
    }

    if (auto profiles = parsed["profiles"].as_table()) {
        for (const auto& [key, node] : *profiles) {
            auto* table = node.as_table();
            if (!table) {
                continue;
            }
            LlmProfileConfig profile;
            profile.name = TomlKeyName(key);
            profile.provider = TomlString(*table, "provider", "local");
            profile.model = TomlString(*table, "model");
            if (profile.model.empty()) {
                profile.model = profile.provider == "openrouter" ? "openrouter/auto" : "qwen36-27b";
            }
            if (auto value = (*table)["temperature"].value<double>()) {
                profile.temperature = *value;
            }
            if (auto value = (*table)["top_p"].value<double>()) {
                profile.topP = *value;
            }
            if (auto value = (*table)["max_output_tokens"].value<int64_t>()) {
                profile.maxOutputTokens = static_cast<long>(*value);
            }
            if (auto value = (*table)["timeout_ms"].value<int64_t>()) {
                profile.timeoutMs = static_cast<long>(*value);
            }
            if (auto params = (*table)["params"].as_table()) {
                profile.params = TomlNodeToJson(*params);
            }
            AddProfileScalarParams(profile);
            if (auto openrouter = (*table)["openrouter"].as_table()) {
                if (auto provider = (*openrouter)["provider"].as_table()) {
                    profile.openRouterProvider = TomlNodeToJson(*provider);
                }
            }
            config.profiles[profile.name] = profile;
        }
    }

    if (auto server = parsed["server"].as_table()) {
        config.server.host = TomlString(*server, "host", config.server.host);
        if (auto value = (*server)["base_port"].value<int64_t>()) {
            if (*value > 0 && *value <= 65535) {
                config.server.basePort = static_cast<int>(*value);
            }
        }
        config.server.authToken = TomlString(*server, "auth_token");
        config.server.allowedOrigins = TomlStringArray(*server, "allowed_origins");
        if (auto apps = (*server)["apps"].as_table()) {
            for (const auto& [key, node] : *apps) {
                auto* table = node.as_table();
                if (!table) {
                    continue;
                }
                ServerAppConfig app;
                app.name = TomlKeyName(key);
                app.displayName = TomlString(*table, "display_name", app.name);
                app.path = TomlString(*table, "path");
                if (auto value = (*table)["port"].value<int64_t>()) {
                    if (*value > 0 && *value <= 65535) {
                        app.port = static_cast<int>(*value);
                    }
                }
                config.server.apps[app.name] = app;
            }
        }
    }

    if (config.providers.empty()) {
        config.providers = DefaultAppConfig().providers;
    }
    if (config.profiles.empty()) {
        config.profiles = DefaultAppConfig().profiles;
    }
    if (!config.profiles.contains(config.defaultProfile)) {
        if (error) {
            *error = "default_profile references unknown profile '" + config.defaultProfile + "'";
        }
        return {};
    }
    for (const auto& [name, provider] : config.providers) {
        if (!IsAllowedProviderType(provider.type)) {
            if (error) {
                *error = "provider '" + name + "' has unsupported type '" + provider.type + "'";
            }
            return {};
        }
    }
    for (const auto& [name, profile] : config.profiles) {
        if (!config.providers.contains(profile.provider)) {
            if (error) {
                *error = "profile '" + name + "' references unknown provider '" + profile.provider + "'";
            }
            return {};
        }
    }
    return config;
}

bool SaveAppConfig(const AppConfig& config, std::string* error) {
    return WriteConfigFile(ConfigPath(), AppConfigToToml(config), error);
}

json AppConfigToJson(const AppConfig& config, bool redactSecrets) {
    json out = {
        {"version", config.version},
        {"defaultProfile", config.defaultProfile},
        {"configPath", ConfigPath().string()},
        {"providers", json::object()},
        {"profiles", json::object()},
    };
    for (const auto& [name, provider] : config.providers) {
        json item = {
            {"type", provider.type},
            {"baseUrl", provider.baseUrl},
        };
        if (!provider.apiKey.empty()) {
            item["apiKey"] = redactSecrets ? "<redacted>" : provider.apiKey;
        }
        out["providers"][name] = item;
    }
    for (const auto& [name, profile] : config.profiles) {
        json item = {
            {"provider", profile.provider},
            {"model", profile.model},
            {"params", profile.params},
        };
        if (profile.timeoutMs.has_value()) {
            item["timeoutMs"] = *profile.timeoutMs;
        }
        if (!profile.openRouterProvider.empty()) {
            item["openrouter"] = {{"provider", profile.openRouterProvider}};
        }
        out["profiles"][name] = item;
    }
    out["server"] = {
        {"host", config.server.host},
        {"basePort", config.server.basePort},
        {"authToken", redactSecrets && !config.server.authToken.empty() ? "<redacted>" : config.server.authToken},
        {"allowedOrigins", config.server.allowedOrigins},
        {"apps", json::object()},
    };
    for (const auto& [name, app] : config.server.apps) {
        json item = {
            {"displayName", app.displayName},
            {"path", app.path},
        };
        if (app.port.has_value()) {
            item["port"] = *app.port;
        }
        out["server"]["apps"][name] = item;
    }
    return out;
}

std::string AppConfigToToml(const AppConfig& config) {
    std::ostringstream out;
    out << "# computer.cpp user configuration\n";
    out << "# The tray settings UI and `computer.cpp config` commands both edit this file.\n";
    out << "# Keep this file private; it may contain provider API keys.\n\n";
    out << "version = " << config.version << "\n";
    out << "default_profile = " << TomlStringLiteral(config.defaultProfile) << "\n\n";

    for (const auto& [name, provider] : config.providers) {
        out << TomlTablePath({"providers", name}) << "\n";
        out << "type = " << TomlStringLiteral(provider.type) << "\n";
        out << "base_url = " << TomlStringLiteral(provider.baseUrl) << "\n";
        if (!provider.apiKey.empty()) {
            out << "api_key = " << TomlStringLiteral(provider.apiKey) << "\n";
        }
        out << "\n";
    }

    for (const auto& [name, profile] : config.profiles) {
        out << TomlTablePath({"profiles", name}) << "\n";
        out << "provider = " << TomlStringLiteral(profile.provider) << "\n";
        out << "model = " << TomlStringLiteral(profile.model) << "\n";
        if (profile.temperature.has_value()) {
            out << "temperature = " << *profile.temperature << "\n";
        }
        if (profile.topP.has_value()) {
            out << "top_p = " << *profile.topP << "\n";
        }
        if (profile.maxOutputTokens.has_value()) {
            out << "max_output_tokens = " << *profile.maxOutputTokens << "\n";
        }
        if (profile.timeoutMs.has_value()) {
            out << "timeout_ms = " << *profile.timeoutMs << "\n";
        }
        out << "\n";

        json params = profile.params;
        for (const std::string key : {"temperature", "top_p", "max_output_tokens"}) {
            params.erase(key);
        }
        if (!params.empty()) {
            out << TomlTablePath({"profiles", name, "params"}) << "\n";
            for (const auto& [key, value] : params.items()) {
                out << TomlKey(key) << " = " << JsonToTomlValue(value) << "\n";
            }
            out << "\n";
        }
        if (!profile.openRouterProvider.empty()) {
            out << TomlTablePath({"profiles", name, "openrouter", "provider"}) << "\n";
            for (const auto& [key, value] : profile.openRouterProvider.items()) {
                out << TomlKey(key) << " = " << JsonToTomlValue(value) << "\n";
            }
            out << "\n";
        }
    }

    out << TomlTablePath({"server"}) << "\n";
    out << "host = " << TomlStringLiteral(config.server.host) << "\n";
    out << "base_port = " << config.server.basePort << "\n";
    if (!config.server.authToken.empty()) {
        out << "auth_token = " << TomlStringLiteral(config.server.authToken) << "\n";
    }
    out << "allowed_origins = " << JsonToTomlValue(config.server.allowedOrigins) << "\n\n";

    for (const auto& [name, app] : config.server.apps) {
        out << TomlTablePath({"server", "apps", name}) << "\n";
        out << "display_name = " << TomlStringLiteral(app.displayName) << "\n";
        out << "path = " << TomlStringLiteral(app.path) << "\n";
        if (app.port.has_value()) {
            out << "port = " << *app.port << "\n";
        }
        out << "\n";
    }

    return out.str();
}

bool SetProviderConfig(
    AppConfig& config,
    const std::string& name,
    const std::string& type,
    const std::string& baseUrl,
    std::string* error) {
    if (IsBlank(name)) {
        if (error) {
            *error = "provider name must be non-empty";
        }
        return false;
    }
    std::string providerError;
    std::string normalizedType = NormalizeLlmProviderType(type, &providerError);
    if (!providerError.empty()) {
        if (error) {
            *error = providerError;
        }
        return false;
    }
    LlmProviderConfig provider = config.providers.contains(name) ? config.providers[name] : LlmProviderConfig{};
    provider.name = name;
    provider.type = normalizedType;
    provider.baseUrl = Trim(baseUrl);
    if (provider.baseUrl.empty()) {
        provider.baseUrl = provider.type == "openrouter" ? std::string(kOpenRouterBaseUrl) : "http://127.0.0.1:8000/v1";
    }
    config.providers[name] = provider;
    return true;
}

bool SetProfileDefaultParam(LlmProfileConfig& profile, const std::string& key, const std::string& rawValue, std::string* error) {
    std::string normalizedKey = key;
    if (normalizedKey == "max-output-tokens") {
        normalizedKey = "max_output_tokens";
    }
    if (normalizedKey == "timeout-ms") {
        normalizedKey = "timeout_ms";
    }
    if (normalizedKey == "top-p") {
        normalizedKey = "top_p";
    }

    if (normalizedKey == "temperature" || normalizedKey == "top_p") {
        auto value = ParseDouble(rawValue);
        if (!value) {
            if (error) {
                *error = normalizedKey + " requires a number";
            }
            return false;
        }
        if (normalizedKey == "temperature" && (*value < 0.0 || *value > 2.0)) {
            if (error) {
                *error = "temperature must be between 0 and 2";
            }
            return false;
        }
        if (normalizedKey == "top_p" && (*value < 0.0 || *value > 1.0)) {
            if (error) {
                *error = "top_p must be between 0 and 1";
            }
            return false;
        }
        if (normalizedKey == "temperature") {
            profile.temperature = *value;
            profile.params["temperature"] = *value;
        } else {
            profile.topP = *value;
            profile.params["top_p"] = *value;
        }
        return true;
    }
    if (normalizedKey == "max_output_tokens") {
        auto value = ParseLong(rawValue);
        if (!value || *value <= 0) {
            if (error) {
                *error = "max_output_tokens requires a positive integer";
            }
            return false;
        }
        profile.maxOutputTokens = *value;
        profile.params["max_output_tokens"] = *value;
        return true;
    }
    if (normalizedKey == "timeout_ms") {
        auto value = ParseLong(rawValue);
        if (!value || *value <= 0) {
            if (error) {
                *error = "timeout_ms requires a positive integer";
            }
            return false;
        }
        profile.timeoutMs = *value;
        return true;
    }
    if (error) {
        *error = "unknown profile parameter '" + key + "'";
    }
    return false;
}

bool ImportLegacyInferenceEnv(AppConfig& config, std::string* warning, std::string* error) {
    (void)warning;
    std::string requestedProvider = NormalizeLlmProviderType(EnvFirst({"COMPUTER_CPP_INFERENCE_PROVIDER"}));
    std::string openRouterKey = EnvFirst({"OPENROUTER_API_KEY"});
    std::string baseUrl = EnvFirst({"OPENAI_BASE_URL", "COMPUTER_CPP_INFERENCE_BASE_URL"});
    bool useOpenRouter = requestedProvider == "openrouter" ||
        (!openRouterKey.empty() && requestedProvider != "openai-compatible" && baseUrl.empty()) ||
        Lowercase(baseUrl).find("openrouter.ai") != std::string::npos;

    std::string providerName = useOpenRouter ? "openrouter" : "main";
    std::string providerType = useOpenRouter ? "openrouter" : "openai-compatible";
    if (baseUrl.empty()) {
        baseUrl = useOpenRouter ? std::string(kOpenRouterBaseUrl) : "http://127.0.0.1:8000/v1";
    }
    if (!SetProviderConfig(config, providerName, providerType, baseUrl, error)) {
        return false;
    }

    LlmProviderConfig& provider = config.providers[providerName];
    std::string apiKey = useOpenRouter ? openRouterKey : EnvFirst({"OPENAI_API_KEY", "INFERENCE_API_KEY"});
    if (!apiKey.empty()) {
        provider.apiKey = apiKey;
    }

    LlmProfileConfig profile;
    profile.name = "main";
    profile.provider = providerName;
    profile.model = useOpenRouter
        ? EnvFirst({"OPENROUTER_MODEL", "COMPUTER_CPP_INFERENCE_MODEL"})
        : EnvFirst({"COMPUTER_CPP_INFERENCE_MODEL", "OPENAI_MODEL"});
    if (profile.model.empty()) {
        profile.model = useOpenRouter ? "openrouter/auto" : "qwen36-27b";
    }
    profile.timeoutMs = 180000;
    config.defaultProfile = profile.name;
    config.profiles[profile.name] = profile;
    return true;
}

} // namespace ComputerCpp
