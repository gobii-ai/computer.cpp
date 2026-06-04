#include "computer_cpp/LuaRunner.h"

#include "LuaPrelude.h"
#include "PosixArgv.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace ComputerCpp {

namespace {
bool IsExecutable(const fs::path& path) {
#if defined(__unix__) || defined(__APPLE__)
    return ::access(path.c_str(), X_OK) == 0;
#else
    return fs::exists(path);
#endif
}

fs::path FindOnPath(const std::string& name) {
    if (name.find('/') != std::string::npos) {
        fs::path path(name);
        return IsExecutable(path) ? path : fs::path();
    }
    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv) {
        return {};
    }
    std::stringstream stream(pathEnv);
    std::string dir;
    while (std::getline(stream, dir, ':')) {
        fs::path candidate = fs::path(dir) / name;
        if (IsExecutable(candidate)) {
            return candidate;
        }
    }
    return {};
}

fs::path FindLuaInterpreter() {
    if (const char* configured = std::getenv("COMPUTER_CPP_LUA")) {
        fs::path path = FindOnPath(configured);
        if (!path.empty()) {
            return path;
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

fs::path TempPreludePath() {
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
#if defined(__unix__) || defined(__APPLE__)
    auto pid = static_cast<long long>(::getpid());
#else
    auto pid = 0LL;
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

    fs::path lua = FindLuaInterpreter();
    if (lua.empty()) {
        result.exitCode = 1;
        result.stderrText = "Error: Lua interpreter not found. Install lua or set COMPUTER_CPP_LUA.\n";
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
