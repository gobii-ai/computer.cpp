#include "CliApp.h"
#include "CliRecordingMetadata.h"
#include "PosixArgv.h"

#include "computer_cpp/AppConfig.h"
#include "computer_cpp/AppPaths.h"
#include "computer_cpp/CommandRecording.h"
#include "computer_cpp/LuaRunner.h"
#include "computer_cpp/StringUtils.h"
#include "computer_cpp/TrayServerState.h"
#include "computer_cpp/WindowsUtil.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <csignal>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace ComputerCpp::Cli {
namespace {

constexpr std::string_view kMcpEndpointPath = "/mcp";
constexpr std::string_view kMcpLatestProtocolVersion = "2025-11-25";

AppConfig LoadAppConfigForCommand(std::string* error) {
    struct Cache {
        std::mutex mutex;
        bool initialized = false;
        fs::path path;
        bool exists = false;
        fs::file_time_type modified;
        uintmax_t size = 0;
        AppConfig config;
        std::string error;
    };
    static Cache cache;

    const fs::path path = ConfigPath();
    std::error_code statError;
    const bool exists = fs::exists(path, statError);
    fs::file_time_type modified{};
    uintmax_t size = 0;
    if (!statError && exists) {
        modified = fs::last_write_time(path, statError);
        if (!statError) {
            size = fs::file_size(path, statError);
        }
    }

    std::lock_guard<std::mutex> lock(cache.mutex);
    if (!statError &&
        cache.initialized &&
        cache.path == path &&
        cache.exists == exists &&
        (!exists || (cache.modified == modified && cache.size == size))) {
        if (error) {
            *error = cache.error;
        }
        return cache.config;
    }

    std::string loadError;
    AppConfig config = LoadAppConfig(&loadError);
    if (!statError) {
        cache.initialized = true;
        cache.path = path;
        cache.exists = exists;
        cache.modified = modified;
        cache.size = size;
        cache.config = config;
        cache.error = loadError;
    }
    if (error) {
        *error = loadError;
    }
    return config;
}

#if defined(_WIN32)
using AppSocket = SOCKET;
constexpr AppSocket kInvalidSocket = INVALID_SOCKET;
#else
using AppSocket = int;
constexpr AppSocket kInvalidSocket = -1;
#endif

void CloseAppSocket(AppSocket fd) {
#if defined(_WIN32)
    if (fd != INVALID_SOCKET) {
        closesocket(fd);
    }
#else
    if (fd >= 0) {
        ::close(fd);
    }
#endif
}

#if defined(__unix__) || defined(__APPLE__)
volatile sig_atomic_t gAppServeStopRequested = 0;
volatile sig_atomic_t gAppServeServerFd = -1;

void AppServeSignalHandler(int) {
    gAppServeStopRequested = 1;
    int fd = gAppServeServerFd;
    if (fd >= 0) {
        ::close(fd);
        gAppServeServerFd = -1;
    }
}

class ScopedAppServeSignals {
public:
    explicit ScopedAppServeSignals(int serverFd) {
        gAppServeStopRequested = 0;
        gAppServeServerFd = serverFd;
        previousTerm_ = std::signal(SIGTERM, AppServeSignalHandler);
        previousInt_ = std::signal(SIGINT, AppServeSignalHandler);
    }

    ~ScopedAppServeSignals() {
        gAppServeServerFd = -1;
        std::signal(SIGTERM, previousTerm_);
        std::signal(SIGINT, previousInt_);
    }

private:
    using Handler = void (*)(int);
    Handler previousTerm_ = nullptr;
    Handler previousInt_ = nullptr;
};
#else
bool gAppServeStopRequested = false;
#endif

struct AppRunArgs {
    bool async = false;
    bool trace = false;
    std::optional<fs::path> traceDir;
    json input = json::object();
};

void PrintRecordingStatusToStderr(const json& recording);

json ErrorPayload(std::string code, std::string message) {
    return {
        {"ok", false},
        {"code", std::move(code)},
        {"error", std::move(message)}
    };
}

int ErrorExit(const CliOptions& options, const std::string& message, int code = 1, const std::string& errorCode = "internal_error") {
    if (options.jsonOutput) {
        std::cout << ErrorPayload(errorCode, message).dump(2) << "\n";
    } else {
        std::cerr << "Error: " << message << "\n";
    }
    return code;
}

std::string NowIsoUtc() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm {};
#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

int64_t NowEpochSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool RuntimeLoggingEnabled() {
    const char* raw = std::getenv("COMPUTER_CPP_LOG");
    if (raw == nullptr || *raw == '\0') {
        return true;
    }
    std::string value = Trim(raw);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value != "0" && value != "false" && value != "off" && value != "quiet";
}

std::string CompactLogValue(std::string value) {
    for (char& ch : value) {
        if (ch == '\n' || ch == '\r') {
            ch = ' ';
        }
    }
    if (value.size() > 160) {
        value.resize(157);
        value += "...";
    }
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped += '"';
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            escaped += '\\';
        }
        escaped += ch;
    }
    escaped += '"';
    return escaped;
}

void AppendRuntimeLog(
    const std::string& category,
    const std::string& message,
    const std::map<std::string, std::string>& fields = {}
) {
    if (!RuntimeLoggingEnabled()) {
        return;
    }
    const char* rawPath = std::getenv("COMPUTER_CPP_LOG_FILE");
    if (rawPath == nullptr || *rawPath == '\0') {
        return;
    }
    try {
        fs::path logPath(rawPath);
        if (!logPath.parent_path().empty()) {
            fs::create_directories(logPath.parent_path());
        }
        std::ofstream log(logPath, std::ios::app);
        if (!log) {
            return;
        }
        log << "[" << NowIsoUtc() << "] computer.cpp " << category << " " << message;
        for (const auto& [key, value] : fields) {
            log << " " << key << "=" << CompactLogValue(value);
        }
        log << "\n";
    } catch (...) {
    }
}

uint64_t Fnv1a64(const std::string& value) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char ch : value) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string Hex64(uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::nouppercase << value;
    return out.str();
}

std::string SanitizeIdPart(const std::string& value) {
    std::string out;
    for (unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_') {
            out.push_back(static_cast<char>(ch));
        } else if (ch == '.' || ch == ' ') {
            out.push_back('-');
        }
    }
    if (out.empty()) {
        return "app";
    }
    return out;
}

std::string AppIdFor(const fs::path& appPath, const json& schema) {
    const std::string name = SanitizeIdPart(schema.value("name", appPath.stem().string()));
    const std::string absolute = fs::absolute(appPath).lexically_normal().string();
    return name + "-" + Hex64(Fnv1a64(absolute));
}

std::string NewOperationId() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    std::random_device rd;
    std::mt19937_64 rng(rd());
    std::ostringstream out;
    out << "op_" << std::hex << micros << "_" << (rng() & 0xffffffu);
    return out.str();
}

struct OperationPaths {
    fs::path dir;
    fs::path operationJson;
    fs::path inputJson;
    fs::path resultJson;
    fs::path errorJson;
    fs::path traceJsonl;
    fs::path progressJson;
    fs::path recordingJson;
    fs::path approvalJson;
    fs::path artifactsDir;
};

OperationPaths PathsForOperation(const std::string& appId, const std::string& operationId) {
    fs::path dir = AppDataDir() / "apps" / appId / "operations" / operationId;
    return {
        dir,
        dir / "operation.json",
        dir / "input.json",
        dir / "result.json",
        dir / "error.json",
        dir / "trace.jsonl",
        dir / "progress.json",
        dir / "recording.json",
        dir / "approval.json",
        dir / "artifacts",
    };
}

bool WriteJsonFile(const fs::path& path, const json& value, std::string& error) {
    try {
        if (!path.parent_path().empty()) {
            EnsureDirectory(path.parent_path());
        }
        fs::path tempPath = path;
#if defined(__unix__) || defined(__APPLE__)
        tempPath += "." + std::to_string(static_cast<long long>(::getpid())) + ".tmp";
#else
        tempPath += ".tmp";
#endif
        std::ofstream file(tempPath);
        if (!file) {
            error = "failed to open " + tempPath.string();
            return false;
        }
        file << value.dump(2) << "\n";
        file.close();
        if (!file) {
            error = "failed to write " + tempPath.string();
            return false;
        }
        std::error_code ec;
        fs::rename(tempPath, path, ec);
        if (ec) {
            fs::remove(tempPath, ec);
            error = "failed to replace " + path.string() + ": " + ec.message();
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

std::optional<json> ReadJsonFile(const fs::path& path, std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = "failed to open " + path.string();
        return std::nullopt;
    }
    json parsed = json::parse(file, nullptr, false);
    if (parsed.is_discarded()) {
        error = "invalid JSON in " + path.string();
        return std::nullopt;
    }
    return parsed;
}

bool WriteTraceJsonl(const fs::path& path, const json& trace, std::string& error) {
    try {
        if (!path.parent_path().empty()) {
            EnsureDirectory(path.parent_path());
        }
        std::ofstream file(path);
        if (!file) {
            error = "failed to open " + path.string();
            return false;
        }
        if (trace.is_array()) {
            for (const auto& entry : trace) {
                file << entry.dump() << "\n";
            }
        }
        return file.good();
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

json PublicOperationRecord(json record) {
    record.erase("input");
    record.erase("recording_requested");
    record.erase("recording_surface");
    return record;
}

bool IsFinalStatus(const std::string& status) {
    return status == "succeeded" || status == "failed" || status == "cancelled";
}

std::optional<json> ParseJsonOutput(const LuaRunResult& result, std::string& error) {
    if (result.stdoutText.empty()) {
        error = result.stderrText.empty() ? "Lua app returned no output" : result.stderrText;
        return std::nullopt;
    }
    json parsed = json::parse(result.stdoutText, nullptr, false);
    if (parsed.is_discarded()) {
        error = "Lua app returned invalid JSON";
        if (!result.stderrText.empty()) {
            error += ": " + result.stderrText;
        }
        return std::nullopt;
    }
    return parsed;
}

LuaRunOptions BaseLuaOptions(
    const CliOptions& options,
    const std::string& executablePath,
    const fs::path& appPath,
    std::string mode
) {
    LuaRunOptions lua;
    lua.session = options.session;
    lua.controlSessionToken = options.controlSessionToken;
    lua.controlScope = options.controlScope;
    lua.executablePath = executablePath;
    lua.scriptPath = appPath;
    lua.jsonOutput = true;
    lua.vars["__ac_app_mode"] = std::move(mode);
    return lua;
}

std::optional<json> LoadAppSchema(
    const CliOptions& options,
    const std::string& executablePath,
    const fs::path& appPath,
    std::string& error
) {
    LuaRunOptions lua = BaseLuaOptions(options, executablePath, appPath, "schema");
    LuaRunResult result = RunLuaScriptCapture(lua);
    auto parsed = ParseJsonOutput(result, error);
    if (!parsed) {
        return std::nullopt;
    }
    if (!parsed->value("ok", false)) {
        error = parsed->value("error", "failed to load app schema");
        return std::nullopt;
    }
    if (!parsed->contains("data") || !(*parsed)["data"].is_object()) {
        error = "Lua app schema payload is missing data";
        return std::nullopt;
    }
    const json commands = (*parsed)["data"].value("commands", json::object());
    for (auto it = commands.begin(); it != commands.end(); ++it) {
        if (it.key().rfind("computer_cpp_", 0) == 0) {
            error = "app command names cannot use the reserved computer_cpp_ prefix: " + it.key();
            return std::nullopt;
        }
    }
    return (*parsed)["data"];
}

bool JsonTruthyString(const std::string& value, bool& out) {
    const std::string lowered = Lowercase(value);
    if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on") {
        out = true;
        return true;
    }
    if (lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off") {
        out = false;
        return true;
    }
    return false;
}

bool ParseInteger(const std::string& value, int64_t& out) {
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc() && ptr == end;
}

bool ParseNumber(const std::string& value, double& out) {
    char* end = nullptr;
    out = std::strtod(value.c_str(), &end);
    return end != value.c_str() && end != nullptr && *end == '\0';
}

bool ParseValueForSchema(const json& schema, const std::string& value, json& out, std::string& error) {
    const std::string type = schema.value("type", "string");
    if (type == "string") {
        out = value;
        return true;
    }
    if (type == "integer") {
        int64_t parsed = 0;
        if (!ParseInteger(value, parsed)) {
            error = "expected integer value";
            return false;
        }
        if (schema.contains("minimum") && parsed < schema["minimum"].get<int64_t>()) {
            error = "value must be at least " + schema["minimum"].dump();
            return false;
        }
        if (schema.contains("maximum") && parsed > schema["maximum"].get<int64_t>()) {
            error = "value must be at most " + schema["maximum"].dump();
            return false;
        }
        out = parsed;
        return true;
    }
    if (type == "number") {
        double parsed = 0;
        if (!ParseNumber(value, parsed)) {
            error = "expected number value";
            return false;
        }
        if (schema.contains("minimum") && parsed < schema["minimum"].get<double>()) {
            error = "value must be at least " + schema["minimum"].dump();
            return false;
        }
        if (schema.contains("maximum") && parsed > schema["maximum"].get<double>()) {
            error = "value must be at most " + schema["maximum"].dump();
            return false;
        }
        out = parsed;
        return true;
    }
    if (type == "boolean") {
        bool parsed = false;
        if (!JsonTruthyString(value, parsed)) {
            error = "expected boolean value";
            return false;
        }
        out = parsed;
        return true;
    }
    if (type == "array" || type == "object") {
        json parsed = json::parse(value, nullptr, false);
        if (parsed.is_discarded() || (type == "array" && !parsed.is_array()) || (type == "object" && !parsed.is_object())) {
            error = "expected JSON " + type + " value";
            return false;
        }
        out = std::move(parsed);
        return true;
    }
    out = value;
    return true;
}

std::set<std::string> RequiredFields(const json& inputSchema) {
    std::set<std::string> required;
    if (!inputSchema.contains("required") || !inputSchema["required"].is_array()) {
        return required;
    }
    for (const auto& item : inputSchema["required"]) {
        if (item.is_string()) {
            required.insert(item.get<std::string>());
        }
    }
    return required;
}

void ApplyDefaults(const json& inputSchema, json& input) {
    const json properties = inputSchema.value("properties", json::object());
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        if (it.value().is_object() && it.value().contains("default") && !input.contains(it.key())) {
            input[it.key()] = it.value()["default"];
        }
    }
}

std::optional<AppRunArgs> ParseAppRunArgs(
    const std::vector<std::string>& args,
    size_t start,
    const json& inputSchema,
    std::string& error
) {
    AppRunArgs parsed;
    ApplyDefaults(inputSchema, parsed.input);
    const json properties = inputSchema.value("properties", json::object());

    for (size_t i = start; i < args.size(); ++i) {
        std::string arg = args[i];
        if (arg == "--async") {
            parsed.async = true;
            continue;
        }
        if (arg == "--trace") {
            parsed.trace = true;
            continue;
        }
        if (arg == "--trace-dir") {
            if (i + 1 >= args.size() || IsBlank(args[i + 1])) {
                error = "--trace-dir requires a directory";
                return std::nullopt;
            }
            parsed.trace = true;
            parsed.traceDir = args[++i];
            continue;
        }
        if (arg.rfind("--", 0) != 0 || arg.size() <= 2) {
            error = "unexpected positional argument: " + arg;
            return std::nullopt;
        }

        std::string key = arg.substr(2);
        std::optional<std::string> value;
        const size_t equals = key.find('=');
        if (equals != std::string::npos) {
            value = key.substr(equals + 1);
            key = key.substr(0, equals);
        }
        if (!properties.contains(key)) {
            error = "unknown command option: --" + key;
            return std::nullopt;
        }
        const json property = properties[key];
        const std::string type = property.value("type", "string");
        if (!value.has_value()) {
            if (type == "boolean" && (i + 1 >= args.size() || args[i + 1].rfind("--", 0) == 0)) {
                parsed.input[key] = true;
                continue;
            }
            if (i + 1 >= args.size()) {
                error = "--" + key + " requires a value";
                return std::nullopt;
            }
            value = args[++i];
        }
        json parsedValue;
        std::string parseError;
        if (!ParseValueForSchema(property, *value, parsedValue, parseError)) {
            error = "--" + key + " " + parseError;
            return std::nullopt;
        }
        parsed.input[key] = std::move(parsedValue);
    }

    for (const std::string& required : RequiredFields(inputSchema)) {
        if (!parsed.input.contains(required) || parsed.input[required].is_null()) {
            error = "--" + required + " is required";
            return std::nullopt;
        }
    }
    return parsed;
}

std::string OptionType(const json& schema) {
    return schema.value("type", "value");
}

std::string OptionDescription(const json& schema, bool required) {
    std::vector<std::string> parts;
    if (required) {
        parts.push_back("Required.");
    }
    if (schema.contains("description") && schema["description"].is_string()) {
        parts.push_back(schema["description"].get<std::string>());
    }
    if (schema.contains("default")) {
        parts.push_back("Default: " + schema["default"].dump() + ".");
    }
    std::ostringstream out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out << " ";
        out << parts[i];
    }
    return out.str();
}

void PrintAppHelp(const fs::path& appPath, const json& schema) {
    std::cout << "Usage:\n";
    std::cout << "  computer.cpp app run " << appPath.string() << " <command> [options]\n\n";
    std::cout << schema.value("title", schema.value("name", "app")) << "\n\n";
    std::cout << "Commands:\n";
    const json commands = schema.value("commands", json::object());
    for (auto it = commands.begin(); it != commands.end(); ++it) {
        std::cout << "  " << it.key();
        const std::string description = it.value().value("description", "");
        if (!description.empty()) {
            std::cout << "  " << description;
        }
        std::cout << "\n";
    }
}

void PrintCommandHelp(const fs::path& appPath, const std::string& commandName, const json& command) {
    const json inputSchema = command.value("input", json::object());
    const json properties = inputSchema.value("properties", json::object());
    const auto required = RequiredFields(inputSchema);

    std::cout << "Usage:\n";
    std::cout << "  computer.cpp app run " << appPath.string() << " " << commandName;
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        const bool isRequired = required.count(it.key()) > 0;
        std::cout << (isRequired ? " --" : " [--") << it.key();
        if (it.value().value("type", "string") != "boolean") {
            std::cout << " <" << OptionType(it.value()) << ">";
        }
        if (!isRequired) {
            std::cout << "]";
        }
    }
    std::cout << " [--async]\n\n";

    const std::string description = command.value("description", "");
    if (!description.empty()) {
        std::cout << description << "\n\n";
    }
    std::cout << "Options:\n";
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        const bool isRequired = required.count(it.key()) > 0;
        std::cout << "  --" << it.key() << " " << OptionType(it.value());
        const std::string descriptionText = OptionDescription(it.value(), isRequired);
        if (!descriptionText.empty()) {
            std::cout << "   " << descriptionText;
        }
        std::cout << "\n";
    }
    std::cout << "  --async   Start operation asynchronously and return operation id.\n";
    std::cout << "  --trace   Include execution trace in JSON output.\n";
    std::cout << "  --trace-dir directory   Write execution trace as JSONL.\n";
}

std::optional<json> RunAppCommand(
    const CliOptions& options,
    const std::string& executablePath,
    const fs::path& appPath,
    const std::string& commandName,
    const json& input,
    const std::optional<fs::path>& operationDir,
    const std::string& appId,
    const std::string& surface,
    const std::optional<bool>& recordingEnabledOverride,
    const std::optional<std::string>& recordingIdOverride,
    json* recordingOut,
    std::string& error
) {
    constexpr int64_t kAppCommandLeaseTtlMs = 60 * 1000;
    constexpr int64_t kAppCommandQueueWaitMs = 60 * 60 * 1000;
    constexpr int64_t kAppCommandMaxRuntimeMs = 24 * 60 * 60 * 1000LL;

    std::string configError;
    const AppConfig appConfig = LoadAppConfigForCommand(&configError);
    const bool recordingEnabled = recordingEnabledOverride.value_or(
        configError.empty() && appConfig.recording.enabled);
    const int retentionDays = configError.empty()
        ? appConfig.recording.retentionDays
        : 14;

    std::unique_ptr<CommandRecording> recording;
    json recordingMetadata = json::object();
    if (recordingEnabled) {
        try {
            CommandRecordingOptions recordingOptions;
            recordingOptions.enabled = true;
            recordingOptions.retentionDays = retentionDays;
            recordingOptions.appId = appId;
            recordingOptions.command = commandName;
            recordingOptions.surface = surface;
            recordingOptions.recordingId = recordingIdOverride.value_or("");
            if (operationDir.has_value()) {
                recordingOptions.statusMirrorPath = *operationDir / "recording.json";
            }
            recording = std::make_unique<CommandRecording>(std::move(recordingOptions));
        } catch (const std::exception& ex) {
            recordingMetadata = {
                {"recordingId", recordingIdOverride.value_or(NewCommandRecordingId())},
                {"status", "failed"},
                {"path", nullptr},
                {"startedAt", NowIsoUtc()},
                {"finishedAt", NowIsoUtc()},
                {"durationMs", 0},
                {"error", std::string("could not prepare recording: ") + ex.what()},
                {"appId", appId},
                {"command", commandName},
                {"surface", surface},
                {"commandStatus", "running"},
            };
        }
    }

    LuaRunOptions lua = BaseLuaOptions(options, executablePath, appPath, "run");
    if (lua.controlSessionToken.empty() &&
        (!operationDir.has_value() || !executablePath.empty())) {
        lua.acquireControlSession = true;
        lua.leaseOwner = "lua-app:" + appId + ":" + surface +
            (operationDir.has_value() ? ":" + operationDir->filename().string() : "");
        lua.leasePurpose = "run " + commandName;
        lua.leaseTtlMs = kAppCommandLeaseTtlMs;
        lua.leaseWaitMs = kAppCommandQueueWaitMs;
        lua.leaseMaxRuntimeMs = kAppCommandMaxRuntimeMs;
    }
    lua.vars["__ac_app_command"] = commandName;
    lua.vars["__ac_app_input_json"] = input.dump();
    if (operationDir.has_value()) {
        lua.vars["__ac_operation_dir"] = operationDir->string();
    }
    LuaRunResult result = RunLuaScriptCapture(lua, true);
    auto parsed = ParseJsonOutput(result, error);
    std::string commandStatus = "failed";
    if (parsed) {
        if (parsed->value("ok", false)) {
            commandStatus = "succeeded";
        } else if (parsed->value("code", "") == "operation_cancelled") {
            commandStatus = "cancelled";
        }
    }
    if (recording) {
        recording->Finish(commandStatus);
        recordingMetadata = recording->metadata();
    } else if (!recordingMetadata.empty()) {
        recordingMetadata["commandStatus"] = commandStatus;
    }
    if (recordingOut) {
        *recordingOut = recordingMetadata;
    }
    if (!parsed) {
        return std::nullopt;
    }
    if (!recordingMetadata.empty()) {
        if (!parsed->contains("data") || !(*parsed)["data"].is_object()) {
            (*parsed)["data"] = json::object();
        }
        (*parsed)["data"]["recording"] = recordingMetadata;
    }
    return *parsed;
}

json MakeInitialOperationRecord(
    const fs::path& appPath,
    const json& schema,
    const std::string& appId,
    const std::string& operationId,
    const std::string& commandName,
    bool recordingRequested,
    const std::string& surface
) {
    const std::string now = NowIsoUtc();
    json record = {
        {"operation", operationId},
        {"status", "pending"},
        {"command", commandName},
        {"app_id", appId},
        {"app_path", fs::absolute(appPath).lexically_normal().string()},
        {"app", {
            {"id", appId},
            {"path", fs::absolute(appPath).lexically_normal().string()},
            {"name", schema.value("name", "")},
            {"title", schema.value("title", "")},
            {"version", schema.value("version", "")},
        }},
        {"created_at", now},
        {"updated_at", now},
        {"started_at", nullptr},
        {"finished_at", nullptr},
        {"progress", nullptr},
        {"result_url", "/operations/" + operationId + "/result"},
        {"error", nullptr},
        {"approval", nullptr},
        {"cancel_requested", false},
        {"recording_requested", recordingRequested},
        {"recording_surface", surface},
    };
    if (recordingRequested) {
        const std::string recordingId = NewCommandRecordingId();
        record["recording"] = {
            {"recordingId", recordingId},
            {"status", "starting"},
            {"path", nullptr},
            {"startedAt", nullptr},
            {"finishedAt", nullptr},
            {"durationMs", nullptr},
            {"error", nullptr},
            {"appId", appId},
            {"command", commandName},
            {"surface", surface},
            {"commandStatus", "pending"},
        };
    }
    return record;
}

std::optional<json> ReadOperationRecord(const OperationPaths& paths, std::string& error) {
    auto record = ReadJsonFile(paths.operationJson, error);
    if (!record) {
        return std::nullopt;
    }
    std::string progressError;
    auto progress = ReadJsonFile(paths.progressJson, progressError);
    if (progress) {
        (*record)["progress"] = *progress;
    }
    std::string recordingError;
    auto recording = ReadJsonFile(paths.recordingJson, recordingError);
    if (recording) {
        (*record)["recording"] = *recording;
    }
    if (fs::exists(paths.approvalJson)) {
        std::string approvalError;
        auto approval = ReadJsonFile(paths.approvalJson, approvalError);
        if (approval && approval->is_object()) {
            (*record)["approval"] = *approval;
            const std::string status = record->value("status", "");
            if ((status == "pending" || status == "running") &&
                approval->value("status", "") == "pending") {
                (*record)["status"] = "waiting_for_approval";
            }
        }
    }
    return record;
}

bool WriteOperationRecord(const OperationPaths& paths, const json& record, std::string& error) {
    return WriteJsonFile(paths.operationJson, record, error);
}

void MarkOperationFailed(
    const OperationPaths& paths,
    json record,
    std::string code,
    std::string message,
    json details = json::object()
) {
    const std::string now = NowIsoUtc();
    record["status"] = "failed";
    record["updated_at"] = now;
    record["finished_at"] = now;
    record["error"] = {{"code", std::move(code)}, {"message", std::move(message)}};
    if (!details.empty()) {
        record["error"]["details"] = std::move(details);
    }
    std::string ignored;
    WriteJsonFile(paths.errorJson, record["error"], ignored);
    WriteOperationRecord(paths, record, ignored);
}

int RunStoredOperation(
    const CliOptions& options,
    const std::string& executablePath,
    const fs::path& appPath,
    const std::string& appId,
    const std::string& operationId
) {
    OperationPaths paths = PathsForOperation(appId, operationId);
    std::string error;
    auto recordOpt = ReadJsonFile(paths.operationJson, error);
    if (!recordOpt) {
        return 1;
    }
    auto inputOpt = ReadJsonFile(paths.inputJson, error);
    if (!inputOpt) {
        MarkOperationFailed(paths, *recordOpt, "invalid_input", error);
        return 1;
    }

    json record = *recordOpt;
    const std::string commandName = record.value("command", "");
    if (record.value("cancel_requested", false) || record.value("status", "") == "cancelled") {
        const std::string now = NowIsoUtc();
        record["status"] = "cancelled";
        record["updated_at"] = now;
        record["finished_at"] = now;
        record["error"] = {{"code", "operation_cancelled"}, {"message", "operation cancelled"}};
        if (record.value("recording_requested", false) &&
            record.contains("recording") &&
            record["recording"].is_object()) {
            record["recording"]["status"] = "failed";
            record["recording"]["finishedAt"] = now;
            record["recording"]["durationMs"] = 0;
            record["recording"]["error"] = "operation cancelled before recording started";
            record["recording"]["commandStatus"] = "cancelled";
        }
        WriteJsonFile(paths.errorJson, record["error"], error);
        WriteOperationRecord(paths, record, error);
        return 1;
    }

    const std::string startedAt = NowIsoUtc();
    record["status"] = "running";
    record["started_at"] = startedAt;
    record["updated_at"] = startedAt;
    if (!WriteOperationRecord(paths, record, error)) {
        return 1;
    }

    json recordingMetadata;
    const bool recordingRequested = record.value("recording_requested", false);
    std::optional<std::string> recordingId;
    if (recordingRequested && record.contains("recording") && record["recording"].is_object()) {
        const std::string value = record["recording"].value("recordingId", "");
        if (!value.empty()) {
            recordingId = value;
        }
    }
    auto payload = RunAppCommand(
        options,
        executablePath,
        appPath,
        commandName,
        *inputOpt,
        paths.dir,
        appId,
        record.value("recording_surface", "async"),
        recordingRequested,
        recordingId,
        &recordingMetadata,
        error);
    auto latest = ReadOperationRecord(paths, error).value_or(record);
    if (latest.value("cancel_requested", false) || latest.value("status", "") == "cancelled") {
        const std::string now = NowIsoUtc();
        latest["status"] = "cancelled";
        latest["updated_at"] = now;
        latest["finished_at"] = now;
        latest["error"] = {{"code", "operation_cancelled"}, {"message", "operation cancelled"}};
        WriteJsonFile(paths.errorJson, latest["error"], error);
        WriteOperationRecord(paths, latest, error);
        return 1;
    }

    if (!payload) {
        MarkOperationFailed(paths, latest, "internal_error", error);
        return 1;
    }

    const std::string now = NowIsoUtc();
    latest["updated_at"] = now;
    latest["finished_at"] = now;
    if (payload->value("ok", false)) {
        const json data = payload->value("data", json::object());
        const json result = data.value("result", json::object());
        if (!WriteJsonFile(paths.resultJson, result, error)) {
            MarkOperationFailed(paths, latest, "internal_error", error);
            return 1;
        }
        WriteTraceJsonl(paths.traceJsonl, data.value("trace", json::array()), error);
        if (data.contains("progress") && data["progress"].is_array() && !data["progress"].empty()) {
            latest["progress"] = data["progress"].back();
        }
        latest["status"] = "succeeded";
        latest["error"] = nullptr;
        WriteOperationRecord(paths, latest, error);
        return 0;
    }

    const std::string code = payload->value("code", "operation_failed");
    const std::string message = payload->value("error", "operation failed");
    const json data = payload->value("data", json::object());
    const json details = data.is_object() ? data.value("error", json::object()) : json::object();
    WriteTraceJsonl(paths.traceJsonl, data.value("trace", json::array()), error);
    latest["status"] = code == "operation_cancelled" ? "cancelled" : "failed";
    latest["error"] = {{"code", code}, {"message", message}};
    if (!details.empty()) {
        latest["error"]["details"] = details;
    }
    if (data.contains("progress") && data["progress"].is_array() && !data["progress"].empty()) {
        latest["progress"] = data["progress"].back();
    }
    WriteJsonFile(paths.errorJson, latest["error"], error);
    WriteOperationRecord(paths, latest, error);
    return latest["status"] == "cancelled" ? 1 : 1;
}

bool StartOperationProcess(
    const CliOptions& options,
    const std::string& executablePath,
    const fs::path& appPath,
    const std::string& appId,
    const std::string& operationId,
    std::string& error
) {
#if defined(__unix__) || defined(__APPLE__)
    if (!executablePath.empty()) {
        std::vector<std::string> command = {executablePath};
        command.push_back("--session");
        command.push_back(options.session);
        if (!options.controlScope.empty()) {
            command.push_back("--control-scope");
            command.push_back(options.controlScope);
        }
        if (!options.controlSessionToken.empty()) {
            command.push_back("--control-session");
            command.push_back(options.controlSessionToken);
        }
        command.push_back("app");
        command.push_back("operation");
        command.push_back("__run-stored");
        command.push_back(appPath.string());
        command.push_back(appId);
        command.push_back(operationId);

        pid_t pid = ::fork();
        if (pid < 0) {
            error = "failed to fork operation runner";
            return false;
        }
        if (pid == 0) {
            (void)::setsid();
            int devNull = ::open("/dev/null", O_RDWR);
            if (devNull >= 0) {
                ::dup2(devNull, STDIN_FILENO);
                ::dup2(devNull, STDOUT_FILENO);
                ::dup2(devNull, STDERR_FILENO);
                if (devNull > STDERR_FILENO) {
                    ::close(devNull);
                }
            }
            PosixArgv argv(command);
            ::execv(argv.front(), argv.data());
            _exit(127);
        }
        return true;
    }

    // Unit-test embeddings may not have a standalone CLI path. Production
    // callers always exec a fresh process so SQLite and runtime locks are not
    // inherited across fork.
    pid_t pid = ::fork();
    if (pid < 0) {
        error = "failed to fork operation runner";
        return false;
    }
    if (pid == 0) {
        (void)::setsid();
        int devNull = ::open("/dev/null", O_RDWR);
        if (devNull >= 0) {
            ::dup2(devNull, STDIN_FILENO);
            ::dup2(devNull, STDOUT_FILENO);
            ::dup2(devNull, STDERR_FILENO);
            if (devNull > STDERR_FILENO) {
                ::close(devNull);
            }
        }
        int code = RunStoredOperation(options, executablePath, appPath, appId, operationId);
        _exit(code == 0 ? 0 : 1);
    }
    return true;
#else
#if defined(_WIN32)
    std::vector<std::string> command = {executablePath};
    command.push_back("--session");
    command.push_back(options.session);
    if (!options.controlScope.empty()) {
        command.push_back("--control-scope");
        command.push_back(options.controlScope);
    }
    if (!options.controlSessionToken.empty()) {
        command.push_back("--control-session");
        command.push_back(options.controlSessionToken);
    }
    command.push_back("app");
    command.push_back("operation");
    command.push_back("__run-stored");
    command.push_back(appPath.string());
    command.push_back(appId);
    command.push_back(operationId);

    if (!Windows::LaunchDetached(command)) {
        error = "failed to start operation runner";
        return false;
    }
    return true;
#else
    (void)options;
    (void)executablePath;
    (void)appPath;
    (void)appId;
    (void)operationId;
    error = "async operations are not implemented on this platform";
    return false;
#endif
#endif
}

std::optional<json> CreateAsyncOperation(
    const CliOptions& options,
    const std::string& executablePath,
    const fs::path& appPath,
    const json& schema,
    const std::string& commandName,
    const json& input,
    const std::string& surface,
    std::string& error
) {
    const std::string appId = AppIdFor(appPath, schema);
    const std::string operationId = NewOperationId();
    OperationPaths paths = PathsForOperation(appId, operationId);
    EnsureDirectory(paths.dir);
    EnsureDirectory(paths.artifactsDir);

    std::string configError;
    const AppConfig config = LoadAppConfig(&configError);
    const bool recordingRequested = configError.empty() && config.recording.enabled;
    json record = MakeInitialOperationRecord(
        appPath,
        schema,
        appId,
        operationId,
        commandName,
        recordingRequested,
        surface);
    if (!WriteJsonFile(paths.inputJson, input, error)) {
        return std::nullopt;
    }
    if (!WriteOperationRecord(paths, record, error)) {
        return std::nullopt;
    }
    if (!StartOperationProcess(options, executablePath, appPath, appId, operationId, error)) {
        record["status"] = "failed";
        record["error"] = {{"code", "internal_error"}, {"message", error}};
        if (recordingRequested && record.contains("recording")) {
            record["recording"]["status"] = "failed";
            record["recording"]["finishedAt"] = NowIsoUtc();
            record["recording"]["durationMs"] = 0;
            record["recording"]["error"] = "operation process could not start";
            record["recording"]["commandStatus"] = "failed";
        }
        WriteOperationRecord(paths, record, error);
        return std::nullopt;
    }
    return PublicOperationRecord(record);
}

int PrintData(const CliOptions& options, const json& data) {
    if (options.jsonOutput) {
        std::cout << json({{"ok", true}, {"data", data}}).dump(2) << "\n";
    } else {
        std::cout << data.dump(2) << "\n";
    }
    return 0;
}

json ShapeRunPayloadForCli(json payload, const AppRunArgs& args, const std::string& commandName) {
    if (!payload.contains("data") || !payload["data"].is_object()) {
        return payload;
    }
    if (args.trace && args.traceDir.has_value() && payload["data"].contains("trace")) {
        std::string error;
        fs::path tracePath = *args.traceDir / (SanitizeIdPart(commandName) + "-" + NewOperationId() + ".jsonl");
        if (WriteTraceJsonl(tracePath, payload["data"]["trace"], error)) {
            payload["data"]["trace_path"] = tracePath.string();
        } else {
            payload["data"]["trace_write_error"] = error;
        }
    }
    if (!args.trace) {
        payload["data"].erase("trace");
    }
    return payload;
}

void PrintRecordingStatusToStderr(const json& recording) {
    if (!recording.is_object() || recording.empty()) {
        return;
    }
    const std::string status = recording.value("status", "");
    const std::string path =
        recording.contains("path") && recording["path"].is_string()
        ? recording["path"].get<std::string>()
        : "";
    if (status == "recorded" && !path.empty()) {
        std::cerr << "Recording: " << path << "\n";
        return;
    }
    std::cerr << "Recording: " << (status.empty() ? "failed" : status);
    const std::string recordingError = RecordingErrorText(recording);
    if (!recordingError.empty()) {
        std::cerr << " (" << recordingError << ")";
    }
    std::cerr << "\n";
}

std::optional<OperationPaths> OperationPathsForCli(
    const CliOptions& options,
    const std::string& executablePath,
    const fs::path& appPath,
    const std::string& operationId,
    std::string& error
) {
    auto schema = LoadAppSchema(options, executablePath, appPath, error);
    if (!schema) {
        return std::nullopt;
    }
    return PathsForOperation(AppIdFor(appPath, *schema), operationId);
}

std::optional<json> ReadKnownOperation(
    const OperationPaths& paths,
    const std::string& operationId,
    std::string& error
) {
    if (!fs::exists(paths.operationJson)) {
        error = "unknown operation: " + operationId;
        return std::nullopt;
    }
    return ReadOperationRecord(paths, error);
}

int HandleOperationGet(
    const CliOptions& options,
    const std::vector<std::string>& args,
    const std::string& executablePath
) {
    if (args.size() != 5) {
        return ErrorExit(options, "app operation get requires <app.lua> <operation-id>", 2, "invalid_input");
    }
    const fs::path appPath = args[3];
    const std::string operationId = args[4];
    std::string error;
    auto paths = OperationPathsForCli(options, executablePath, appPath, operationId, error);
    if (!paths) {
        return ErrorExit(options, error, 1, "invalid_app");
    }
    auto record = ReadKnownOperation(*paths, operationId, error);
    if (!record) {
        return ErrorExit(options, error, 2, "unknown_operation");
    }
    return PrintData(options, PublicOperationRecord(*record));
}

std::optional<int64_t> ParseWaitSeconds(
    const std::vector<std::string>& args,
    size_t start,
    std::string& error
) {
    int64_t waitSeconds = 0;
    for (size_t i = start; i < args.size(); ++i) {
        if (args[i] != "--wait") {
            error = "unknown operation result option: " + args[i];
            return std::nullopt;
        }
        if (i + 1 >= args.size()) {
            error = "operation result --wait requires seconds";
            return std::nullopt;
        }
        if (!ParseInteger(args[++i], waitSeconds) || waitSeconds < 0) {
            error = "operation result --wait requires non-negative seconds";
            return std::nullopt;
        }
    }
    return waitSeconds;
}

std::optional<json> WaitForOperationResult(
    const OperationPaths& paths,
    const std::string& operationId,
    int64_t waitSeconds,
    std::string& error
) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(waitSeconds);
    while (true) {
        auto record = ReadKnownOperation(paths, operationId, error);
        if (!record) {
            return std::nullopt;
        }
        const std::string status = record->value("status", "");
        if (IsFinalStatus(status) || waitSeconds == 0 || std::chrono::steady_clock::now() >= deadline) {
            return record;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

std::optional<json> OperationResultView(
    const OperationPaths& paths,
    const std::string& operationId,
    const json& record,
    std::string& error
) {
    const std::string status = record.value("status", "");
    json view = {
        {"operation", operationId},
        {"status", status},
        {"result", nullptr},
    };
    if (record.contains("approval")) view["approval"] = record["approval"];
    if (record.contains("recording")) view["recording"] = record["recording"];
    if (status == "succeeded") {
        auto result = ReadJsonFile(paths.resultJson, error);
        if (!result) return std::nullopt;
        view["result"] = *result;
    } else if (record.contains("error") && !record["error"].is_null()) {
        view["error"] = record["error"];
    }
    return view;
}

int HandleOperationResult(
    const CliOptions& options,
    const std::vector<std::string>& args,
    const std::string& executablePath
) {
    if (args.size() < 5) {
        return ErrorExit(options, "app operation result requires <app.lua> <operation-id>", 2, "invalid_input");
    }
    const fs::path appPath = args[3];
    const std::string operationId = args[4];
    std::string error;
    auto waitSeconds = ParseWaitSeconds(args, 5, error);
    if (!waitSeconds) {
        return ErrorExit(options, error, 2, "invalid_input");
    }
    auto paths = OperationPathsForCli(options, executablePath, appPath, operationId, error);
    if (!paths) {
        return ErrorExit(options, error, 1, "invalid_app");
    }
    auto record = WaitForOperationResult(*paths, operationId, *waitSeconds, error);
    if (!record) {
        return ErrorExit(options, error, 2, "unknown_operation");
    }

    auto data = OperationResultView(*paths, operationId, *record, error);
    if (!data) return ErrorExit(options, error, 1, "internal_error");
    const std::string status = data->value("status", "");
    const json recording = data->value("recording", json::object());
    if (!options.jsonOutput) data->erase("recording");
    if (!IsFinalStatus(status) || status == "succeeded") {
        const int code = PrintData(options, *data);
        if (!options.jsonOutput && status == "succeeded") PrintRecordingStatusToStderr(recording);
        return code;
    }
    const json errorData = data->value("error", json::object());
    if (options.jsonOutput) {
        json payload = ErrorPayload(
            errorData.value("code", status == "cancelled" ? "operation_cancelled" : "operation_failed"),
            errorData.value("message", status == "cancelled" ? "operation cancelled" : "operation failed")
        );
        payload["data"] = *data;
        std::cout << payload.dump(2) << "\n";
    } else {
        std::cout << data->dump(2) << "\n";
        PrintRecordingStatusToStderr(recording);
    }
    return 1;
}

std::optional<json> CancelOperationRecord(const OperationPaths& paths, const std::string& operationId, std::string& error) {
    auto record = ReadKnownOperation(paths, operationId, error);
    if (!record) {
        return std::nullopt;
    }

    const std::string status = record->value("status", "");
    if (status == "pending" || status == "running" || status == "waiting_for_approval") {
        const std::string now = NowIsoUtc();
        (*record)["status"] = "cancelled";
        (*record)["cancel_requested"] = true;
        (*record)["updated_at"] = now;
        (*record)["finished_at"] = now;
        (*record)["error"] = {{"code", "operation_cancelled"}, {"message", "operation cancelled"}};
        if (status == "pending" &&
            record->value("recording_requested", false) &&
            record->contains("recording") &&
            (*record)["recording"].is_object()) {
            (*record)["recording"]["status"] = "failed";
            (*record)["recording"]["finishedAt"] = now;
            (*record)["recording"]["durationMs"] = 0;
            (*record)["recording"]["error"] = "operation cancelled before recording started";
            (*record)["recording"]["commandStatus"] = "cancelled";
        }
        if (!WriteJsonFile(paths.errorJson, (*record)["error"], error) || !WriteOperationRecord(paths, *record, error)) {
            return std::nullopt;
        }
    }
    return json({{"operation", operationId}, {"status", (*record)["status"]}});
}

std::optional<json> RespondApprovalRecord(
    const OperationPaths& paths,
    const std::string& operationId,
    const std::string& approvalId,
    bool approved,
    const std::string& note,
    std::string& error
) {
    auto record = ReadKnownOperation(paths, operationId, error);
    if (!record) return std::nullopt;
    if (IsFinalStatus(record->value("status", ""))) {
        error = "operation is already complete";
        return std::nullopt;
    }
    if (!fs::exists(paths.approvalJson)) {
        error = "operation has no approval request";
        return std::nullopt;
    }
    auto approval = ReadJsonFile(paths.approvalJson, error);
    if (!approval || !approval->is_object()) return std::nullopt;
    if (approval->value("id", "") != approvalId) {
        error = "approval id does not match the current request";
        return std::nullopt;
    }
    if (approval->value("status", "") != "pending") {
        error = "approval request has already been answered";
        return std::nullopt;
    }
    if (approval->contains("expires_at") && (*approval)["expires_at"].is_number_integer() &&
        (*approval)["expires_at"].get<int64_t>() <= NowEpochSeconds()) {
        (*approval)["status"] = "expired";
        (*approval)["responded_at"] = NowEpochSeconds();
        std::string writeError;
        if (!WriteJsonFile(paths.approvalJson, *approval, writeError)) {
            error = writeError;
            return std::nullopt;
        }
        error = "approval request has expired";
        return std::nullopt;
    }
    (*approval)["status"] = approved ? "approved" : "denied";
    (*approval)["responded_at"] = NowEpochSeconds();
    if (!note.empty()) (*approval)["note"] = note;
    if (!WriteJsonFile(paths.approvalJson, *approval, error)) return std::nullopt;
    return json({
        {"operation", operationId},
        {"status", "running"},
        {"approval", *approval},
    });
}

int HandleOperationApproval(
    const CliOptions& options,
    const std::vector<std::string>& args,
    const std::string& executablePath,
    bool approved
) {
    if (args.size() < 6) {
        return ErrorExit(
            options,
            std::string("app operation ") + (approved ? "approve" : "deny") +
                " requires <app.lua> <operation-id> <approval-id>",
            2,
            "invalid_input");
    }
    std::string note;
    for (size_t i = 6; i < args.size(); ++i) {
        if (args[i] != "--note" || i + 1 >= args.size()) {
            return ErrorExit(options, "approval response accepts only --note <text>", 2, "invalid_input");
        }
        note = args[++i];
    }
    std::string error;
    auto paths = OperationPathsForCli(options, executablePath, args[3], args[4], error);
    if (!paths) return ErrorExit(options, error, 1, "invalid_app");
    auto response = RespondApprovalRecord(*paths, args[4], args[5], approved, note, error);
    if (!response) {
        return ErrorExit(
            options,
            error,
            fs::exists(paths->operationJson) ? 1 : 2,
            fs::exists(paths->operationJson) ? "approval_conflict" : "unknown_operation");
    }
    return PrintData(options, *response);
}

int HandleOperationCancel(
    const CliOptions& options,
    const std::vector<std::string>& args,
    const std::string& executablePath
) {
    if (args.size() != 5) {
        return ErrorExit(options, "app operation cancel requires <app.lua> <operation-id>", 2, "invalid_input");
    }
    const fs::path appPath = args[3];
    const std::string operationId = args[4];
    std::string error;
    auto paths = OperationPathsForCli(options, executablePath, appPath, operationId, error);
    if (!paths) {
        return ErrorExit(options, error, 1, "invalid_app");
    }
    auto cancelled = CancelOperationRecord(*paths, operationId, error);
    if (!cancelled) {
        return ErrorExit(options, error, fs::exists(paths->operationJson) ? 1 : 2, fs::exists(paths->operationJson) ? "internal_error" : "unknown_operation");
    }
    return PrintData(options, *cancelled);
}

int HandleAppOperation(
    const CliOptions& options,
    const std::vector<std::string>& args,
    const std::string& executablePath
) {
    if (args.size() < 3) {
        return ErrorExit(options, "app operation requires get, result, approve, deny, or cancel", 2, "invalid_input");
    }
    if (args[2] == "get") {
        return HandleOperationGet(options, args, executablePath);
    }
    if (args[2] == "result") {
        return HandleOperationResult(options, args, executablePath);
    }
    if (args[2] == "cancel") {
        return HandleOperationCancel(options, args, executablePath);
    }
    if (args[2] == "approve") {
        return HandleOperationApproval(options, args, executablePath, true);
    }
    if (args[2] == "deny") {
        return HandleOperationApproval(options, args, executablePath, false);
    }
    if (args[2] == "__run-stored" && args.size() == 6) {
        return RunStoredOperation(options, executablePath, args[3], args[4], args[5]);
    }
    return ErrorExit(options, "unknown app operation subcommand: " + args[2], 2, "invalid_input");
}

struct AppServeOptions {
    fs::path appPath;
    std::string host = "0.0.0.0";
    int port = 8787;
    std::string authToken;
    std::set<std::string> allowedOrigins;
    std::optional<fs::path> trayStateFile;
    std::string trayConfigName;
    std::string trayDisplayName;
};

struct HttpRequest {
    std::string method;
    std::string target;
    std::string path;
    std::map<std::string, std::string> query;
    std::map<std::string, std::string> headers;
    std::string body;
};

std::string LowercaseAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string TrimTrailingSlash(std::string value) {
    while (value.size() > 1 && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

std::optional<std::string> NormalizeOrigin(const std::string& value) {
    std::string origin = Trim(value);
    if (origin.empty() || origin == "null") {
        return std::nullopt;
    }
    origin = TrimTrailingSlash(origin);
    std::string lowered = LowercaseAscii(origin);
    std::string_view scheme;
    size_t authorityStart = 0;
    if (lowered.rfind("http://", 0) == 0) {
        scheme = "http://";
        authorityStart = 7;
    } else if (lowered.rfind("https://", 0) == 0) {
        scheme = "https://";
        authorityStart = 8;
    } else {
        return std::nullopt;
    }
    const size_t authorityEnd = lowered.find_first_of("/?#", authorityStart);
    if (authorityEnd != std::string::npos) {
        return std::nullopt;
    }
    std::string authority = lowered.substr(authorityStart);
    if (authority.empty() || authority.find('@') != std::string::npos) {
        return std::nullopt;
    }
    return std::string(scheme) + authority;
}

void AddDefaultAllowedOrigins(AppServeOptions& serve) {
    auto add = [&](const std::string& origin) {
        if (auto normalized = NormalizeOrigin(origin)) {
            serve.allowedOrigins.insert(*normalized);
        }
    };
    add("http://127.0.0.1:" + std::to_string(serve.port));
    add("http://localhost:" + std::to_string(serve.port));
    const std::string bindHost = serve.host == "localhost" ? "127.0.0.1" : serve.host;
    if (bindHost != "0.0.0.0" && bindHost != "127.0.0.1") {
        add("http://" + bindHost + ":" + std::to_string(serve.port));
    }
}

bool OriginAllowed(const HttpRequest& request, const AppServeOptions& serveOptions) {
    auto it = request.headers.find("origin");
    if (it == request.headers.end() || IsBlank(it->second)) {
        return true;
    }
    auto origin = NormalizeOrigin(it->second);
    if (!origin) {
        return false;
    }
    return serveOptions.allowedOrigins.count(*origin) > 0;
}

bool HeaderAccepts(const HttpRequest& request, const std::string& mediaType) {
    auto it = request.headers.find("accept");
    if (it == request.headers.end()) {
        return false;
    }
    std::string accept = LowercaseAscii(it->second);
    std::string wanted = LowercaseAscii(mediaType);
    size_t start = 0;
    while (start <= accept.size()) {
        const size_t comma = accept.find(',', start);
        std::string part = Trim(accept.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        const size_t semicolon = part.find(';');
        if (semicolon != std::string::npos) {
            part = Trim(part.substr(0, semicolon));
        }
        if (part == wanted || part == "*/*") {
            return true;
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return false;
}

std::string UrlDecode(const std::string& value) {
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const std::string hex = value.substr(i + 1, 2);
            char* end = nullptr;
            long decoded = std::strtol(hex.c_str(), &end, 16);
            if (end != nullptr && *end == '\0') {
                out.push_back(static_cast<char>(decoded));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i] == '+' ? ' ' : value[i]);
    }
    return out;
}

std::map<std::string, std::string> ParseQueryString(const std::string& query) {
    std::map<std::string, std::string> out;
    size_t start = 0;
    while (start <= query.size()) {
        const size_t amp = query.find('&', start);
        const std::string part = query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        if (!part.empty()) {
            const size_t eq = part.find('=');
            if (eq == std::string::npos) {
                out[UrlDecode(part)] = "";
            } else {
                out[UrlDecode(part.substr(0, eq))] = UrlDecode(part.substr(eq + 1));
            }
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return out;
}

void SplitTarget(HttpRequest& request) {
    const size_t question = request.target.find('?');
    if (question == std::string::npos) {
        request.path = request.target;
        return;
    }
    request.path = request.target.substr(0, question);
    request.query = ParseQueryString(request.target.substr(question + 1));
}

bool SendAll(AppSocket fd, const std::string& data) {
#if defined(__unix__) || defined(__APPLE__)
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
#elif defined(_WIN32)
    size_t sent = 0;
    while (sent < data.size()) {
        int chunk = static_cast<int>(std::min<size_t>(data.size() - sent, 64 * 1024));
        int n = ::send(fd, data.data() + sent, chunk, 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
#else
    (void)fd;
    (void)data;
    return false;
#endif
}

std::string ReasonPhrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 202: return "Accepted";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 500: return "Internal Server Error";
        case 504: return "Gateway Timeout";
        default: return "OK";
    }
}

std::map<std::string, std::string> RecordingHeaders(const json& recording) {
    return RecordingHttpHeaders(recording);
}

bool SendJsonResponse(
    AppSocket fd,
    int status,
    const json& body,
    const std::map<std::string, std::string>& extraHeaders = {}
) {
    const std::string payload = body.dump(2);
    std::ostringstream response;
    response << "HTTP/1.1 " << status << " " << ReasonPhrase(status) << "\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: " << payload.size() << "\r\n";
    response << "Connection: close\r\n";
    for (const auto& [name, value] : extraHeaders) {
        response << name << ": " << value << "\r\n";
    }
    response << "\r\n";
    response << payload;
    return SendAll(fd, response.str());
}

bool SendEmptyResponse(
    AppSocket fd,
    int status,
    const std::map<std::string, std::string>& extraHeaders = {}
) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << " " << ReasonPhrase(status) << "\r\n";
    response << "Content-Length: 0\r\n";
    response << "Connection: close\r\n";
    for (const auto& [name, value] : extraHeaders) {
        response << name << ": " << value << "\r\n";
    }
    response << "\r\n";
    return SendAll(fd, response.str());
}

json HttpErrorBody(std::string code, std::string message, json details = json::object()) {
    json error = {
        {"code", std::move(code)},
        {"message", std::move(message)}
    };
    if (!details.empty()) {
        error["details"] = std::move(details);
    }
    return {{"error", error}};
}

int HttpStatusForErrorCode(const std::string& code) {
    if (code == "invalid_input" || code == "invalid_app") return 400;
    if (code == "unknown_command" || code == "unknown_operation") return 404;
    if (code == "operation_cancelled" || code == "approval_conflict" ||
        code == "approval_denied" || code == "approval_requires_async") return 409;
    if (code == "permission_denied") return 403;
    if (code == "timeout" || code == "runtime_timeout" || code == "approval_timeout") return 504;
    return 500;
}

bool ReadHttpRequest(AppSocket fd, HttpRequest& request, std::string& error) {
#if defined(__unix__) || defined(__APPLE__) || defined(_WIN32)
    std::string raw;
    std::array<char, 4096> buffer {};
    size_t headerEnd = std::string::npos;
    while ((headerEnd = raw.find("\r\n\r\n")) == std::string::npos) {
#if defined(_WIN32)
        int n = ::recv(fd, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
        ssize_t n = ::recv(fd, buffer.data(), buffer.size(), 0);
#endif
        if (n <= 0) {
            error = "failed to read HTTP request";
            return false;
        }
        raw.append(buffer.data(), static_cast<size_t>(n));
        if (raw.size() > 1024 * 1024) {
            error = "HTTP request headers are too large";
            return false;
        }
    }

    std::istringstream headers(raw.substr(0, headerEnd));
    std::string requestLine;
    if (!std::getline(headers, requestLine)) {
        error = "missing HTTP request line";
        return false;
    }
    if (!requestLine.empty() && requestLine.back() == '\r') {
        requestLine.pop_back();
    }
    std::istringstream requestLineStream(requestLine);
    std::string version;
    requestLineStream >> request.method >> request.target >> version;
    if (request.method.empty() || request.target.empty()) {
        error = "invalid HTTP request line";
        return false;
    }
    SplitTarget(request);

    std::string line;
    while (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string name = LowercaseAscii(Trim(line.substr(0, colon)));
        std::string value = Trim(line.substr(colon + 1));
        request.headers[std::move(name)] = std::move(value);
    }

    size_t contentLength = 0;
    if (auto it = request.headers.find("content-length"); it != request.headers.end()) {
        int64_t parsed = 0;
        if (!ParseInteger(it->second, parsed) || parsed < 0) {
            error = "invalid Content-Length";
            return false;
        }
        contentLength = static_cast<size_t>(parsed);
    }
    const size_t bodyStart = headerEnd + 4;
    request.body = raw.substr(bodyStart);
    while (request.body.size() < contentLength) {
#if defined(_WIN32)
        int n = ::recv(fd, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
        ssize_t n = ::recv(fd, buffer.data(), buffer.size(), 0);
#endif
        if (n <= 0) {
            error = "failed to read HTTP request body";
            return false;
        }
        request.body.append(buffer.data(), static_cast<size_t>(n));
    }
    if (request.body.size() > contentLength) {
        request.body.resize(contentLength);
    }
    return true;
#else
    (void)fd;
    (void)request;
    error = "HTTP server is not implemented on this platform";
    return false;
#endif
}

bool Authorized(const HttpRequest& request, const AppServeOptions& serveOptions) {
    if (serveOptions.authToken.empty()) {
        return true;
    }
    auto it = request.headers.find("authorization");
    return it != request.headers.end() && it->second == "Bearer " + serveOptions.authToken;
}

bool QueryFlagTrue(const std::map<std::string, std::string>& query, const std::string& key) {
    auto it = query.find(key);
    if (it == query.end()) {
        return false;
    }
    if (it->second.empty()) {
        return true;
    }
    bool out = false;
    return JsonTruthyString(it->second, out) && out;
}

int64_t QueryWaitSeconds(const std::map<std::string, std::string>& query) {
    auto it = query.find("wait");
    if (it == query.end()) {
        return 0;
    }
    int64_t wait = 0;
    if (!ParseInteger(it->second, wait) || wait < 0) {
        return -1;
    }
    return wait;
}

bool ParseJsonObjectBody(const HttpRequest& request, json& body, std::string& error) {
    if (request.body.empty()) {
        body = json::object();
        return true;
    }
    body = json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        error = "request body must be a JSON object";
        return false;
    }
    return true;
}

bool StartsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool IsSupportedMcpProtocolVersion(const std::string& version) {
    return version == "2025-11-25" || version == "2025-06-18" || version == "2025-03-26";
}

bool McpProtocolHeaderSupported(const HttpRequest& request) {
    auto it = request.headers.find("mcp-protocol-version");
    if (it == request.headers.end() || IsBlank(it->second)) {
        return true;
    }
    return IsSupportedMcpProtocolVersion(Trim(it->second));
}

std::string NegotiateMcpProtocolVersion(const json& params) {
    const std::string requested = params.value("protocolVersion", std::string(kMcpLatestProtocolVersion));
    if (IsSupportedMcpProtocolVersion(requested)) {
        return requested;
    }
    return std::string(kMcpLatestProtocolVersion);
}

bool ValidJsonRpcId(const json& id) {
    return id.is_string() || id.is_number_integer() || id.is_number_unsigned();
}

json JsonRpcIdOrNull(const json& message) {
    if (message.is_object() && message.contains("id") && ValidJsonRpcId(message["id"])) {
        return message["id"];
    }
    return nullptr;
}

json JsonRpcError(json id, int code, std::string message, json data = nullptr) {
    json error = {
        {"code", code},
        {"message", std::move(message)}
    };
    if (!data.is_null()) {
        error["data"] = std::move(data);
    }
    return {
        {"jsonrpc", "2.0"},
        {"id", std::move(id)},
        {"error", std::move(error)}
    };
}

json JsonRpcResult(json id, json result) {
    return {
        {"jsonrpc", "2.0"},
        {"id", std::move(id)},
        {"result", std::move(result)}
    };
}

bool SendJsonRpcError(
    AppSocket fd,
    int httpStatus,
    json id,
    int code,
    std::string message,
    json data = nullptr
) {
    return SendJsonResponse(fd, httpStatus, JsonRpcError(std::move(id), code, std::move(message), std::move(data)));
}

json McpServerInfo(const json& schema) {
    json info = {
        {"name", schema.value("name", "computer.cpp")},
        {"title", schema.value("title", schema.value("name", "computer.cpp"))},
        {"version", schema.value("version", "")}
    };
    if (schema.contains("description") && schema["description"].is_string()) {
        info["description"] = schema["description"];
    }
    return info;
}

json McpInitializeResult(const json& schema, const json& params) {
    return {
        {"protocolVersion", NegotiateMcpProtocolVersion(params)},
        {"capabilities", {
            {"tools", {
                {"listChanged", false}
            }}
        }},
        {"serverInfo", McpServerInfo(schema)},
        {"instructions", "This MCP server exposes semantic computer.cpp Lua app commands as tools. Tool calls may operate local desktop applications; review tool inputs before invoking them."}
    };
}

void AddMcpTool(
    json& tools,
    std::string name,
    std::string description,
    json properties,
    json required
) {
    tools.push_back({
        {"name", std::move(name)},
        {"description", std::move(description)},
        {"inputSchema", {
            {"type", "object"},
            {"properties", std::move(properties)},
            {"required", std::move(required)},
            {"additionalProperties", false},
        }},
    });
}

json MakeMcpToolsList(const json& schema) {
    json tools = json::array();
    const json commands = schema.value("commands", json::object());
    for (auto it = commands.begin(); it != commands.end(); ++it) {
        if (it.key().rfind("computer_cpp_", 0) == 0) continue;
        const json command = it.value().is_object() ? it.value() : json::object();
        json tool = {
            {"name", it.key()},
            {"description", command.value("description", "")},
            {"inputSchema", command.value("input", json({{"type", "object"}, {"additionalProperties", false}}))}
        };
        if (command.contains("output") && command["output"].is_object()) {
            tool["outputSchema"] = command["output"];
        }
        tools.push_back(std::move(tool));
    }
    const json emptyObject = {
        {"type", "object"},
        {"properties", json::object()},
        {"additionalProperties", true},
    };
    AddMcpTool(
        tools,
        "computer_cpp_operation_start",
        "Start one app command asynchronously so it can pause for approval.",
        {{"command", {{"type", "string"}}}, {"arguments", emptyObject}},
        {"command"});
    AddMcpTool(
        tools,
        "computer_cpp_operation_status",
        "Inspect or briefly wait for an asynchronous operation, including approval and result state.",
        {
            {"operation", {{"type", "string"}}},
            {"waitSeconds", {{"type", "integer"}, {"minimum", 0}, {"maximum", 30}, {"default", 0}}},
        },
        {"operation"});
    AddMcpTool(
        tools,
        "computer_cpp_operation_respond",
        "Approve or deny the exact current approval request for an asynchronous operation.",
        {
            {"operation", {{"type", "string"}}},
            {"approvalId", {{"type", "string"}}},
            {"decision", {{"type", "string"}, {"enum", {"approve", "deny"}}}},
            {"note", {{"type", "string"}}},
        },
        {"operation", "approvalId", "decision"});
    AddMcpTool(
        tools,
        "computer_cpp_operation_cancel",
        "Cancel an asynchronous app operation.",
        {{"operation", {{"type", "string"}}}},
        {"operation"});
    return {{"tools", std::move(tools)}};
}

json McpTextContent(std::string text) {
    return {
        {"type", "text"},
        {"text", std::move(text)}
    };
}

json McpToolSuccessResult(const json& result, const json& recording = json::object()) {
    json out = {
        {"content", json::array({McpTextContent(result.is_string() ? result.get<std::string>() : result.dump(2))})},
        {"isError", false}
    };
    if (result.is_object()) {
        out["structuredContent"] = result;
    }
    AttachMcpRecordingMetadata(out, recording);
    return out;
}

json McpToolErrorResult(
    const std::string& code,
    const std::string& message,
    const json& recording = json::object()
) {
    json out = {
        {"content", json::array({McpTextContent(code + ": " + message)})},
        {"isError", true}
    };
    AttachMcpRecordingMetadata(out, recording);
    return out;
}

bool HandleMcpJsonRpcRequest(
    AppSocket fd,
    const CliOptions& options,
    const std::string& executablePath,
    const AppServeOptions& serveOptions,
    const json& schema,
    const json& message
) {
    const json id = JsonRpcIdOrNull(message);
    const std::string method = message.value("method", "");
    const json params = message.value("params", json::object());
    if (message.contains("params") && !message["params"].is_object()) {
        return SendJsonRpcError(fd, 400, id, -32602, "params must be an object");
    }

    if (method == "initialize") {
        return SendJsonResponse(fd, 200, JsonRpcResult(id, McpInitializeResult(schema, params)));
    }
    if (method == "ping") {
        return SendJsonResponse(fd, 200, JsonRpcResult(id, json::object()));
    }
    if (method == "tools/list") {
        return SendJsonResponse(fd, 200, JsonRpcResult(id, MakeMcpToolsList(schema)));
    }
    if (method == "tools/call") {
        const std::string toolName = params.value("name", "");
        if (toolName.empty()) {
            return SendJsonRpcError(fd, 400, id, -32602, "tools/call requires params.name");
        }
        json arguments = json::object();
        if (params.contains("arguments")) {
            if (!params["arguments"].is_object()) {
                return SendJsonRpcError(fd, 400, id, -32602, "tools/call params.arguments must be an object");
            }
            arguments = params["arguments"];
        }
        const json commands = schema.value("commands", json::object());
        const std::string appId = AppIdFor(serveOptions.appPath, schema);
        const auto sendOperationSuccess = [&](const json& result) {
            return SendJsonResponse(
                fd,
                200,
                JsonRpcResult(id, McpToolSuccessResult(result)));
        };
        const auto sendOperationError = [&](const std::string& code, const std::string& error) {
            return SendJsonResponse(
                fd,
                200,
                JsonRpcResult(id, McpToolErrorResult(code, error)));
        };
        const auto stringArg = [&](const char* name) {
            return arguments.contains(name) && arguments[name].is_string()
                ? arguments[name].get<std::string>()
                : std::string();
        };
        if (toolName == "computer_cpp_operation_start") {
            const std::string command = stringArg("command");
            if (command.empty() ||
                (arguments.contains("arguments") && !arguments["arguments"].is_object())) {
                return sendOperationError(
                    "invalid_input",
                    "command is required and arguments must be an object");
            }
            const json commandArguments =
                arguments.contains("arguments") ? arguments["arguments"] : json::object();
            if (!commands.contains(command)) {
                return sendOperationError("unknown_command", "unknown command: " + command);
            }
            std::string error;
            auto operation = CreateAsyncOperation(
                options,
                executablePath,
                serveOptions.appPath,
                schema,
                command,
                commandArguments,
                "mcp",
                error);
            if (!operation) return sendOperationError("internal_error", error);
            return sendOperationSuccess(*operation);
        }
        if (toolName == "computer_cpp_operation_status") {
            const std::string operationId = stringArg("operation");
            const int waitSeconds =
                !arguments.contains("waitSeconds") ? 0
                : arguments["waitSeconds"].is_number_integer()
                    ? arguments["waitSeconds"].get<int>()
                    : -1;
            if (operationId.empty() || waitSeconds < 0 || waitSeconds > 30) {
                return sendOperationError(
                    "invalid_input",
                    "operation is required and waitSeconds must be between 0 and 30");
            }
            std::string error;
            OperationPaths paths = PathsForOperation(appId, operationId);
            auto record = WaitForOperationResult(paths, operationId, waitSeconds, error);
            if (!record) return sendOperationError("unknown_operation", error);
            auto result = OperationResultView(paths, operationId, *record, error);
            if (!result) return sendOperationError("internal_error", error);
            result->erase("recording");
            return sendOperationSuccess(*result);
        }
        if (toolName == "computer_cpp_operation_respond") {
            const std::string operationId = stringArg("operation");
            const std::string approvalId = stringArg("approvalId");
            const std::string decision = stringArg("decision");
            if (operationId.empty() || approvalId.empty() ||
                (decision != "approve" && decision != "deny") ||
                (arguments.contains("note") && !arguments["note"].is_string())) {
                return sendOperationError(
                    "invalid_input",
                    "operation, approvalId, and an approve or deny decision are required");
            }
            std::string error;
            OperationPaths paths = PathsForOperation(appId, operationId);
            auto response = RespondApprovalRecord(
                paths,
                operationId,
                approvalId,
                decision == "approve",
                arguments.contains("note") ? arguments["note"].get<std::string>() : "",
                error);
            if (!response) return sendOperationError("approval_conflict", error);
            return sendOperationSuccess(*response);
        }
        if (toolName == "computer_cpp_operation_cancel") {
            const std::string operationId = stringArg("operation");
            if (operationId.empty()) {
                return sendOperationError("invalid_input", "operation is required");
            }
            std::string error;
            OperationPaths paths = PathsForOperation(appId, operationId);
            auto cancelled = CancelOperationRecord(paths, operationId, error);
            if (!cancelled) return sendOperationError("unknown_operation", error);
            return sendOperationSuccess(*cancelled);
        }
        if (toolName.rfind("computer_cpp_", 0) == 0) {
            return SendJsonRpcError(fd, 400, id, -32602, "Unknown tool: " + toolName);
        }
        if (!commands.contains(toolName)) {
            return SendJsonRpcError(fd, 400, id, -32602, "Unknown tool: " + toolName);
        }

        AppendRuntimeLog("server", "mcp_tool_start", {{"tool", toolName}});
        std::string error;
        json recording;
        auto payload = RunAppCommand(
            options,
            executablePath,
            serveOptions.appPath,
            toolName,
            arguments,
            std::nullopt,
            appId,
            "mcp",
            std::nullopt,
            std::nullopt,
            &recording,
            error);
        if (!payload) {
            AppendRuntimeLog("server", "mcp_tool_failed", {{"tool", toolName}, {"error", error.empty() ? "tool execution failed" : error}});
            json errorData = McpRecordingErrorData(recording);
            return SendJsonRpcError(
                fd,
                500,
                id,
                -32603,
                error.empty() ? "tool execution failed" : error,
                std::move(errorData));
        }
        if (!payload->value("ok", false)) {
            const std::string code = payload->value("code", "operation_failed");
            const std::string messageText = payload->value("error", "operation failed");
            AppendRuntimeLog("server", "mcp_tool_failed", {{"tool", toolName}, {"code", code}, {"error", messageText}});
            return SendJsonResponse(fd, 200, JsonRpcResult(id, McpToolErrorResult(code, messageText, recording)));
        }

        const json data = payload->value("data", json::object());
        const json result = data.value("result", json::object());
        AppendRuntimeLog("server", "mcp_tool_done", {{"tool", toolName}});
        return SendJsonResponse(fd, 200, JsonRpcResult(id, McpToolSuccessResult(result, recording)));
    }

    return SendJsonRpcError(fd, 404, id, -32601, "Method not found: " + method);
}

bool HandleMcpRequest(
    AppSocket fd,
    const CliOptions& options,
    const std::string& executablePath,
    const AppServeOptions& serveOptions,
    const json& schema,
    const HttpRequest& request
) {
    if (request.method == "GET") {
        if (!HeaderAccepts(request, "text/event-stream")) {
            return SendJsonResponse(fd, 400, JsonRpcError(nullptr, -32600, "GET /mcp requires Accept: text/event-stream"));
        }
        return SendJsonResponse(
            fd,
            405,
            JsonRpcError(nullptr, -32000, "SSE streams are not supported by this MCP server"),
            {{"Allow", "POST"}});
    }
    if (request.method == "DELETE") {
        return SendJsonResponse(
            fd,
            405,
            JsonRpcError(nullptr, -32000, "MCP sessions are stateless and cannot be deleted"),
            {{"Allow", "POST"}});
    }
    if (request.method != "POST") {
        return SendJsonResponse(fd, 405, JsonRpcError(nullptr, -32600, "MCP endpoint supports POST"), {{"Allow", "POST"}});
    }

    json message = json::parse(request.body, nullptr, false);
    if (message.is_discarded()) {
        return SendJsonRpcError(fd, 400, nullptr, -32700, "Parse error");
    }
    if (!message.is_object()) {
        return SendJsonRpcError(fd, 400, nullptr, -32600, "MCP POST body must be a single JSON-RPC message object");
    }
    const json id = JsonRpcIdOrNull(message);
    if (!McpProtocolHeaderSupported(request)) {
        return SendJsonRpcError(fd, 400, id, -32600, "unsupported MCP-Protocol-Version");
    }
    if (!HeaderAccepts(request, "application/json") || !HeaderAccepts(request, "text/event-stream")) {
        return SendJsonRpcError(fd, 400, id, -32600, "POST /mcp requires Accept listing application/json and text/event-stream");
    }

    if (!message.contains("method")) {
        if (message.contains("id") && ValidJsonRpcId(message["id"]) &&
            (message.contains("result") || message.contains("error"))) {
            return SendEmptyResponse(fd, 202);
        }
        return SendJsonRpcError(fd, 400, id, -32600, "invalid JSON-RPC message");
    }
    if (!message["method"].is_string() || message["method"].get<std::string>().empty()) {
        return SendJsonRpcError(fd, 400, id, -32600, "JSON-RPC method must be a non-empty string");
    }
    if (!message.contains("id")) {
        return SendEmptyResponse(fd, 202);
    }
    if (!ValidJsonRpcId(message["id"])) {
        return SendJsonRpcError(fd, 400, nullptr, -32600, "JSON-RPC request id must be a string or integer");
    }
    return HandleMcpJsonRpcRequest(fd, options, executablePath, serveOptions, schema, message);
}

bool HandleHttpCommand(
    AppSocket fd,
    const CliOptions& options,
    const std::string& executablePath,
    const AppServeOptions& serveOptions,
    const json& schema,
    const HttpRequest& request
) {
    const std::string prefix = "/commands/";
    const std::string commandName = UrlDecode(request.path.substr(prefix.size()));
    const json commands = schema.value("commands", json::object());
    if (commandName.empty() || !commands.contains(commandName)) {
        AppendRuntimeLog("server", "command_rejected", {{"command", commandName}, {"code", "unknown_command"}});
        return SendJsonResponse(fd, 404, HttpErrorBody("unknown_command", "unknown command: " + commandName));
    }
    json input;
    std::string error;
    if (!ParseJsonObjectBody(request, input, error)) {
        AppendRuntimeLog("server", "command_rejected", {{"command", commandName}, {"code", "invalid_input"}, {"error", error}});
        return SendJsonResponse(fd, 400, HttpErrorBody("invalid_input", error));
    }

    if (QueryFlagTrue(request.query, "async")) {
        AppendRuntimeLog("server", "command_async_start", {{"command", commandName}});
        auto operation = CreateAsyncOperation(
            options,
            executablePath,
            serveOptions.appPath,
            schema,
            commandName,
            input,
            "http",
            error);
        if (!operation) {
            AppendRuntimeLog("server", "command_async_failed", {{"command", commandName}, {"error", error}});
            return SendJsonResponse(fd, 500, HttpErrorBody("internal_error", error));
        }
        AppendRuntimeLog("server", "command_async_created", {
            {"command", commandName},
            {"operation", operation->value("operation", "")}
        });
        auto headers = RecordingHeaders(operation->value("recording", json::object()));
        headers["Location"] = (*operation)["result_url"].get<std::string>();
        headers["Retry-After"] = "2";
        return SendJsonResponse(
            fd,
            202,
            *operation,
            headers);
    }

    AppendRuntimeLog("server", "command_start", {{"command", commandName}});
    json recording;
    auto payload = RunAppCommand(
        options,
        executablePath,
        serveOptions.appPath,
        commandName,
        input,
        std::nullopt,
        AppIdFor(serveOptions.appPath, schema),
        "http",
        std::nullopt,
        std::nullopt,
        &recording,
        error);
    if (!payload) {
        AppendRuntimeLog("server", "command_failed", {{"command", commandName}, {"code", "internal_error"}, {"error", error}});
        return SendJsonResponse(fd, 500, HttpErrorBody("internal_error", error), RecordingHeaders(recording));
    }
    if (!payload->value("ok", false)) {
        const std::string code = payload->value("code", "operation_failed");
        const json data = payload->value("data", json::object());
        const json details = data.is_object() ? data.value("error", json::object()) : json::object();
        AppendRuntimeLog("server", "command_failed", {{"command", commandName}, {"code", code}, {"error", payload->value("error", "operation failed")}});
        return SendJsonResponse(
            fd,
            HttpStatusForErrorCode(code),
            HttpErrorBody(code, payload->value("error", "operation failed"), details),
            RecordingHeaders(recording));
    }
    AppendRuntimeLog("server", "command_done", {{"command", commandName}});
    return SendJsonResponse(fd, 200, (*payload)["data"]["result"], RecordingHeaders(recording));
}

bool HandleHttpOperationResult(
    AppSocket fd,
    const std::string& operationId,
    const OperationPaths& paths,
    const HttpRequest& request
) {
    std::string error;
    const int64_t waitSeconds = QueryWaitSeconds(request.query);
    if (waitSeconds < 0) {
        return SendJsonResponse(fd, 400, HttpErrorBody("invalid_input", "wait must be a non-negative integer"));
    }
    auto record = WaitForOperationResult(paths, operationId, waitSeconds, error);
    if (!record) {
        return SendJsonResponse(fd, 404, HttpErrorBody("unknown_operation", error));
    }
    const auto recordingHeaders = RecordingHeaders(record->value("recording", json::object()));
    auto view = OperationResultView(paths, operationId, *record, error);
    if (!view) {
        return SendJsonResponse(fd, 500, HttpErrorBody("internal_error", error), recordingHeaders);
    }
    view->erase("recording");
    const std::string status = view->value("status", "");
    if (status == "succeeded") {
        return SendJsonResponse(fd, 200, *view, recordingHeaders);
    }
    if (status == "pending" || status == "running" || status == "waiting_for_approval") {
        auto headers = recordingHeaders;
        headers["Location"] = "/operations/" + operationId + "/result";
        headers["Retry-After"] = "2";
        return SendJsonResponse(fd, 202, *view, headers);
    }

    const json errorData = view->value("error", json::object());
    return SendJsonResponse(
        fd,
        HttpStatusForErrorCode(errorData.value("code", status == "cancelled" ? "operation_cancelled" : "operation_failed")),
        HttpErrorBody(
            errorData.value("code", status == "cancelled" ? "operation_cancelled" : "operation_failed"),
            errorData.value("message", status == "cancelled" ? "operation cancelled" : "operation failed"),
            {{"operation", operationId}, {"status", status}}),
        recordingHeaders);
}

bool HandleHttpOperation(
    AppSocket fd,
    const std::string& appId,
    const HttpRequest& request
) {
    const std::string prefix = "/operations/";
    std::string rest = request.path.substr(prefix.size());
    if (rest.empty()) {
        return SendJsonResponse(fd, 404, HttpErrorBody("unknown_operation", "unknown operation"));
    }

    const auto handleApproval = [&](const std::string& suffix, bool approved) -> std::optional<bool> {
        if (request.method != "POST" || rest.size() <= suffix.size() ||
            rest.substr(rest.size() - suffix.size()) != suffix) {
            return std::nullopt;
        }
        const std::string operationId = rest.substr(0, rest.size() - suffix.size());
        json input;
        std::string error;
        if (!ParseJsonObjectBody(request, input, error)) {
            return SendJsonResponse(fd, 400, HttpErrorBody("invalid_input", error));
        }
        if (!input.contains("approval_id") || !input["approval_id"].is_string() ||
            (input.contains("note") && !input["note"].is_string())) {
            return SendJsonResponse(
                fd,
                400,
                HttpErrorBody(
                    "invalid_input",
                    "approval_id must be a string and note, when provided, must be a string"));
        }
        const std::string approvalId = input["approval_id"].get<std::string>();
        const std::string note =
            input.contains("note") ? input["note"].get<std::string>() : "";
        if (approvalId.empty()) {
            return SendJsonResponse(
                fd,
                400,
                HttpErrorBody("invalid_input", "approval_id is required"));
        }
        OperationPaths paths = PathsForOperation(appId, operationId);
        auto response = RespondApprovalRecord(
            paths,
            operationId,
            approvalId,
            approved,
            note,
            error);
        if (!response) {
            return SendJsonResponse(
                fd,
                fs::exists(paths.operationJson) ? 409 : 404,
                HttpErrorBody(
                    fs::exists(paths.operationJson) ? "approval_conflict" : "unknown_operation",
                    error));
        }
        return SendJsonResponse(fd, 200, *response);
    };
    if (auto response = handleApproval(":approve", true)) return *response;
    if (auto response = handleApproval(":deny", false)) return *response;

    if (request.method == "POST" && rest.size() > 7 && rest.substr(rest.size() - 7) == ":cancel") {
        const std::string operationId = rest.substr(0, rest.size() - 7);
        OperationPaths paths = PathsForOperation(appId, operationId);
        std::string error;
        auto cancelled = CancelOperationRecord(paths, operationId, error);
        if (!cancelled) {
            return SendJsonResponse(fd, fs::exists(paths.operationJson) ? 500 : 404, HttpErrorBody(fs::exists(paths.operationJson) ? "internal_error" : "unknown_operation", error));
        }
        const json updatedRecord = ReadOperationRecord(paths, error).value_or(json::object());
        return SendJsonResponse(
            fd,
            200,
            *cancelled,
            RecordingHeaders(updatedRecord.value("recording", json::object())));
    }

    const std::string resultSuffix = "/result";
    if (request.method == "GET" && rest.size() > resultSuffix.size() && rest.substr(rest.size() - resultSuffix.size()) == resultSuffix) {
        const std::string operationId = rest.substr(0, rest.size() - resultSuffix.size());
        OperationPaths paths = PathsForOperation(appId, operationId);
        return HandleHttpOperationResult(fd, operationId, paths, request);
    }

    if (request.method == "GET") {
        const std::string operationId = rest;
        OperationPaths paths = PathsForOperation(appId, operationId);
        std::string error;
        auto record = ReadKnownOperation(paths, operationId, error);
        if (!record) {
            return SendJsonResponse(fd, 404, HttpErrorBody("unknown_operation", error));
        }
        return SendJsonResponse(
            fd,
            200,
            PublicOperationRecord(*record),
            RecordingHeaders(record->value("recording", json::object())));
    }

    return SendJsonResponse(fd, 404, HttpErrorBody("unknown_operation", "unknown operation route"));
}

bool HandleHttpRequest(
    AppSocket fd,
    const CliOptions& options,
    const std::string& executablePath,
    const AppServeOptions& serveOptions,
    const json& schema,
    const std::string& appId,
    const HttpRequest& request
) {
    if (!OriginAllowed(request, serveOptions)) {
        AppendRuntimeLog("server", "request_rejected", {{"method", request.method}, {"path", request.path}, {"code", "origin_not_allowed"}});
        if (request.path == kMcpEndpointPath) {
            return SendJsonResponse(fd, 403, JsonRpcError(nullptr, -32000, "origin is not allowed"));
        }
        return SendJsonResponse(fd, 403, HttpErrorBody("permission_denied", "origin is not allowed"));
    }
    if (!Authorized(request, serveOptions)) {
        AppendRuntimeLog("server", "request_rejected", {{"method", request.method}, {"path", request.path}, {"code", "permission_denied"}});
        if (request.path == kMcpEndpointPath) {
            return SendJsonResponse(
                fd,
                401,
                JsonRpcError(nullptr, -32000, "missing or invalid bearer token"),
                {{"WWW-Authenticate", "Bearer"}});
        }
        return SendJsonResponse(fd, 401, HttpErrorBody("permission_denied", "missing or invalid bearer token"));
    }
    if (request.method == "POST" && request.path == "/shutdown") {
        AppendRuntimeLog("server", "shutdown_requested");
        bool sent = SendJsonResponse(fd, 200, {{"ok", true}});
        gAppServeStopRequested = true;
        return sent;
    }
    if (request.method == "GET" && request.path == "/health") {
        return SendJsonResponse(fd, 200, {{"ok", true}});
    }
    if (request.method == "GET" && request.path == "/schema") {
        return SendJsonResponse(fd, 200, schema);
    }
    if (request.path == kMcpEndpointPath) {
        return HandleMcpRequest(fd, options, executablePath, serveOptions, schema, request);
    }
    if (request.method == "POST" && StartsWith(request.path, "/commands/")) {
        return HandleHttpCommand(fd, options, executablePath, serveOptions, schema, request);
    }
    if (StartsWith(request.path, "/operations/")) {
        return HandleHttpOperation(fd, appId, request);
    }
    AppendRuntimeLog("server", "request_rejected", {{"method", request.method}, {"path", request.path}, {"code", "not_found"}});
    return SendJsonResponse(fd, 404, HttpErrorBody("not_found", "unknown endpoint"));
}

bool ParseListen(const std::string& value, std::string& host, int& port) {
    const size_t colon = value.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= value.size()) {
        return false;
    }
    host = value.substr(0, colon);
    int64_t parsedPort = 0;
    if (!ParseInteger(value.substr(colon + 1), parsedPort) || parsedPort <= 0 || parsedPort > 65535) {
        return false;
    }
    port = static_cast<int>(parsedPort);
    return true;
}

bool IsLocalBindHost(const std::string& host) {
    return host == "127.0.0.1" || host == "localhost";
}

std::optional<AppServeOptions> ParseServeOptions(const std::vector<std::string>& args, std::string& error) {
    if (args.size() < 3) {
        error = "app serve requires <app.lua>";
        return std::nullopt;
    }
    AppServeOptions serve;
    serve.appPath = args[2];
    for (size_t i = 3; i < args.size(); ++i) {
        if (args[i] == "--listen") {
            if (i + 1 >= args.size() || !ParseListen(args[++i], serve.host, serve.port)) {
                error = "app serve --listen requires host:port";
                return std::nullopt;
            }
        } else if (args[i] == "--auth-token-env") {
            if (i + 1 >= args.size() || IsBlank(args[i + 1])) {
                error = "app serve --auth-token-env requires an environment variable name";
                return std::nullopt;
            }
            const char* token = std::getenv(args[++i].c_str());
            if (token != nullptr) {
                serve.authToken = token;
            }
        } else if (args[i] == "--allowed-origin") {
            if (i + 1 >= args.size() || IsBlank(args[i + 1])) {
                error = "app serve --allowed-origin requires an origin";
                return std::nullopt;
            }
            auto origin = NormalizeOrigin(args[++i]);
            if (!origin) {
                error = "app serve --allowed-origin requires an http(s) origin such as https://mcp.example.com";
                return std::nullopt;
            }
            serve.allowedOrigins.insert(*origin);
        } else if (args[i] == "--tray-state-file") {
            if (i + 1 >= args.size() || IsBlank(args[i + 1])) {
                error = "app serve --tray-state-file requires a path";
                return std::nullopt;
            }
            serve.trayStateFile = args[++i];
        } else if (args[i] == "--tray-config-name") {
            if (i + 1 >= args.size() || IsBlank(args[i + 1])) {
                error = "app serve --tray-config-name requires a value";
                return std::nullopt;
            }
            serve.trayConfigName = args[++i];
        } else if (args[i] == "--tray-display-name") {
            if (i + 1 >= args.size() || IsBlank(args[i + 1])) {
                error = "app serve --tray-display-name requires a value";
                return std::nullopt;
            }
            serve.trayDisplayName = args[++i];
        } else if (args[i] == "--trace-dir" || args[i] == "--operation-store" ||
                   args[i] == "--default-timeout" || args[i] == "--max-operation-time") {
            if (i + 1 >= args.size() || IsBlank(args[i + 1])) {
                error = "app serve " + args[i] + " requires a value";
                return std::nullopt;
            }
            ++i;
        } else {
            error = "unknown app serve option: " + args[i];
            return std::nullopt;
        }
    }
    if (!IsLocalBindHost(serve.host) && serve.authToken.empty()) {
        error = "app serve requires --auth-token-env when binding outside 127.0.0.1";
        return std::nullopt;
    }
    AddDefaultAllowedOrigins(serve);
    return serve;
}

int RunHttpServer(
    const CliOptions& options,
    const std::string& executablePath,
    const AppServeOptions& serveOptions,
    const json& schema,
    const std::string& appId
) {
#if defined(__unix__) || defined(__APPLE__) || defined(_WIN32)
    gAppServeStopRequested = false;
#if defined(_WIN32)
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return ErrorExit(options, "failed to initialize Winsock", 1, "internal_error");
    }
#endif
    AppSocket serverFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd == kInvalidSocket) {
#if defined(_WIN32)
        WSACleanup();
#endif
        return ErrorExit(options, "failed to create HTTP socket", 1, "internal_error");
    }
    int reuse = 1;
    ::setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(serveOptions.port));
    const std::string bindHost = serveOptions.host == "localhost" ? "127.0.0.1" : serveOptions.host;
    if (::inet_pton(AF_INET, bindHost.c_str(), &addr.sin_addr) != 1) {
        CloseAppSocket(serverFd);
#if defined(_WIN32)
        WSACleanup();
#endif
        return ErrorExit(options, "app serve --listen host must be an IPv4 address or localhost", 2, "invalid_input");
    }
    if (::bind(serverFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
#if defined(_WIN32)
        std::string detail = std::to_string(WSAGetLastError());
#else
        std::string detail = std::strerror(errno);
#endif
        std::string message = "failed to bind " + bindHost + ":" + std::to_string(serveOptions.port) + ": " + detail;
        CloseAppSocket(serverFd);
#if defined(_WIN32)
        WSACleanup();
#endif
        return ErrorExit(options, message, 1, "internal_error");
    }
    if (::listen(serverFd, 16) != 0) {
#if defined(_WIN32)
        std::string detail = std::to_string(WSAGetLastError());
#else
        std::string detail = std::strerror(errno);
#endif
        std::string message = "failed to listen: " + detail;
        CloseAppSocket(serverFd);
#if defined(_WIN32)
        WSACleanup();
#endif
        return ErrorExit(options, message, 1, "internal_error");
    }
#if defined(__unix__) || defined(__APPLE__)
    ScopedAppServeSignals signals(serverFd);
#endif
    std::cerr << "computer.cpp app server listening on http://" << bindHost << ":" << serveOptions.port
              << " (MCP endpoint: /mcp)\n";
    AppendRuntimeLog("server", "listening", {
        {"url", "http://" + bindHost + ":" + std::to_string(serveOptions.port)},
        {"app", fs::absolute(serveOptions.appPath).string()}
    });

    if (serveOptions.trayStateFile.has_value()) {
        TrayAppServerState state;
#if defined(_WIN32)
        state.pid = static_cast<long>(GetCurrentProcessId());
#else
        state.pid = static_cast<long>(getpid());
#endif
        state.host = bindHost;
        state.port = serveOptions.port;
        state.url = "http://" + bindHost + ":" + std::to_string(serveOptions.port);
        state.appPath = fs::absolute(serveOptions.appPath).string();
        state.appId = appId;
        state.configName = serveOptions.trayConfigName;
        state.displayName = serveOptions.trayDisplayName;
        state.startedAt = NowIsoUtc();
        std::string stateError;
        if (!SaveTrayAppServerState(state, *serveOptions.trayStateFile, &stateError)) {
            std::cerr << "warning: " << stateError << "\n";
            AppendRuntimeLog("server", "state_save_failed", {{"error", stateError}});
        }
    }

    while (!gAppServeStopRequested) {
        AppSocket clientFd = ::accept(serverFd, nullptr, nullptr);
        if (clientFd == kInvalidSocket) {
#if defined(__unix__) || defined(__APPLE__)
            if (gAppServeStopRequested) break;
            if (errno == EINTR) continue;
#endif
            break;
        }
        HttpRequest request;
        std::string error;
        if (!ReadHttpRequest(clientFd, request, error)) {
            AppendRuntimeLog("server", "request_rejected", {{"code", "invalid_input"}, {"error", error}});
            SendJsonResponse(clientFd, 400, HttpErrorBody("invalid_input", error));
        } else {
            HandleHttpRequest(clientFd, options, executablePath, serveOptions, schema, appId, request);
        }
        CloseAppSocket(clientFd);
    }
#if defined(__unix__) || defined(__APPLE__)
    if (gAppServeServerFd >= 0) {
        CloseAppSocket(serverFd);
        gAppServeServerFd = -1;
    }
#else
    CloseAppSocket(serverFd);
#endif
    if (serveOptions.trayStateFile.has_value()) {
        std::string stateError;
#if defined(_WIN32)
        RemoveTrayAppServerStateForPid(*serveOptions.trayStateFile, static_cast<long>(GetCurrentProcessId()), &stateError);
#else
        RemoveTrayAppServerStateForPid(*serveOptions.trayStateFile, static_cast<long>(getpid()), &stateError);
#endif
    }
#if defined(_WIN32)
    WSACleanup();
#endif
    AppendRuntimeLog("server", "stopped", {
        {"url", "http://" + bindHost + ":" + std::to_string(serveOptions.port)}
    });
    return 0;
#else
    (void)executablePath;
    (void)serveOptions;
    (void)schema;
    (void)appId;
    return ErrorExit(options, "HTTP server is not implemented on this platform", 1, "internal_error");
#endif
}

int HandleAppServe(
    const CliOptions& options,
    const std::vector<std::string>& args,
    const std::string& executablePath
) {
    std::string error;
    auto serveOptions = ParseServeOptions(args, error);
    if (!serveOptions) {
        return ErrorExit(options, error, 2, "invalid_input");
    }
    auto schema = LoadAppSchema(options, executablePath, serveOptions->appPath, error);
    if (!schema) {
        return ErrorExit(options, error, 1, "invalid_app");
    }
    const std::string appId = AppIdFor(serveOptions->appPath, *schema);
    return RunHttpServer(options, executablePath, *serveOptions, *schema, appId);
}

int HandleAppRun(
    const CliOptions& options,
    const std::vector<std::string>& args,
    const std::string& executablePath
) {
    if (args.size() < 3) {
        return ErrorExit(options, "app run requires <app.lua>", 2, "invalid_input");
    }
    fs::path appPath = args[2];
    std::string error;
    auto schema = LoadAppSchema(options, executablePath, appPath, error);
    if (!schema) {
        return ErrorExit(options, error, 1, "invalid_app");
    }

    if (args.size() == 3 || args[3] == "--help" || args[3] == "-h") {
        PrintAppHelp(appPath, *schema);
        return 0;
    }

    const std::string commandName = args[3];
    const json commands = schema->value("commands", json::object());
    if (!commands.contains(commandName)) {
        return ErrorExit(options, "unknown command: " + commandName, 2, "unknown_command");
    }
    const json command = commands[commandName];
    if (args.size() > 4 && (args[4] == "--help" || args[4] == "-h")) {
        PrintCommandHelp(appPath, commandName, command);
        return 0;
    }

    auto parsedArgs = ParseAppRunArgs(args, 4, command.value("input", json::object()), error);
    if (!parsedArgs) {
        return ErrorExit(options, error, 2, "invalid_input");
    }
    if (parsedArgs->async) {
        auto operation = CreateAsyncOperation(
            options,
            executablePath,
            appPath,
            *schema,
            commandName,
            parsedArgs->input,
            "cli",
            error);
        if (!operation) {
            return ErrorExit(options, error, 1, "internal_error");
        }
        if (options.jsonOutput) {
            std::cout << json({{"ok", true}, {"data", *operation}}).dump(2) << "\n";
        } else {
            std::cout << operation->dump(2) << "\n";
        }
        return 0;
    }

    json recording;
    auto payload = RunAppCommand(
        options,
        executablePath,
        appPath,
        commandName,
        parsedArgs->input,
        std::nullopt,
        AppIdFor(appPath, *schema),
        "cli",
        std::nullopt,
        std::nullopt,
        &recording,
        error);
    if (!payload) {
        if (options.jsonOutput) {
            json output = ErrorPayload("operation_failed", error);
            if (!recording.empty()) {
                output["data"]["recording"] = recording;
            }
            std::cout << output.dump(2) << "\n";
        } else {
            std::cerr << "Error: " << error << "\n";
            PrintRecordingStatusToStderr(recording);
        }
        return 1;
    }
    *payload = ShapeRunPayloadForCli(std::move(*payload), *parsedArgs, commandName);
    if (!payload->value("ok", false)) {
        const std::string code = payload->value("code", "operation_failed");
        const std::string message = payload->value("error", "operation failed");
        if (options.jsonOutput) {
            std::cout << payload->dump(2) << "\n";
        } else {
            std::cerr << "Error: " << message << "\n";
            PrintRecordingStatusToStderr(recording);
        }
        if (code == "invalid_input" || code == "unknown_command") return 2;
        if (code == "operation_cancelled") return 1;
        return 1;
    }

    if (options.jsonOutput) {
        std::cout << payload->dump(2) << "\n";
    } else {
        std::cout << (*payload)["data"]["result"].dump(2) << "\n";
        PrintRecordingStatusToStderr(recording);
    }
    return 0;
}

} // namespace

bool IsSemanticAppCommand(const std::vector<std::string>& args) {
    if (args.size() < 2 || args[0] != "app") {
        return false;
    }
    return args[1] == "run" || args[1] == "operation" || args[1] == "serve";
}

int HandleSemanticAppCommand(
    const CliOptions& options,
    const std::vector<std::string>& args,
    const std::string& executablePath
) {
    if (args.size() < 2) {
        return ErrorExit(options, "app requires run, operation, or serve", 2, "invalid_input");
    }
    if (args[1] == "run") {
        return HandleAppRun(options, args, executablePath);
    }
    if (args[1] == "operation") {
        return HandleAppOperation(options, args, executablePath);
    }
    if (args[1] == "serve") {
        return HandleAppServe(options, args, executablePath);
    }
    return ErrorExit(options, "unknown app subcommand: " + args[1], 2, "invalid_input");
}

} // namespace ComputerCpp::Cli
