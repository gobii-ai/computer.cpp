#include "CliConfig.h"

#include "computer_cpp/AppConfig.h"
#include "computer_cpp/AppPaths.h"
#include "computer_cpp/InferenceClient.h"
#include "computer_cpp/StringUtils.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

using json = nlohmann::json;

namespace ComputerCpp::Cli {
namespace {

int ErrorExit(const std::string& message, int code = 2) {
    std::cerr << "Error: " << message << "\n";
    return code;
}

std::string ReadAllStdin() {
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    return buffer.str();
}

std::string ShellQuote(const std::string& value) {
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

int OpenConfigFile() {
    std::string path = ConfigPath().string();
#ifdef __APPLE__
    std::string command = "/usr/bin/open " + ShellQuote(path);
#elif defined(_WIN32)
    std::string command = "start \"\" \"" + path + "\"";
#else
    std::string command = "xdg-open " + ShellQuote(path);
#endif
    int status = std::system(command.c_str());
    if (status != 0) {
        std::cout << path << "\n";
        return 1;
    }
    std::cout << path << "\n";
    return 0;
}

bool LoadConfig(AppConfig& config, std::string& error) {
    config = LoadAppConfig(&error);
    return error.empty();
}

LlmProfileConfig& EnsureProfile(AppConfig& config, const std::string& name) {
    auto it = config.profiles.find(name);
    if (it != config.profiles.end()) {
        return it->second;
    }
    LlmProfileConfig profile;
    profile.name = name;
    config.profiles[name] = profile;
    return config.profiles[name];
}

int SaveAndReport(const CliOptions& options, const AppConfig& config, const json& extra = json::object()) {
    std::string error;
    if (!SaveAppConfig(config, &error)) {
        return ErrorExit(error, 1);
    }
    json response = {{"ok", true}, {"configPath", ConfigPath().string()}};
    for (const auto& [key, value] : extra.items()) {
        response[key] = value;
    }
    if (options.jsonOutput) {
        std::cout << response.dump(2) << "\n";
    } else {
        std::cout << "wrote " << ConfigPath().string() << "\n";
        if (extra.contains("warning")) {
            std::cerr << "Warning: " << extra["warning"].get<std::string>() << "\n";
        }
    }
    return 0;
}

int HandleInit(const CliOptions& options, const std::vector<std::string>& args) {
    bool force = false;
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--force") {
            force = true;
        } else {
            return ErrorExit("unknown config init option: " + args[i]);
        }
    }
    if (AppConfigExists() && !force) {
        return ErrorExit("config already exists at " + ConfigPath().string() + "; pass --force to overwrite", 1);
    }
    return SaveAndReport(options, DefaultAppConfig());
}

int HandleShow(const CliOptions& options, const std::vector<std::string>& args) {
    if (args.size() > 2) {
        return ErrorExit("unknown config show option: " + args[2]);
    }
    std::string error;
    AppConfig config = LoadAppConfig(&error);
    if (!error.empty()) {
        return ErrorExit(error, 1);
    }
    json data = AppConfigToJson(config, true);
    if (options.jsonOutput) {
        std::cout << data.dump(2) << "\n";
    } else {
        std::cout << data.dump(2) << "\n";
    }
    return 0;
}

int HandleSetProvider(const CliOptions& options, const std::vector<std::string>& args) {
    if (args.size() < 3 || IsBlank(args[2])) {
        return ErrorExit("config set-provider requires a provider name");
    }
    std::string name = args[2];
    AppConfig config;
    std::string error;
    if (!LoadConfig(config, error)) {
        return ErrorExit(error, 1);
    }

    bool hadProvider = config.providers.contains(name);
    LlmProviderConfig existing = hadProvider ? config.providers[name] : LlmProviderConfig{};
    std::string type = existing.type.empty() ? "openai-compatible" : existing.type;
    std::string baseUrl = existing.baseUrl;
    bool baseUrlProvided = false;
    bool setNoApiKey = false;
    bool readApiKeyStdin = false;
    std::string apiKey;

    for (size_t i = 3; i < args.size(); ++i) {
        if (args[i] == "--type") {
            if (i + 1 >= args.size()) return ErrorExit("config set-provider --type requires a value");
            type = args[++i];
        } else if (args[i] == "--base-url") {
            if (i + 1 >= args.size()) return ErrorExit("config set-provider --base-url requires a value");
            baseUrl = args[++i];
            baseUrlProvided = true;
        } else if (args[i] == "--api-key") {
            if (i + 1 >= args.size()) return ErrorExit("config set-provider --api-key requires a value");
            apiKey = args[++i];
        } else if (args[i] == "--api-key-stdin") {
            readApiKeyStdin = true;
        } else if (args[i] == "--no-api-key") {
            setNoApiKey = true;
        } else {
            return ErrorExit("unknown config set-provider option: " + args[i]);
        }
    }

    std::string normalizedType = NormalizeLlmProviderType(type, &error);
    if (!error.empty()) {
        return ErrorExit(error);
    }
    if (!baseUrlProvided && (!hadProvider || normalizedType != existing.type)) {
        baseUrl.clear();
    }
    if (!SetProviderConfig(config, name, type, baseUrl, &error)) {
        return ErrorExit(error);
    }
    LlmProviderConfig& provider = config.providers[name];
    json extra = json::object();
    if (setNoApiKey) {
        provider.apiKey.clear();
    } else {
        if (readApiKeyStdin) {
            apiKey = Trim(ReadAllStdin());
        }
        if (!apiKey.empty()) {
            provider.apiKey = apiKey;
        }
    }
    return SaveAndReport(options, config, extra);
}

int HandleSetProfile(const CliOptions& options, const std::vector<std::string>& args) {
    if (args.size() < 3 || IsBlank(args[2])) {
        return ErrorExit("config set-profile requires a profile name");
    }
    std::string name = args[2];
    AppConfig config;
    std::string error;
    if (!LoadConfig(config, error)) {
        return ErrorExit(error, 1);
    }
    LlmProfileConfig& profile = EnsureProfile(config, name);
    profile.name = name;

    for (size_t i = 3; i < args.size(); ++i) {
        if (args[i] == "--provider") {
            if (i + 1 >= args.size()) return ErrorExit("config set-profile --provider requires a value");
            std::string provider = args[++i];
            if (!config.providers.contains(provider)) {
                return ErrorExit("unknown provider '" + provider + "'");
            }
            profile.provider = provider;
        } else if (args[i] == "--model") {
            if (i + 1 >= args.size()) return ErrorExit("config set-profile --model requires a value");
            profile.model = args[++i];
        } else if (args[i] == "--temperature" || args[i] == "--top-p" ||
                   args[i] == "--max-output-tokens" || args[i] == "--timeout-ms") {
            if (i + 1 >= args.size()) return ErrorExit("config set-profile " + args[i] + " requires a value");
            std::string key = args[i].substr(2);
            std::string value = args[++i];
            if (!SetProfileDefaultParam(profile, key, value, &error)) {
                return ErrorExit(error);
            }
        } else if (args[i] == "--param") {
            if (i + 1 >= args.size()) return ErrorExit("config set-profile --param requires key=value");
            std::string assignment = args[++i];
            auto pos = assignment.find('=');
            if (pos == std::string::npos || pos == 0) {
                return ErrorExit("config set-profile --param requires key=value");
            }
            std::string rawValue = assignment.substr(pos + 1);
            json parsed = json::parse(rawValue, nullptr, false);
            profile.params[assignment.substr(0, pos)] = parsed.is_discarded() ? json(rawValue) : parsed;
        } else if (args[i] == "--openrouter-provider-json") {
            if (i + 1 >= args.size()) return ErrorExit("config set-profile --openrouter-provider-json requires JSON object");
            json parsed = json::parse(args[++i], nullptr, false);
            if (!parsed.is_object()) {
                return ErrorExit("--openrouter-provider-json must be a JSON object");
            }
            profile.openRouterProvider = parsed;
        } else if (args[i] == "--default") {
            config.defaultProfile = name;
        } else {
            return ErrorExit("unknown config set-profile option: " + args[i]);
        }
    }

    if (!config.providers.contains(profile.provider)) {
        return ErrorExit("profile '" + name + "' references unknown provider '" + profile.provider + "'");
    }
    return SaveAndReport(options, config);
}

int HandleUse(const CliOptions& options, const std::vector<std::string>& args) {
    if (args.size() != 3 || IsBlank(args[2])) {
        return ErrorExit("config use requires a profile name");
    }
    AppConfig config;
    std::string error;
    if (!LoadConfig(config, error)) {
        return ErrorExit(error, 1);
    }
    if (!config.profiles.contains(args[2])) {
        return ErrorExit("unknown profile '" + args[2] + "'");
    }
    config.defaultProfile = args[2];
    return SaveAndReport(options, config);
}

int HandleImportEnv(const CliOptions& options, const std::vector<std::string>& args) {
    if (args.size() > 2) {
        return ErrorExit("unknown config import-env option: " + args[2]);
    }
    AppConfig config;
    std::string error;
    if (!LoadConfig(config, error)) {
        return ErrorExit(error, 1);
    }
    std::string warning;
    if (!ImportLegacyInferenceEnv(config, &warning, &error)) {
        return ErrorExit(error, 1);
    }
    json extra = json::object();
    if (!warning.empty()) {
        extra["warning"] = warning;
    }
    return SaveAndReport(options, config, extra);
}

int HandleTest(const CliOptions& options, const std::vector<std::string>& args) {
    std::string profile;
    bool live = false;
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--live") {
            live = true;
        } else if (profile.empty()) {
            profile = args[i];
        } else {
            return ErrorExit("unknown config test option: " + args[i]);
        }
    }
    json request = json::object();
    if (!profile.empty()) {
        request["profile"] = profile;
    }
    if (!live) {
        json response = Inference::ResolveChatConfig(request);
        if (options.jsonOutput) {
            std::cout << response.dump(2) << "\n";
        } else if (response.value("ok", false)) {
            auto data = response.value("data", json::object());
            std::cout << "profile=" << data.value("profile", "") << "\n";
            std::cout << "provider=" << data.value("provider", "") << "\n";
            std::cout << "base_url=" << data.value("baseUrl", "") << "\n";
            std::cout << "model=" << data.value("model", "") << "\n";
            std::cout << "api_key=" << (data.value("hasApiKey", false) ? "configured" : "missing") << "\n";
        } else {
            std::cerr << "Error: " << response.value("error", "invalid config") << "\n";
            return 1;
        }
        return response.value("ok", false) ? 0 : 1;
    }

    request["messages"] = json::array({{{"role", "user"}, {"content", "Reply with exactly: ok"}}});
    request["max_output_tokens"] = 8;
    json response = Inference::ChatCompletion(request);
    if (options.jsonOutput) {
        std::cout << response.dump(2) << "\n";
    } else if (response.value("ok", false)) {
        auto data = response.value("data", json::object());
        std::cout << "ok " << data.value("provider", "") << " " << data.value("model", "") << "\n";
        std::cout << data.value("content", "") << "\n";
    } else {
        std::cerr << "Error: " << response.value("error", "test failed") << "\n";
        return 1;
    }
    return response.value("ok", false) ? 0 : 1;
}

} // namespace

int HandleConfigCommand(const CliOptions& options, const std::vector<std::string>& args) {
    if (args.size() < 2 || args[1] == "--help" || args[1] == "-h") {
        std::cout << R"(computer.cpp config

Usage:
  config path
  config open
  config init [--force]
  config show
  config set-provider <name> [--type openrouter|openai-compatible] [--base-url url]
                      [--api-key key|--api-key-stdin|--no-api-key]
  config set-profile <name> [--provider name] [--model id] [--temperature n]
                     [--top-p n] [--max-output-tokens n] [--timeout-ms n]
                     [--openrouter-provider-json json] [--default]
  config use <profile>
  config import-env
  config test [profile] [--live]
)";
        return 0;
    }
    if (args[1] == "path") {
        if (args.size() > 2) return ErrorExit("unknown config path option: " + args[2]);
        std::cout << ConfigPath().string() << "\n";
        return 0;
    }
    if (args[1] == "open") {
        if (args.size() > 2) return ErrorExit("unknown config open option: " + args[2]);
        return OpenConfigFile();
    }
    if (args[1] == "init") return HandleInit(options, args);
    if (args[1] == "show") return HandleShow(options, args);
    if (args[1] == "set-provider") return HandleSetProvider(options, args);
    if (args[1] == "set-profile") return HandleSetProfile(options, args);
    if (args[1] == "use") return HandleUse(options, args);
    if (args[1] == "import-env") return HandleImportEnv(options, args);
    if (args[1] == "test") return HandleTest(options, args);
    return ErrorExit("unknown config subcommand: " + args[1]);
}

} // namespace ComputerCpp::Cli
