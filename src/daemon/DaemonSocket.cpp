#include "DaemonSocket.h"

#include "computer_cpp/AppPaths.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string_view>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace ComputerCpp {

std::string ReadLineFromFd(int fd) {
    std::string line;
#if defined(__unix__) || defined(__APPLE__)
    char ch = 0;
    while (true) {
        ssize_t n = ::read(fd, &ch, 1);
        if (n <= 0) {
            break;
        }
        if (ch == '\n') {
            break;
        }
        line.push_back(ch);
    }
#else
    (void)fd;
#endif
    return line;
}

void WriteJsonLineToFd(int fd, const nlohmann::json& response) {
#if defined(__unix__) || defined(__APPLE__)
    std::string line = response.dump();
    line.push_back('\n');
    const char* ptr = line.data();
    size_t remaining = line.size();
    while (remaining > 0) {
        ssize_t written = ::write(fd, ptr, remaining);
        if (written <= 0) {
            break;
        }
        ptr += written;
        remaining -= static_cast<size_t>(written);
    }
#else
    (void)fd;
    (void)response;
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

bool IsSessionNameValid(const std::string& session) {
    if (session.empty() || session.size() > 80) {
        return false;
    }
    for (char c : session) {
        bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.';
        if (!ok) {
            return false;
        }
    }
    return session != "." && session != "..";
}

std::filesystem::path SocketPathForSession(const std::string& session) {
    auto dir = AppDataDir() / "run";
    EnsureDirectory(dir);
    auto path = dir / (session + ".sock");
#if defined(__unix__) || defined(__APPLE__)
    if (path.string().size() >= sizeof(sockaddr_un::sun_path)) {
        auto shortDir = std::filesystem::temp_directory_path() / "computer.cpp-run";
        EnsureDirectory(shortDir);
        std::ostringstream name;
        name << "ac-" << std::hex << std::hash<std::string>{}(path.string()) << ".sock";
        return shortDir / name.str();
    }
#endif
    return path;
}

std::filesystem::path PidPathForSession(const std::string& session) {
    auto dir = AppDataDir() / "run";
    EnsureDirectory(dir);
    return dir / (session + ".pid");
}

std::string PipeNameForSession(const std::string& session) {
    return "\\\\.\\pipe\\computer.cpp-" + session;
}

#if defined(_WIN32)
std::string ReadLineFromPipe(HANDLE pipe) {
    std::string line;
    char ch = 0;
    DWORD read = 0;
    while (ReadFile(pipe, &ch, 1, &read, nullptr) && read == 1) {
        if (ch == '\n') {
            break;
        }
        line.push_back(ch);
    }
    return line;
}

bool WriteAllToPipe(HANDLE pipe, std::string_view payload) {
    const char* ptr = payload.data();
    size_t remaining = payload.size();
    while (remaining > 0) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(remaining, 64 * 1024));
        DWORD written = 0;
        if (!WriteFile(pipe, ptr, chunk, &written, nullptr) || written == 0) {
            return false;
        }
        ptr += written;
        remaining -= written;
    }
    return true;
}

void WriteJsonLineToPipe(HANDLE pipe, const nlohmann::json& response) {
    std::string line = response.dump();
    line.push_back('\n');
    WriteAllToPipe(pipe, line);
}
#endif

} // namespace ComputerCpp
