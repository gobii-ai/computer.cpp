#include "computer_cpp/LuaRunner.h"
#include "computer_cpp/WindowsUtil.h"

#include "LuaPrelude.h"
#include "PosixArgv.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace ComputerCpp {

namespace {
bool IsExecutable(const fs::path& path) {
#if defined(__unix__) || defined(__APPLE__)
    return ::access(path.c_str(), X_OK) == 0;
#else
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec;
#endif
}

std::vector<std::string> PathExtensions() {
#if defined(_WIN32)
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
#else
    return {""};
#endif
}

fs::path FindOnPath(const std::string& name) {
    if (name.find('/') != std::string::npos
#if defined(_WIN32)
        || name.find('\\') != std::string::npos || name.find(':') != std::string::npos
#endif
    ) {
        fs::path path(name);
        if (IsExecutable(path)) {
            return path;
        }
#if defined(_WIN32)
        if (!path.has_extension()) {
            for (const auto& ext : PathExtensions()) {
                fs::path candidate = path;
                candidate += ext;
                if (IsExecutable(candidate)) {
                    return candidate;
                }
            }
        }
#endif
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
        for (const auto& ext : PathExtensions()) {
            fs::path candidate = fs::path(dir) / name;
#if defined(_WIN32)
            if (!candidate.has_extension()) {
                candidate += ext;
            }
#else
            (void)ext;
#endif
            if (IsExecutable(candidate)) {
                return candidate;
            }
        }
    }
    return {};
}

std::vector<fs::path> BundledLuaCandidates(const fs::path& executablePath) {
    if (executablePath.empty()) {
        return {};
    }
    fs::path executable = executablePath;
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

int RunChildProcess(const std::vector<std::string>& args, bool agentStdio) {
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
    if (::waitpid(pid, &status, 0) < 0) {
        std::cerr << "Error: failed waiting for Lua runner\n";
        return 1;
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
    WaitForSingleObject(processInfo.hProcess, INFINITE);
    int exitCode = Windows::ProcessExitCode(processInfo.hProcess);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return exitCode;
#else
    (void)args;
    (void)agentStdio;
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

LuaRunResult RunChildProcessCapture(const std::vector<std::string>& args, bool agentStdio, bool streamStderr) {
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
    if (::waitpid(pid, &status, 0) < 0) {
        result.exitCode = 1;
        result.stderrText = "Error: failed waiting for Lua runner\n";
    } else if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exitCode = 128 + WTERMSIG(status);
        result.stderrText = "Error: Lua runner terminated by signal " + std::to_string(WTERMSIG(status)) + "\n";
    } else {
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
        WaitForSingleObject(processInfo.hProcess, INFINITE);
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

    fs::path prelude = TempPreludePath();
    {
        std::ofstream file(prelude);
        if (!file) {
            result.exitCode = 1;
            result.stderrText = "Error: could not write Lua prelude: " + prelude.string() + "\n";
            return result;
        }
        file << LuaPreludeSource(options);
    }

    std::vector<std::string> args = {
        lua.string(),
        prelude.string(),
        fs::absolute(options.scriptPath).string(),
    };
    args.insert(args.end(), options.scriptArgs.begin(), options.scriptArgs.end());

    if (capture) {
        result = RunChildProcessCapture(args, options.agentStdio, streamStderr);
    } else {
        result.exitCode = RunChildProcess(args, options.agentStdio);
    }
    std::error_code ec;
    fs::remove(prelude, ec);
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
