#include "computer_cpp/Transport.h"

#include "computer_cpp/Daemon.h"
#include "computer_cpp/WindowsUtil.h"

#include "DaemonParsing.h"
#include "DaemonSocket.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

using json = nlohmann::json;

namespace ComputerCpp {

namespace {

std::string ReadLineFromSocketFd(int fd) {
    std::string line;
#if defined(__unix__) || defined(__APPLE__)
    char ch = 0;
    while (true) {
        ssize_t n = ::read(fd, &ch, 1);
        if (n <= 0) break;
        if (ch == '\n') break;
        line.push_back(ch);
    }
#else
    (void)fd;
#endif
    return line;
}

bool WriteAll(int fd, const std::string& payload) {
#if defined(__unix__) || defined(__APPLE__)
    const char* ptr = payload.data();
    size_t remaining = payload.size();
    while (remaining > 0) {
        ssize_t written = ::write(fd, ptr, remaining);
        if (written <= 0) return false;
        ptr += written;
        remaining -= static_cast<size_t>(written);
    }
    return true;
#else
    (void)fd;
    (void)payload;
    return false;
#endif
}

int TransportTimeoutMs() {
    const char* raw = std::getenv("COMPUTER_CPP_TRANSPORT_TIMEOUT_MS");
    int value = 300000;
    if (raw && *raw) {
        if (auto parsed = ParseIntegerStrict<int>(raw)) {
            value = *parsed;
        }
    }
    return std::clamp(value, 1000, 900000);
}

void SetSocketTimeouts(int fd) {
#if defined(__unix__) || defined(__APPLE__)
    int timeoutMs = TransportTimeoutMs();
    timeval timeout {};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#else
    (void)fd;
#endif
}

void SetCloseOnExec(int fd) {
#if defined(__unix__) || defined(__APPLE__)
    if (fd < 0) {
        return;
    }
    int flags = ::fcntl(fd, F_GETFD);
    if (flags >= 0) {
        ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }
#else
    (void)fd;
#endif
}

bool ConnectedPeerLooksLikeDaemon(int fd) {
#if defined(__linux__)
    ucred cred {};
    socklen_t len = sizeof(cred);
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) {
        return true;
    }
    std::ifstream comm("/proc/" + std::to_string(cred.pid) + "/comm");
    std::string name;
    std::getline(comm, name);
    if (name.find("computer.cpp") != std::string::npos) {
        return true;
    }
    std::error_code ec;
    auto exe = std::filesystem::read_symlink("/proc/" + std::to_string(cred.pid) + "/exe", ec).string();
    return !ec && exe.find("computer.cpp") != std::string::npos;
#else
    (void)fd;
    return true;
#endif
}

std::string MacComputerCppAppBundleForCli(const std::string& executablePath) {
#if defined(__APPLE__)
    std::filesystem::path executable(executablePath);
    std::error_code ec;
    if (executable.is_relative()) {
        executable = std::filesystem::absolute(executable, ec);
        ec.clear();
    }
    std::vector<std::filesystem::path> candidates = {
        executable.parent_path() / "ComputerCpp.app",
        executable.parent_path().parent_path() / "Applications" / "ComputerCpp.app",
        "/Applications/ComputerCpp.app",
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return candidate.string();
        }
        ec.clear();
    }
#else
    (void)executablePath;
#endif
    return "";
}

int ConnectUnixSocket(const std::string& session) {
#if defined(__unix__) || defined(__APPLE__)
    int fd = ::socket(AF_UNIX, SOCK_STREAM
#if defined(SOCK_CLOEXEC)
        | SOCK_CLOEXEC
#endif
        , 0);
    if (fd < 0) {
        return -1;
    }
    SetCloseOnExec(fd);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::string path = SocketPathForSession(session).string();
    if (path.size() >= sizeof(addr.sun_path)) {
        ::close(fd);
        return -1;
    }
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }
    if (!ConnectedPeerLooksLikeDaemon(fd)) {
        ::close(fd);
        return -1;
    }
    SetSocketTimeouts(fd);
    return fd;
#else
    (void)session;
    return -1;
#endif
}

#if defined(_WIN32)
HANDLE ConnectNamedPipeClient(const std::string& session) {
    std::wstring pipeName = Windows::Utf8ToWide(PipeNameForSession(session));
    const DWORD timeoutMs = static_cast<DWORD>(TransportTimeoutMs());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    const auto missingPipeDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (std::chrono::steady_clock::now() < deadline) {
        HANDLE pipe = CreateFileW(
            pipeName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            DWORD mode = PIPE_READMODE_BYTE;
            SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
            return pipe;
        }
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) {
            if (std::chrono::steady_clock::now() >= missingPipeDeadline) {
                return INVALID_HANDLE_VALUE;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (error != ERROR_PIPE_BUSY) {
            return INVALID_HANDLE_VALUE;
        }
        WaitNamedPipeW(pipeName.c_str(), std::min<DWORD>(250, timeoutMs));
    }
    return INVALID_HANDLE_VALUE;
}

#endif

}

bool IsDaemonReady(const std::string& session) {
#if defined(_WIN32)
    HANDLE pipe = ConnectNamedPipeClient(session);
    if (pipe == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(pipe);
    return true;
#else
    int fd = ConnectUnixSocket(session);
    if (fd < 0) {
        return false;
    }
    ::close(fd);
    return true;
#endif
}

json SendDaemonRequest(const std::string& session, const json& request) {
#if defined(_WIN32)
    HANDLE pipe = ConnectNamedPipeClient(session);
    if (pipe == INVALID_HANDLE_VALUE) {
        return {{"ok", false}, {"error", "daemon is not running"}, {"code", "daemon_unavailable"}};
    }
    std::string line = request.dump();
    line.push_back('\n');
    if (!WriteAllToPipe(pipe, line)) {
        CloseHandle(pipe);
        return {{"ok", false}, {"error", "failed to write request"}, {"code", "transport_write_failed"}};
    }
    std::string responseLine = ReadLineFromPipe(pipe);
    CloseHandle(pipe);
    auto response = json::parse(responseLine, nullptr, false);
    if (response.is_discarded()) {
        return {{"ok", false}, {"error", "invalid daemon response: malformed JSON"}, {"code", "bad_response"}};
    }
    return response;
#else
    int fd = ConnectUnixSocket(session);
    if (fd < 0) {
        return {{"ok", false}, {"error", "daemon is not running"}, {"code", "daemon_unavailable"}};
    }
    std::string line = request.dump();
    line.push_back('\n');
    if (!WriteAll(fd, line)) {
        ::close(fd);
        return {{"ok", false}, {"error", "failed to write request"}, {"code", "transport_write_failed"}};
    }
    std::string responseLine = ReadLineFromSocketFd(fd);
    ::close(fd);
    auto response = json::parse(responseLine, nullptr, false);
    if (response.is_discarded()) {
        return {{"ok", false}, {"error", "invalid daemon response: malformed JSON"}, {"code", "bad_response"}};
    }
    return response;
#endif
}

DaemonStartResult EnsureDaemon(const std::string& session, const std::string& executablePath) {
    DaemonStartResult result;
    if (IsDaemonReady(session)) {
        result.connectedExisting = true;
        return result;
    }

#if defined(__unix__) || defined(__APPLE__)
    pid_t pid = ::fork();
    if (pid < 0) {
        result.error = "fork failed";
        return result;
    }

    if (pid == 0) {
        int devnull = ::open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            ::dup2(devnull, STDIN_FILENO);
            ::dup2(devnull, STDOUT_FILENO);
            ::dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) {
                ::close(devnull);
            }
        }
        ::setsid();
#if defined(__APPLE__)
        std::string appBundle = MacComputerCppAppBundleForCli(executablePath);
        if (!appBundle.empty()) {
            ::execl("/usr/bin/open", "/usr/bin/open", appBundle.c_str(), nullptr);
            _exit(127);
        }
#endif
        ::execl(executablePath.c_str(), executablePath.c_str(), "daemon", "--session", session.c_str(), nullptr);
        _exit(127);
    }

    result.started = true;
    for (int i = 0; i < 50; ++i) {
        if (IsDaemonReady(session)) {
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    result.error = "daemon did not become ready";
    return result;
#elif defined(_WIN32)
    std::vector<std::string> command = {executablePath, "daemon", "--session", session};
    if (!Windows::LaunchDetached(command)) {
        result.error = "failed to start daemon";
        return result;
    }

    result.started = true;
    for (int i = 0; i < 50; ++i) {
        if (IsDaemonReady(session)) {
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    result.error = "daemon did not become ready";
    return result;
#else
    result.error = "daemon auto-start is not implemented on this platform";
    return result;
#endif
}

bool StopDaemon(const std::string& session) {
    json response = SendDaemonRequest(session, {{"id", "shutdown"}, {"method", "shutdown"}, {"params", json::object()}});
    return response.value("ok", false);
}

}
