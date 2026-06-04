#include "CliSessionProcess.h"

#include "computer_cpp/ControlSession.h"
#include "PosixArgv.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ComputerCpp::Cli {
namespace {

#if defined(__unix__) || defined(__APPLE__)
std::atomic<int> gSessionRunSignal = 0;

void SessionRunSignalHandler(int signal) {
    gSessionRunSignal.store(signal);
}

class ScopedSessionRunSignals {
public:
    ScopedSessionRunSignals() {
        gSessionRunSignal.store(0);
        previousInt_ = std::signal(SIGINT, SessionRunSignalHandler);
        previousTerm_ = std::signal(SIGTERM, SessionRunSignalHandler);
        previousHup_ = std::signal(SIGHUP, SessionRunSignalHandler);
    }

    ~ScopedSessionRunSignals() {
        std::signal(SIGINT, previousInt_);
        std::signal(SIGTERM, previousTerm_);
        std::signal(SIGHUP, previousHup_);
        gSessionRunSignal.store(0);
    }

private:
    using Handler = void (*)(int);
    Handler previousInt_ = SIG_DFL;
    Handler previousTerm_ = SIG_DFL;
    Handler previousHup_ = SIG_DFL;
};

int ChildExitCodeFromStatus(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

void TerminateChildGroup(pid_t pid, int graceMs = 5000) {
    if (pid <= 0) {
        return;
    }
    ::kill(-pid, SIGTERM);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(graceMs);
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        pid_t result = ::waitpid(pid, &status, WNOHANG);
        if (result == pid || (result < 0 && errno == ECHILD)) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ::kill(-pid, SIGKILL);
    while (true) {
        pid_t result = ::waitpid(pid, &status, 0);
        if (result == pid || (result < 0 && errno == ECHILD)) {
            return;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        return;
    }
}
#endif

} // namespace

int RunChildWithControlSession(
    const std::vector<std::string>& command,
    const std::string& token,
    const std::string& scope,
    int64_t ttlMs,
    int64_t maxMs
) {
#if defined(__unix__) || defined(__APPLE__)
    PosixArgv argv(command);

    pid_t pid = ::fork();
    if (pid < 0) {
        std::cerr << "Error: failed to fork child command\n";
        return 1;
    }
    if (pid == 0) {
        ::setpgid(0, 0);
        ::setenv("COMPUTER_CPP_CONTROL_SESSION", token.c_str(), 1);
        ::setenv("COMPUTER_CPP_CONTROL_SCOPE", scope.c_str(), 1);
        ::execvp(argv.front(), argv.data());
        std::cerr << "Error: failed to exec child command: " << command[0] << "\n";
        _exit(127);
    }
    ::setpgid(pid, pid);

    ScopedSessionRunSignals signals;
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = maxMs > 0 ? started + std::chrono::milliseconds(maxMs)
                                    : std::chrono::steady_clock::time_point::max();
    const int64_t renewIntervalMs = std::clamp(ttlMs / 3, static_cast<int64_t>(250), static_cast<int64_t>(30000));
    auto nextRenew = std::chrono::steady_clock::now() + std::chrono::milliseconds(renewIntervalMs);
    int status = 0;

    while (true) {
        pid_t result = ::waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            return ChildExitCodeFromStatus(status);
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result < 0 && errno != ECHILD) {
            std::cerr << "Error: failed waiting for child command\n";
            TerminateChildGroup(pid);
            return 1;
        }
        if (result < 0 && errno == ECHILD) {
            return 1;
        }

        int signal = gSessionRunSignal.load();
        if (signal != 0) {
            std::cerr << "session run interrupted; terminating child process group\n";
            TerminateChildGroup(pid);
            return 128 + signal;
        }

        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            std::cerr << "session run exceeded --max; terminating child process group\n";
            TerminateChildGroup(pid);
            return 4;
        }

        if (now >= nextRenew) {
            auto renew = RenewControlSession(token, ttlMs);
            if (!renew.ok) {
                std::cerr << "Error: control session renew failed: "
                          << (renew.error.empty() ? "unknown error" : renew.error)
                          << "; terminating child process group\n";
                TerminateChildGroup(pid);
                return 6;
            }
            nextRenew = now + std::chrono::milliseconds(renewIntervalMs);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
#else
    (void)command;
    (void)token;
    (void)scope;
    (void)ttlMs;
    (void)maxMs;
    std::cerr << "Error: session child process execution is not implemented on this platform\n";
    return 8;
#endif
}

} // namespace ComputerCpp::Cli
