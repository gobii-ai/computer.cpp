#include "CliApp.h"

#include "computer_cpp/AppPaths.h"
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
#endif

struct AppRunArgs {
    bool async = false;
    bool trace = false;
    std::optional<fs::path> traceDir;
    json input = json::object();
};

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
    std::string& error
) {
    LuaRunOptions lua = BaseLuaOptions(options, executablePath, appPath, "run");
    lua.vars["__ac_app_command"] = commandName;
    lua.vars["__ac_app_input_json"] = input.dump();
    if (operationDir.has_value()) {
        lua.vars["__ac_operation_dir"] = operationDir->string();
    }
    LuaRunResult result = RunLuaScriptCapture(lua, true);
    auto parsed = ParseJsonOutput(result, error);
    if (!parsed) {
        return std::nullopt;
    }
    return *parsed;
}

json MakeInitialOperationRecord(
    const fs::path& appPath,
    const json& schema,
    const std::string& appId,
    const std::string& operationId,
    const std::string& commandName
) {
    const std::string now = NowIsoUtc();
    return {
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
        {"cancel_requested", false},
    };
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
    return record;
}

bool WriteOperationRecord(const OperationPaths& paths, const json& record, std::string& error) {
    return WriteJsonFile(paths.operationJson, record, error);
}

void MarkOperationFailed(const OperationPaths& paths, json record, std::string code, std::string message) {
    const std::string now = NowIsoUtc();
    record["status"] = "failed";
    record["updated_at"] = now;
    record["finished_at"] = now;
    record["error"] = {{"code", std::move(code)}, {"message", std::move(message)}};
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

    auto payload = RunAppCommand(options, executablePath, appPath, commandName, *inputOpt, paths.dir, error);
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
    WriteTraceJsonl(paths.traceJsonl, data.value("trace", json::array()), error);
    latest["status"] = code == "operation_cancelled" ? "cancelled" : "failed";
    latest["error"] = {{"code", code}, {"message", message}};
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
    std::string& error
) {
    const std::string appId = AppIdFor(appPath, schema);
    const std::string operationId = NewOperationId();
    OperationPaths paths = PathsForOperation(appId, operationId);
    EnsureDirectory(paths.dir);
    EnsureDirectory(paths.artifactsDir);

    json record = MakeInitialOperationRecord(appPath, schema, appId, operationId, commandName);
    if (!WriteJsonFile(paths.inputJson, input, error)) {
        return std::nullopt;
    }
    if (!WriteOperationRecord(paths, record, error)) {
        return std::nullopt;
    }
    if (!StartOperationProcess(options, executablePath, appPath, appId, operationId, error)) {
        record["status"] = "failed";
        record["error"] = {{"code", "internal_error"}, {"message", error}};
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

    const std::string status = record->value("status", "");
    if (status == "succeeded") {
        auto result = ReadJsonFile(paths->resultJson, error);
        if (!result) {
            return ErrorExit(options, error, 1, "internal_error");
        }
        return PrintData(options, {{"status", status}, {"result", *result}});
    }
    if (status == "pending" || status == "running") {
        return PrintData(options, {{"status", status}, {"operation", operationId}, {"result", nullptr}});
    }

    json errorData = record->value("error", json::object());
    json data = {{"status", status}, {"operation", operationId}, {"result", nullptr}, {"error", errorData}};
    if (options.jsonOutput) {
        json payload = ErrorPayload(
            errorData.value("code", status == "cancelled" ? "operation_cancelled" : "operation_failed"),
            errorData.value("message", status == "cancelled" ? "operation cancelled" : "operation failed")
        );
        payload["data"] = data;
        std::cout << payload.dump(2) << "\n";
    } else {
        std::cout << data.dump(2) << "\n";
    }
    return 1;
}

std::optional<json> CancelOperationRecord(const OperationPaths& paths, const std::string& operationId, std::string& error) {
    auto record = ReadKnownOperation(paths, operationId, error);
    if (!record) {
        return std::nullopt;
    }

    const std::string status = record->value("status", "");
    if (status == "pending" || status == "running") {
        const std::string now = NowIsoUtc();
        (*record)["status"] = "cancelled";
        (*record)["cancel_requested"] = true;
        (*record)["updated_at"] = now;
        (*record)["finished_at"] = now;
        (*record)["error"] = {{"code", "operation_cancelled"}, {"message", "operation cancelled"}};
        if (!WriteJsonFile(paths.errorJson, (*record)["error"], error) || !WriteOperationRecord(paths, *record, error)) {
            return std::nullopt;
        }
    }
    return json({{"operation", operationId}, {"status", (*record)["status"]}});
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
        return ErrorExit(options, "app operation requires get, result, or cancel", 2, "invalid_input");
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
    if (code == "operation_cancelled") return 409;
    if (code == "permission_denied") return 403;
    if (code == "timeout") return 504;
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

json MakeMcpToolsList(const json& schema) {
    json tools = json::array();
    const json commands = schema.value("commands", json::object());
    for (auto it = commands.begin(); it != commands.end(); ++it) {
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
    return {{"tools", std::move(tools)}};
}

json McpTextContent(std::string text) {
    return {
        {"type", "text"},
        {"text", std::move(text)}
    };
}

json McpToolSuccessResult(const json& result) {
    json out = {
        {"content", json::array({McpTextContent(result.is_string() ? result.get<std::string>() : result.dump(2))})},
        {"isError", false}
    };
    if (result.is_object()) {
        out["structuredContent"] = result;
    }
    return out;
}

json McpToolErrorResult(const std::string& code, const std::string& message) {
    return {
        {"content", json::array({McpTextContent(code + ": " + message)})},
        {"isError", true}
    };
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
        if (!commands.contains(toolName)) {
            return SendJsonRpcError(fd, 400, id, -32602, "Unknown tool: " + toolName);
        }

        std::string error;
        auto payload = RunAppCommand(options, executablePath, serveOptions.appPath, toolName, arguments, std::nullopt, error);
        if (!payload) {
            return SendJsonRpcError(fd, 500, id, -32603, error.empty() ? "tool execution failed" : error);
        }
        if (!payload->value("ok", false)) {
            const std::string code = payload->value("code", "operation_failed");
            const std::string messageText = payload->value("error", "operation failed");
            return SendJsonResponse(fd, 200, JsonRpcResult(id, McpToolErrorResult(code, messageText)));
        }

        const json data = payload->value("data", json::object());
        const json result = data.value("result", json::object());
        return SendJsonResponse(fd, 200, JsonRpcResult(id, McpToolSuccessResult(result)));
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
        return SendJsonResponse(fd, 404, HttpErrorBody("unknown_command", "unknown command: " + commandName));
    }
    json input;
    std::string error;
    if (!ParseJsonObjectBody(request, input, error)) {
        return SendJsonResponse(fd, 400, HttpErrorBody("invalid_input", error));
    }

    if (QueryFlagTrue(request.query, "async")) {
        auto operation = CreateAsyncOperation(options, executablePath, serveOptions.appPath, schema, commandName, input, error);
        if (!operation) {
            return SendJsonResponse(fd, 500, HttpErrorBody("internal_error", error));
        }
        return SendJsonResponse(
            fd,
            202,
            *operation,
            {{"Location", (*operation)["result_url"].get<std::string>()}, {"Retry-After", "2"}});
    }

    auto payload = RunAppCommand(options, executablePath, serveOptions.appPath, commandName, input, std::nullopt, error);
    if (!payload) {
        return SendJsonResponse(fd, 500, HttpErrorBody("internal_error", error));
    }
    if (!payload->value("ok", false)) {
        const std::string code = payload->value("code", "operation_failed");
        return SendJsonResponse(fd, HttpStatusForErrorCode(code), HttpErrorBody(code, payload->value("error", "operation failed")));
    }
    return SendJsonResponse(fd, 200, (*payload)["data"]["result"]);
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
    const std::string status = record->value("status", "");
    if (status == "succeeded") {
        auto result = ReadJsonFile(paths.resultJson, error);
        if (!result) {
            return SendJsonResponse(fd, 500, HttpErrorBody("internal_error", error));
        }
        return SendJsonResponse(fd, 200, {{"status", status}, {"result", *result}});
    }
    if (status == "pending" || status == "running") {
        return SendJsonResponse(
            fd,
            202,
            {{"status", status}, {"operation", operationId}, {"result", nullptr}},
            {{"Location", "/operations/" + operationId + "/result"}, {"Retry-After", "2"}});
    }

    json errorData = record->value("error", json::object());
    return SendJsonResponse(
        fd,
        HttpStatusForErrorCode(errorData.value("code", status == "cancelled" ? "operation_cancelled" : "operation_failed")),
        HttpErrorBody(
            errorData.value("code", status == "cancelled" ? "operation_cancelled" : "operation_failed"),
            errorData.value("message", status == "cancelled" ? "operation cancelled" : "operation failed"),
            {{"operation", operationId}, {"status", status}}));
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

    if (request.method == "POST" && rest.size() > 7 && rest.substr(rest.size() - 7) == ":cancel") {
        const std::string operationId = rest.substr(0, rest.size() - 7);
        OperationPaths paths = PathsForOperation(appId, operationId);
        std::string error;
        auto cancelled = CancelOperationRecord(paths, operationId, error);
        if (!cancelled) {
            return SendJsonResponse(fd, fs::exists(paths.operationJson) ? 500 : 404, HttpErrorBody(fs::exists(paths.operationJson) ? "internal_error" : "unknown_operation", error));
        }
        return SendJsonResponse(fd, 200, *cancelled);
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
        return SendJsonResponse(fd, 200, PublicOperationRecord(*record));
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
        if (request.path == kMcpEndpointPath) {
            return SendJsonResponse(fd, 403, JsonRpcError(nullptr, -32000, "origin is not allowed"));
        }
        return SendJsonResponse(fd, 403, HttpErrorBody("permission_denied", "origin is not allowed"));
    }
    if (!Authorized(request, serveOptions)) {
        if (request.path == kMcpEndpointPath) {
            return SendJsonResponse(
                fd,
                401,
                JsonRpcError(nullptr, -32000, "missing or invalid bearer token"),
                {{"WWW-Authenticate", "Bearer"}});
        }
        return SendJsonResponse(fd, 401, HttpErrorBody("permission_denied", "missing or invalid bearer token"));
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
        state.displayName = serveOptions.trayDisplayName;
        state.startedAt = NowIsoUtc();
        std::string stateError;
        if (!SaveTrayAppServerState(state, *serveOptions.trayStateFile, &stateError)) {
            std::cerr << "warning: " << stateError << "\n";
        }
    }

    while (
#if defined(__unix__) || defined(__APPLE__)
        !gAppServeStopRequested
#else
        true
#endif
    ) {
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
        auto operation = CreateAsyncOperation(options, executablePath, appPath, *schema, commandName, parsedArgs->input, error);
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

    auto payload = RunAppCommand(options, executablePath, appPath, commandName, parsedArgs->input, std::nullopt, error);
    if (!payload) {
        return ErrorExit(options, error, 1, "operation_failed");
    }
    *payload = ShapeRunPayloadForCli(std::move(*payload), *parsedArgs, commandName);
    if (!payload->value("ok", false)) {
        const std::string code = payload->value("code", "operation_failed");
        const std::string message = payload->value("error", "operation failed");
        if (options.jsonOutput) {
            std::cout << payload->dump(2) << "\n";
        } else {
            std::cerr << "Error: " << message << "\n";
        }
        if (code == "invalid_input" || code == "unknown_command") return 2;
        if (code == "operation_cancelled") return 1;
        return 1;
    }

    if (options.jsonOutput) {
        std::cout << payload->dump(2) << "\n";
    } else {
        std::cout << (*payload)["data"]["result"].dump(2) << "\n";
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
