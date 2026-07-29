#include "computer_cpp/LuaRunner.h"
#include "computer_cpp/ControlSession.h"
#include "computer_cpp/WindowsUtil.h"

#include "LuaPrelude.h"
#include "PosixArgv.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace ComputerCpp {

namespace {

class ManagedControlSession {
public:
    ManagedControlSession() = default;
    ManagedControlSession(const ManagedControlSession&) = delete;
    ManagedControlSession& operator=(const ManagedControlSession&) = delete;

    ~ManagedControlSession() {
        StopAndRelease();
    }

    ControlSessionResult Acquire(const LuaRunOptions& options) {
        ControlSessionAcquireOptions acquire;
        acquire.scope = options.controlScope;
        acquire.daemonSession = options.session;
        acquire.owner = options.leaseOwner;
        acquire.purpose = options.leasePurpose;
        acquire.ttlMs = options.leaseTtlMs;
        acquire.waitMs = options.leaseWaitMs;
        acquire.maxRuntimeMs = options.leaseMaxRuntimeMs;

        ControlSessionResult result;
        try {
            result = AcquireControlSession(acquire);
        } catch (const std::exception& ex) {
            result.code = "control_session_error";
            result.error = ex.what();
            return result;
        }
        if (!result.ok) {
            return result;
        }

        token_ = result.record.token;
        ttlMs_ = result.record.expiresAtMs - result.record.renewedAtMs;
        if (ttlMs_ <= 0) {
            ttlMs_ = ClampControlSessionTtlMs(options.leaseTtlMs);
        }
        renewIntervalMs_ =
            std::clamp(ttlMs_ / 3, static_cast<int64_t>(250), static_cast<int64_t>(30000));
        nextRenewal_ = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(renewIntervalMs_);
        return result;
    }

    const std::string& token() const {
        return token_;
    }

    bool RenewIfDue() {
        if (token_.empty() ||
            !renewalError_.empty() ||
            std::chrono::steady_clock::now() < nextRenewal_) {
            return renewalError_.empty();
        }
        ControlSessionResult renewed;
        try {
            renewed = RenewControlSession(token_, ttlMs_);
        } catch (const std::exception& ex) {
            renewalError_ = ex.what();
            return false;
        }
        if (!renewed.ok) {
            renewalError_ = renewed.error.empty()
                ? "control session renewal failed"
                : renewed.error;
            return false;
        }
        nextRenewal_ = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(renewIntervalMs_);
        return true;
    }

    const std::string& RenewalError() const {
        return renewalError_;
    }

    void StopAndRelease() {
        if (!token_.empty()) {
            try {
                ReleaseControlSession(token_);
            } catch (const std::exception&) {
                // The lease will expire by TTL even if storage is unavailable
                // during best-effort cleanup.
            }
            token_.clear();
        }
    }

private:
    std::string token_;
    int64_t ttlMs_ = 0;
    int64_t renewIntervalMs_ = 0;
    std::chrono::steady_clock::time_point nextRenewal_;
    std::string renewalError_;
};

bool IsExecutable(const fs::path& path) {
#if defined(__unix__) || defined(__APPLE__)
    return ::access(path.c_str(), X_OK) == 0;
#else
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec;
#endif
}

#if defined(_WIN32)
std::vector<std::string> PathExtensions() {
    std::vector<std::string> extensions{""};
    if (const char* raw = std::getenv("PATHEXT")) {
        std::stringstream stream(raw);
        std::string ext;
        while (std::getline(stream, ext, ';')) {
            if (!ext.empty()) {
                extensions.push_back(ext);
            }
        }
    }
    extensions.push_back(".exe");
    return extensions;
}
#endif

std::vector<fs::path> WithPathExtensions(const fs::path& path) {
    std::vector<fs::path> candidates{path};
#if defined(_WIN32)
    for (const auto& ext : PathExtensions()) {
        if (!ext.empty()) {
            fs::path candidate = path;
            candidate += ext;
            candidates.push_back(candidate);
        }
    }
#endif
    return candidates;
}

fs::path FindOnPath(const std::string& name) {
    if (name.find('/') != std::string::npos
#if defined(_WIN32)
        || name.find('\\') != std::string::npos || name.find(':') != std::string::npos
#endif
    ) {
        fs::path path(name);
        for (const auto& candidate : WithPathExtensions(path)) {
            if (IsExecutable(candidate)) {
                return candidate;
            }
        }
        return {};
    }
    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv) {
        return {};
    }
    std::stringstream stream(pathEnv);
    std::string dir;
    const char delimiter =
#if defined(_WIN32)
        ';';
#else
        ':';
#endif
    while (std::getline(stream, dir, delimiter)) {
        if (dir.empty()) {
            dir = ".";
        }
        for (const auto& candidate : WithPathExtensions(fs::path(dir) / name)) {
            if (IsExecutable(candidate)) {
                return candidate;
            }
        }
    }
    return {};
}

std::vector<fs::path> BundledLuaCandidates(const fs::path& executablePath) {
    fs::path executable = executablePath;
    if (executable.empty()) {
#if defined(_WIN32)
        std::wstring buffer(32768, L'\0');
        DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length > 0 && length < buffer.size()) {
            buffer.resize(length);
            executable = buffer;
        }
#elif defined(__APPLE__)
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
            executable = fs::path(buffer.c_str());
        }
#elif defined(__linux__)
        std::vector<char> buffer(4096, '\0');
        ssize_t length = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
        if (length > 0) {
            executable = fs::path(std::string(buffer.data(), static_cast<size_t>(length)));
        }
#endif
    }
    if (executable.empty()) {
        return {};
    }
    if (executable.filename() == executable) {
        fs::path resolved = FindOnPath(executable.string());
        if (!resolved.empty()) {
            executable = resolved;
        }
    }
    if (executable.is_relative()) {
        std::error_code ec;
        executable = fs::absolute(executable, ec);
        if (ec) {
            executable = executablePath;
        }
    }
    fs::path executableDir = executable.parent_path();
#if defined(_WIN32)
    return {
        executableDir / "lua" / "bin" / "lua.exe",
        executableDir.parent_path() / "lua" / "bin" / "lua.exe",
        executableDir / "ComputerCpp" / "lua" / "bin" / "lua.exe",
    };
#else
    fs::path macosDir = executableDir;
    return {
        macosDir.parent_path() / "Resources" / "lua" / "bin" / "lua",
        executable.parent_path() / "ComputerCpp.app" / "Contents" / "Resources" / "lua" / "bin" / "lua",
    };
#endif
}

}

fs::path FindLuaInterpreter(const fs::path& executablePath) {
    if (const char* configured = std::getenv("COMPUTER_CPP_LUA")) {
        fs::path path = FindOnPath(configured);
        if (!path.empty()) {
            return path;
        }
    }
    for (const fs::path& candidate : BundledLuaCandidates(executablePath)) {
        if (IsExecutable(candidate)) {
            return candidate;
        }
    }
    for (const std::string& name : {"lua", "lua5.4", "lua5.3", "luajit"}) {
        fs::path path = FindOnPath(name);
        if (!path.empty()) {
            return path;
        }
    }
    return {};
}

namespace {

fs::path TempPreludePath() {
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
#if defined(__unix__) || defined(__APPLE__)
    auto pid = static_cast<long long>(::getpid());
#else
    auto pid =
#if defined(_WIN32)
        static_cast<long long>(GetCurrentProcessId());
#else
        0LL;
#endif
#endif
    return fs::temp_directory_path() / ("computer.cpp-lua-" + std::to_string(pid) + "-" + std::to_string(stamp) + ".lua");
}

int RunChildProcess(
    const std::vector<std::string>& args,
    bool agentStdio,
    ManagedControlSession* managedControlSession
) {
#if defined(__unix__) || defined(__APPLE__)
    Cli::PosixArgv argv(args);

    pid_t pid = ::fork();
    if (pid < 0) {
        std::cerr << "Error: failed to fork Lua runner\n";
        return 1;
    }
    if (pid == 0) {
        if (agentStdio) {
            ::setenv("COMPUTER_CPP_AGENT_STDIO", "1", 1);
        }
        ::execv(argv.front(), argv.data());
        std::cerr << "Error: failed to exec Lua interpreter: " << args[0] << "\n";
        _exit(127);
    }

    int status = 0;
    while (true) {
        pid_t waited = ::waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            break;
        }
        if (waited < 0 && errno == EINTR) {
            continue;
        }
        if (waited < 0) {
            std::cerr << "Error: failed waiting for Lua runner\n";
            return 1;
        }
        if (managedControlSession && !managedControlSession->RenewIfDue()) {
            ::kill(pid, SIGKILL);
            while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
            return 6;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        std::cerr << "Error: Lua runner terminated by signal " << WTERMSIG(status) << "\n";
        return 128 + WTERMSIG(status);
    }
    return 1;
#elif defined(_WIN32)
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    std::unique_ptr<Windows::ScopedEnvVar> agentEnv;
    if (agentStdio) {
        agentEnv = std::make_unique<Windows::ScopedEnvVar>(L"COMPUTER_CPP_AGENT_STDIO", L"1");
    }
    Windows::ProcessOptions processOptions;
    processOptions.inheritHandles = true;
    processOptions.startupInfo = &startupInfo;
    if (!Windows::StartProcess(args, processOptions, processInfo)) {
        std::cerr << "Error: failed to start Lua interpreter: " << args[0] << "\n";
        return 127;
    }
    while (true) {
        DWORD wait = WaitForSingleObject(processInfo.hProcess, 100);
        if (wait == WAIT_OBJECT_0) {
            break;
        }
        if (wait != WAIT_TIMEOUT) {
            TerminateProcess(processInfo.hProcess, 1);
            WaitForSingleObject(processInfo.hProcess, INFINITE);
            break;
        }
        if (managedControlSession && !managedControlSession->RenewIfDue()) {
            TerminateProcess(processInfo.hProcess, 6);
            WaitForSingleObject(processInfo.hProcess, INFINITE);
            break;
        }
    }
    int exitCode = Windows::ProcessExitCode(processInfo.hProcess);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return exitCode;
#else
    (void)args;
    (void)agentStdio;
    (void)managedControlSession;
    std::cerr << "Error: Lua runner is not implemented on this platform yet\n";
    return 1;
#endif
}

std::string ReadFileBestEffort(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

LuaRunResult RunChildProcessCapture(
    const std::vector<std::string>& args,
    bool agentStdio,
    bool streamStderr,
    ManagedControlSession* managedControlSession
) {
    LuaRunResult result;
#if defined(__unix__) || defined(__APPLE__)
    fs::path stdoutPath = TempPreludePath();
    stdoutPath += ".stdout";
    fs::path stderrPath;
    if (!streamStderr) {
        stderrPath = TempPreludePath();
        stderrPath += ".stderr";
    }

    Cli::PosixArgv argv(args);
    pid_t pid = ::fork();
    if (pid < 0) {
        result.exitCode = 1;
        result.stderrText = "Error: failed to fork Lua runner\n";
        return result;
    }
    if (pid == 0) {
        int stdoutFd = ::open(stdoutPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        int stderrFd = -1;
        if (!streamStderr) {
            stderrFd = ::open(stderrPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        }
        if (stdoutFd < 0 || (!streamStderr && stderrFd < 0)) {
            _exit(126);
        }
        ::dup2(stdoutFd, STDOUT_FILENO);
        if (!streamStderr) {
            ::dup2(stderrFd, STDERR_FILENO);
        }
        ::close(stdoutFd);
        if (stderrFd >= 0) {
            ::close(stderrFd);
        }
        if (agentStdio) {
            ::setenv("COMPUTER_CPP_AGENT_STDIO", "1", 1);
        }
        ::execv(argv.front(), argv.data());
        std::cerr << "Error: failed to exec Lua interpreter: " << args[0] << "\n";
        _exit(127);
    }

    int status = 0;
    bool waitedSuccessfully = false;
    while (true) {
        pid_t waited = ::waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            waitedSuccessfully = true;
            break;
        }
        if (waited < 0 && errno == EINTR) {
            continue;
        }
        if (waited < 0) {
            result.exitCode = 1;
            result.stderrText = "Error: failed waiting for Lua runner\n";
            break;
        }
        if (managedControlSession && !managedControlSession->RenewIfDue()) {
            ::kill(pid, SIGKILL);
            while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
            result.exitCode = 6;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (waitedSuccessfully && WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    } else if (waitedSuccessfully && WIFSIGNALED(status)) {
        result.exitCode = 128 + WTERMSIG(status);
        result.stderrText = "Error: Lua runner terminated by signal " + std::to_string(WTERMSIG(status)) + "\n";
    } else if (waitedSuccessfully) {
        result.exitCode = 1;
    }

    result.stdoutText = ReadFileBestEffort(stdoutPath);
    if (!streamStderr) {
        const std::string childStderr = ReadFileBestEffort(stderrPath);
        result.stderrText += childStderr;
    }
    std::error_code ec;
    fs::remove(stdoutPath, ec);
    if (!streamStderr) {
        fs::remove(stderrPath, ec);
    }
    return result;
#elif defined(_WIN32)
    fs::path stdoutPath = TempPreludePath();
    stdoutPath += ".stdout";
    fs::path stderrPath;
    if (!streamStderr) {
        stderrPath = TempPreludePath();
        stderrPath += ".stderr";
    }

    SECURITY_ATTRIBUTES attrs{};
    attrs.nLength = sizeof(attrs);
    attrs.bInheritHandle = TRUE;

    HANDLE stdoutHandle = CreateFileW(
        stdoutPath.wstring().c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        &attrs,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY,
        nullptr);
    HANDLE stderrHandle = INVALID_HANDLE_VALUE;
    if (!streamStderr) {
        stderrHandle = CreateFileW(
            stderrPath.wstring().c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            &attrs,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_TEMPORARY,
            nullptr);
    }
    if (stdoutHandle == INVALID_HANDLE_VALUE || (!streamStderr && stderrHandle == INVALID_HANDLE_VALUE)) {
        result.exitCode = 1;
        result.stderrText = "Error: failed to create Lua capture files\n";
        if (stdoutHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(stdoutHandle);
        }
        if (stderrHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(stderrHandle);
        }
        return result;
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startupInfo.hStdOutput = stdoutHandle;
    startupInfo.hStdError = streamStderr ? GetStdHandle(STD_ERROR_HANDLE) : stderrHandle;
    PROCESS_INFORMATION processInfo{};
    std::unique_ptr<Windows::ScopedEnvVar> agentEnv;
    if (agentStdio) {
        agentEnv = std::make_unique<Windows::ScopedEnvVar>(L"COMPUTER_CPP_AGENT_STDIO", L"1");
    }
    Windows::ProcessOptions processOptions;
    processOptions.inheritHandles = true;
    processOptions.startupInfo = &startupInfo;
    if (!Windows::StartProcess(args, processOptions, processInfo)) {
        result.exitCode = 127;
        result.stderrText = "Error: failed to start Lua interpreter: " + args[0] + "\n";
    } else {
        while (true) {
            DWORD wait = WaitForSingleObject(processInfo.hProcess, 100);
            if (wait == WAIT_OBJECT_0) {
                break;
            }
            if (wait != WAIT_TIMEOUT) {
                TerminateProcess(processInfo.hProcess, 1);
                WaitForSingleObject(processInfo.hProcess, INFINITE);
                break;
            }
            if (managedControlSession && !managedControlSession->RenewIfDue()) {
                TerminateProcess(processInfo.hProcess, 6);
                WaitForSingleObject(processInfo.hProcess, INFINITE);
                break;
            }
        }
        result.exitCode = Windows::ProcessExitCode(processInfo.hProcess);
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
    }
    CloseHandle(stdoutHandle);
    if (stderrHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(stderrHandle);
    }
    result.stdoutText = ReadFileBestEffort(stdoutPath);
    if (!streamStderr) {
        result.stderrText += ReadFileBestEffort(stderrPath);
    }
    std::error_code ec;
    fs::remove(stdoutPath, ec);
    if (!streamStderr) {
        fs::remove(stderrPath, ec);
    }
    return result;
#else
    (void)args;
    (void)agentStdio;
    (void)streamStderr;
    (void)managedControlSession;
    result.exitCode = 1;
    result.stderrText = "Error: Lua runner is not implemented on this platform yet\n";
    return result;
#endif
}

LuaRunResult RunLuaScriptInternal(const LuaRunOptions& options, bool capture, bool streamStderr) {
    LuaRunResult result;
    if (options.scriptPath.empty()) {
        result.exitCode = 2;
        result.stderrText = "Error: run requires a Lua script path\n";
        return result;
    }
    if (!fs::exists(options.scriptPath)) {
        result.exitCode = 2;
        result.stderrText = "Error: Lua script not found: " + options.scriptPath.string() + "\n";
        return result;
    }

    fs::path lua = FindLuaInterpreter(options.executablePath);
    if (lua.empty()) {
        result.exitCode = 1;
        result.stderrText = "Error: Lua runtime not found. This build may be missing its bundled Lua runtime. Set COMPUTER_CPP_LUA to override.\n";
        return result;
    }

    LuaRunOptions effectiveOptions = options;
    ManagedControlSession managedControlSession;
    if (!effectiveOptions.dryRun &&
        effectiveOptions.controlSessionToken.empty() &&
        effectiveOptions.acquireControlSession) {
        ControlSessionResult acquired = managedControlSession.Acquire(effectiveOptions);
        if (!acquired.ok) {
            result.exitCode = acquired.code == "control_session_busy" ? 6 : 1;
            result.stderrText = "Error: " +
                (acquired.error.empty() ? "could not acquire desktop control" : acquired.error) +
                "\n";
            return result;
        }
        effectiveOptions.controlSessionToken = managedControlSession.token();
        effectiveOptions.controlScope = acquired.record.scope;
    }

    fs::path prelude = TempPreludePath();
    {
        std::ofstream file(prelude);
        if (!file) {
            result.exitCode = 1;
            result.stderrText = "Error: could not write Lua prelude: " + prelude.string() + "\n";
            return result;
        }
        file << LuaPreludeSource(effectiveOptions);
    }

    std::vector<std::string> args = {
        lua.string(),
        prelude.string(),
        fs::absolute(options.scriptPath).string(),
    };
    args.insert(args.end(), options.scriptArgs.begin(), options.scriptArgs.end());

    if (capture) {
        result = RunChildProcessCapture(
            args,
            effectiveOptions.agentStdio,
            streamStderr,
            managedControlSession.token().empty() ? nullptr : &managedControlSession);
    } else {
        result.exitCode = RunChildProcess(
            args,
            effectiveOptions.agentStdio,
            managedControlSession.token().empty() ? nullptr : &managedControlSession);
    }
    std::error_code ec;
    fs::remove(prelude, ec);
    managedControlSession.StopAndRelease();
    const std::string renewalError = managedControlSession.RenewalError();
    if (!renewalError.empty()) {
        result.exitCode = 6;
        result.stdoutText.clear();
        result.stderrText += "Error: lost exclusive desktop control: " + renewalError + "\n";
    }
    return result;
}

}

int RunLuaScript(const LuaRunOptions& options) {
    LuaRunResult result = RunLuaScriptInternal(options, false, false);
    if (!result.stderrText.empty()) {
        std::cerr << result.stderrText;
    }
    if (!result.stdoutText.empty()) {
        std::cout << result.stdoutText;
    }
    return result.exitCode;
}

LuaRunResult RunLuaScriptCapture(const LuaRunOptions& options) {
    return RunLuaScriptInternal(options, true, false);
}

LuaRunResult RunLuaScriptCapture(const LuaRunOptions& options, bool streamStderr) {
    return RunLuaScriptInternal(options, true, streamStderr);
}

}
