#include "TrayIcon.h"
#include "GobiiConnectionDialog.h"

#include "computer_cpp/AppConfig.h"
#include "computer_cpp/AppPaths.h"
#include "computer_cpp/CommandRecording.h"
#include "computer_cpp/Daemon.h"
#include "computer_cpp/ConfiguredServerController.h"
#include "computer_cpp/GobiiArtifactUploader.h"
#include "computer_cpp/GobiiConnectionController.h"
#include "computer_cpp/GobiiCredentialStore.h"
#include "computer_cpp/InferenceClient.h"
#include "computer_cpp/Platform.h"
#include "computer_cpp/StringUtils.h"
#include "computer_cpp/Transport.h"
#include "computer_cpp/TrayServerState.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <functional>
#include <ctime>
#include <iomanip>
#include <map>
#include <optional>
#include <future>
#include <sstream>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>
#include <wx/clipbrd.h>
#include <wx/collpane.h>
#include <wx/dcmemory.h>
#include <wx/filedlg.h>
#include <wx/graphics.h>
#include <wx/icon.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/process.h>
#include <wx/radiobut.h>
#include <wx/scrolwin.h>
#include <wx/simplebook.h>
#include <wx/socket.h>
#include <wx/statline.h>
#include <wx/stdpaths.h>
#include <wx/settings.h>
#include <wx/timer.h>
#include <wx/wx.h>

#ifdef _WIN32
#include <process.h>
#endif

#if defined(__unix__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using json = nlohmann::json;

namespace ComputerCpp::App {

enum {
    ID_PERMISSIONS = 1001,
    ID_SETTINGS,
    ID_RECORDING_TOGGLE,
    ID_SHOW_LOGS,
    ID_CHECK_UPDATES,
    ID_START_SERVER,
    ID_STOP_SERVER,
    ID_GOBII_CONNECT,
    ID_GOBII_STATUS,
    ID_GOBII_PAUSE_RESUME,
    ID_GOBII_DISCONNECT,
    ID_GOBII_MANAGE,
    ID_SERVER_PROCESS,
    ID_SERVER_TIMER,
    ID_STATE,
    ID_TEST_SCREENSHOT,
    ID_TEST_MOUSE,
    ID_QUIT = wxID_EXIT
};

std::string TemporaryScreenshotPath(const char* filename) {
    return (std::filesystem::path(
                wxStandardPaths::Get().GetTempDir().ToStdString()) /
            filename)
        .string();
}

wxSize TrayIconBitmapSize() {
    int size = 22;
#ifndef __WXOSX__
    int width = wxSystemSettings::GetMetric(wxSYS_SMALLICON_X);
    int height = wxSystemSettings::GetMetric(wxSYS_SMALLICON_Y);
    if (width > 0 && height > 0) {
        size = std::clamp(std::max(width, height), 16, 32);
    }
#endif
    return wxSize(size, size);
}

bool TrayIconPrefersDarkForeground();

#ifndef __APPLE__
bool TrayIconPrefersDarkForeground() {
    return !wxSystemSettings::GetAppearance().IsDark();
}
#else
void* CreateNativeTrayIcon(TrayIcon* owner);
void DestroyNativeTrayIcon(void* handle);
#endif

wxIcon CreateComputerTrayIcon() {
    wxSize size = TrayIconBitmapSize();

    wxBitmap bitmap(size.GetWidth(), size.GetHeight(), 32);
    bitmap.UseAlpha();

    wxMemoryDC dc(bitmap);
    dc.SetBackground(wxBrush(wxColour(0, 0, 0, 0)));
    dc.Clear();

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
    if (gc) {
        const double scale = static_cast<double>(std::min(size.GetWidth(), size.GetHeight())) / 22.0;
        auto s = [scale](double value) { return value * scale; };

        auto drawRounded = [&](double x,
                               double y,
                               double width,
                               double height,
                               double radius,
                               const wxColour& fill,
                               const wxColour& stroke = wxColour(0, 0, 0, 0),
                               double strokeWidth = 0.0) {
            gc->SetBrush(wxBrush(fill));
            if (strokeWidth > 0.0) {
                gc->SetPen(wxPen(stroke, std::max(1, static_cast<int>(std::round(strokeWidth * scale)))));
            } else {
                gc->SetPen(*wxTRANSPARENT_PEN);
            }
            gc->DrawRoundedRectangle(s(x), s(y), s(width), s(height), s(radius));
        };

        auto basePath = [&](double topInset, double topY, double bottomInset, double bottomY) {
            wxGraphicsPath path = gc->CreatePath();
            path.MoveToPoint(s(topInset), s(topY));
            path.AddLineToPoint(s(22.0 - topInset), s(topY));
            path.AddLineToPoint(s(22.0 - bottomInset), s(bottomY));
            path.AddLineToPoint(s(bottomInset), s(bottomY));
            path.CloseSubpath();
            return path;
        };

        gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

        const bool darkForeground =
#ifdef __APPLE__
            true;
#else
            TrayIconPrefersDarkForeground();
#endif
        const wxColour foreground = darkForeground
            ? wxColour(24, 31, 42, 245)
            : wxColour(248, 250, 252, 248);
        const wxColour midtone = darkForeground
            ? wxColour(75, 85, 99, 210)
            : wxColour(203, 213, 225, 215);
        const wxColour softFill = darkForeground
            ? wxColour(24, 31, 42, 28)
            : wxColour(248, 250, 252, 34);
        const wxColour screenFill = darkForeground
            ? wxColour(24, 31, 42, 40)
            : wxColour(248, 250, 252, 48);
        const wxColour shadow = darkForeground
            ? wxColour(255, 255, 255, 34)
            : wxColour(0, 0, 0, 78);

        drawRounded(4.2, 4.4, 13.6, 10.9, 2.2, shadow);
        drawRounded(4.0, 3.3, 14.0, 11.3, 2.0, softFill, foreground, 1.35);
        drawRounded(5.8, 5.3, 10.4, 6.5, 1.0, screenFill, midtone, 0.65);
        drawRounded(7.1, 6.3, 7.8, 1.0, 0.45, wxColour(midtone.Red(), midtone.Green(), midtone.Blue(), 120));

        wxGraphicsPath baseShadow = basePath(3.7, 15.2, 2.0, 18.0);
        gc->SetBrush(wxBrush(shadow));
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->FillPath(baseShadow);

        wxGraphicsPath base = basePath(3.1, 14.8, 2.0, 17.4);
        gc->SetBrush(wxBrush(softFill));
        gc->SetPen(wxPen(foreground, std::max(1, static_cast<int>(std::round(0.9 * scale)))));
        gc->DrawPath(base);

        drawRounded(8.7, 15.6, 4.6, 0.8, 0.35, wxColour(midtone.Red(), midtone.Green(), midtone.Blue(), 165));
        drawRounded(2.4, 17.0, 17.2, 1.4, 0.55, foreground);

        delete gc;
    }

    dc.SelectObject(wxNullBitmap);

    wxIcon icon;
    icon.CopyFromBitmap(bitmap);
    return icon;
}

std::string ShellQuote(const std::string& value) {
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

std::string ComputerCppBundlePath() {
    std::filesystem::path executablePath(wxStandardPaths::Get().GetExecutablePath().ToStdString());
    std::string path = executablePath.string();
    size_t bundleMarker = path.find(".app/");
    if (bundleMarker == std::string::npos) {
        return "";
    }
    return path.substr(0, bundleMarker + 4);
}

std::filesystem::path ComputerCppCliHelperPath() {
    std::filesystem::path executablePath(wxStandardPaths::Get().GetExecutablePath().ToStdString());
    std::string bundlePath = ComputerCppBundlePath();
    if (!bundlePath.empty()) {
        std::filesystem::path bundled = std::filesystem::path(bundlePath) / "Contents" / "MacOS" / "computer.cpp";
        std::error_code ec;
        if (std::filesystem::exists(bundled, ec) && !ec) {
            return bundled;
        }
    }
    return executablePath.parent_path() / "computer.cpp";
}

std::string NormalizeBindHost(std::string host) {
    host = ComputerCpp::Trim(host);
    if (host.empty()) {
        return "127.0.0.1";
    }
    return host == "localhost" ? "127.0.0.1" : host;
}

bool IsTcpPortAvailable(const std::string& host, int port) {
    if (port <= 0 || port > 65535) {
        return false;
    }
#if defined(__unix__) || defined(__APPLE__)
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    int reuse = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    std::string bindHost = NormalizeBindHost(host);
    if (::inet_pton(AF_INET, bindHost.c_str(), &addr.sin_addr) != 1) {
        ::close(fd);
        return false;
    }
    bool available = ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    ::close(fd);
    return available;
#else
    wxIPV4address addr;
    addr.Hostname(NormalizeBindHost(host));
    addr.Service(port);
    wxSocketServer server(addr);
    return server.IsOk();
#endif
}

std::string HealthConnectHost(const std::string& host) {
    std::string normalized = NormalizeBindHost(host);
    return normalized == "0.0.0.0" ? "127.0.0.1" : normalized;
}

bool HttpServerRequestOk(
    const TrayAppServerState& state,
    const std::string& bearerToken,
    const std::string& method,
    const std::string& path,
    int expectedStatus,
    int timeoutMs = 1000,
    std::string* responseBody = nullptr
) {
    if (bearerToken.empty() || state.port <= 0 || state.port > 65535) {
        return false;
    }
    timeoutMs = std::max(1, timeoutMs);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    const auto remainingTimeoutMs = [&] {
        return static_cast<int>(std::max<int64_t>(
            0,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count()));
    };
#if defined(__unix__) || defined(__APPLE__)
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    timeval timeout {};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(state.port));
    std::string host = HealthConnectHost(state.host);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        ::close(fd);
        return false;
    }
    const int originalFlags = ::fcntl(fd, F_GETFL, 0);
    if (originalFlags < 0 ||
        ::fcntl(fd, F_SETFL, originalFlags | O_NONBLOCK) != 0) {
        ::close(fd);
        return false;
    }
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (errno != EINPROGRESS) {
            ::close(fd);
            return false;
        }
        pollfd descriptor{};
        descriptor.fd = fd;
        descriptor.events = POLLOUT;
        int ready = 0;
        do {
            ready = ::poll(&descriptor, 1, remainingTimeoutMs());
        } while (ready < 0 && errno == EINTR);
        int socketError = 0;
        socklen_t socketErrorSize = sizeof(socketError);
        if (ready <= 0 ||
            ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &socketErrorSize) != 0 ||
            socketError != 0) {
            ::close(fd);
            return false;
        }
    }
    if (::fcntl(fd, F_SETFL, originalFlags) != 0) {
        ::close(fd);
        return false;
    }

    std::string request =
        method + " " + path + " HTTP/1.1\r\nHost: " + host + ":" + std::to_string(state.port) +
        "\r\nAuthorization: Bearer " + bearerToken +
        "\r\nContent-Length: 0" +
        "\r\nConnection: close\r\n\r\n";
    const char* cursor = request.data();
    size_t remaining = request.size();
    while (remaining > 0) {
        ssize_t sent = ::send(fd, cursor, remaining, 0);
        if (sent <= 0) {
            ::close(fd);
            return false;
        }
        cursor += sent;
        remaining -= static_cast<size_t>(sent);
    }

    std::string response;
    std::array<char, 4096> buffer{};
    while (response.size() < 1024 * 1024) {
        const int remainingMs = remainingTimeoutMs();
        if (remainingMs <= 0) {
            break;
        }
        timeout.tv_sec = remainingMs / 1000;
        timeout.tv_usec = (remainingMs % 1000) * 1000;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ssize_t read = ::recv(fd, buffer.data(), buffer.size(), 0);
        if (read == 0) {
            break;
        }
        if (read < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            ::close(fd);
            return false;
        }
        response.append(buffer.data(), static_cast<size_t>(read));
    }
    ::close(fd);
    if (response.empty()) {
        return false;
    }
    std::string expected11 = "HTTP/1.1 " + std::to_string(expectedStatus);
    std::string expected10 = "HTTP/1.0 " + std::to_string(expectedStatus);
    const bool statusOk =
        response.rfind(expected11, 0) == 0 || response.rfind(expected10, 0) == 0;
    if (statusOk && responseBody) {
        const size_t bodyStart = response.find("\r\n\r\n");
        *responseBody = bodyStart == std::string::npos
            ? std::string()
            : response.substr(bodyStart + 4);
    }
    return statusOk;
#else
    wxSocketClient socket;
    socket.SetTimeout(std::max(1, (timeoutMs + 999) / 1000));
    wxIPV4address addr;
    addr.Hostname(HealthConnectHost(state.host));
    addr.Service(state.port);
    socket.Connect(addr, false);
    int remainingMs = remainingTimeoutMs();
    if (remainingMs <= 0 ||
        !socket.WaitOnConnect(remainingMs / 1000, remainingMs % 1000) ||
        !socket.IsConnected()) {
        return false;
    }
    std::string host = HealthConnectHost(state.host);
    std::string request =
        method + " " + path + " HTTP/1.1\r\nHost: " + host + ":" + std::to_string(state.port) +
        "\r\nAuthorization: Bearer " + bearerToken +
        "\r\nContent-Length: 0" +
        "\r\nConnection: close\r\n\r\n";
    remainingMs = remainingTimeoutMs();
    if (remainingMs <= 0 ||
        !socket.WaitForWrite(remainingMs / 1000, remainingMs % 1000)) {
        socket.Close();
        return false;
    }
    socket.Write(request.data(), request.size());
    if (socket.Error()) {
        socket.Close();
        return false;
    }
    std::string response;
    std::array<char, 4096> buffer{};
    while (response.size() < 1024 * 1024) {
        remainingMs = remainingTimeoutMs();
        if (remainingMs <= 0 ||
            !socket.WaitForRead(remainingMs / 1000, remainingMs % 1000)) {
            break;
        }
        socket.Read(buffer.data(), buffer.size());
        const size_t read = socket.LastCount();
        if (read == 0) {
            break;
        }
        response.append(buffer.data(), read);
    }
    socket.Close();
    if (response.empty()) {
        return false;
    }
    std::string expected11 = "HTTP/1.1 " + std::to_string(expectedStatus);
    std::string expected10 = "HTTP/1.0 " + std::to_string(expectedStatus);
    const bool statusOk =
        response.rfind(expected11, 0) == 0 || response.rfind(expected10, 0) == 0;
    if (statusOk && responseBody) {
        const size_t bodyStart = response.find("\r\n\r\n");
        *responseBody = bodyStart == std::string::npos
            ? std::string()
            : response.substr(bodyStart + 4);
    }
    return statusOk;
#endif
}

bool HttpHealthOk(
    const TrayAppServerState& state,
    const std::string& bearerToken,
    int timeoutMs = 1000,
    std::string* responseBody = nullptr
) {
    return HttpServerRequestOk(
        state,
        bearerToken,
        "GET",
        "/health",
        200,
        timeoutMs,
        responseBody);
}

bool RequestServerShutdown(const TrayAppServerState& state, const std::string& bearerToken) {
    return HttpServerRequestOk(state, bearerToken, "POST", "/shutdown", 200);
}

std::string ProcessCommandLine(long pid) {
#if defined(__APPLE__)
    std::string command = "ps -p " + std::to_string(pid) + " -o command= 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return {};
    }
    std::string out;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        out += buffer;
    }
    pclose(pipe);
    return ComputerCpp::Trim(out);
#elif defined(__linux__)
    std::ifstream in("/proc/" + std::to_string(pid) + "/cmdline", std::ios::binary);
    if (!in) {
        return {};
    }
    std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::replace(raw.begin(), raw.end(), '\0', ' ');
    return ComputerCpp::Trim(raw);
#else
    (void)pid;
    return {};
#endif
}

bool LooksLikeConfiguredServerProcess(long pid) {
#if defined(_WIN32)
    (void)pid;
    return true;
#else
    const std::string command = ProcessCommandLine(pid);
    return command.find("computer.cpp") != std::string::npos &&
        command.find("app") != std::string::npos &&
        command.find("serve") != std::string::npos &&
        command.find("--configured") != std::string::npos &&
        command.find("--tray-state-file") != std::string::npos;
#endif
}

bool LooksLikeTrayAppServerProcess(const TrayAppServerState& state) {
    std::string command = ProcessCommandLine(state.pid);
    if (command.empty()) {
        return false;
    }
    const bool currentTrayServer = command.find("--tray-state-file") != std::string::npos;
    const bool legacyAdoptedServer = state.appId.empty() &&
        command.find("--auth-token-env COMPUTER_CPP_TRAY_SERVER_TOKEN") != std::string::npos;
    return command.find("computer.cpp") != std::string::npos &&
        command.find("app serve") != std::string::npos &&
        command.find(state.appPath) != std::string::npos &&
        (currentTrayServer || legacyAdoptedServer);
}

std::string AbsolutePathString(const std::string& path) {
    try {
        return std::filesystem::absolute(path).lexically_normal().string();
    } catch (...) {
        return {};
    }
}

std::string ServerConfigSignature(const AppConfig& config) {
    return AppConfigToJson(config, false)["server"].dump();
}

bool ProcessHasExited(long pid, bool reapChild) {
#if defined(__unix__) || defined(__APPLE__)
    if (reapChild) {
        int status = 0;
        pid_t result = ::waitpid(static_cast<pid_t>(pid), &status, WNOHANG);
        if (result == static_cast<pid_t>(pid)) {
            return true;
        }
        if (result < 0 && errno == ECHILD) {
            return !IsProcessAlive(pid);
        }
        return false;
    }
    return !IsProcessAlive(pid);
#else
    (void)reapChild;
    return !IsProcessAlive(pid);
#endif
}

bool WaitForProcessExit(long pid, bool reapChild) {
    for (int i = 0; i < 20; ++i) {
        if (ProcessHasExited(pid, reapChild)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return ProcessHasExited(pid, reapChild);
}

void SignalServerProcess(long pid, wxSignal signal, bool includeChildren) {
#if defined(__unix__) || defined(__APPLE__)
    if (includeChildren) {
        wxKill(pid, signal, nullptr, wxKILL_CHILDREN);
    } else {
        int posixSignal = signal == wxSIGKILL ? SIGKILL : SIGTERM;
        ::kill(static_cast<pid_t>(pid), posixSignal);
    }
#else
    wxKill(pid, signal, nullptr, includeChildren ? wxKILL_CHILDREN : wxKILL_NOCHILDREN);
#endif
}

std::string ServerDisplayUrl(const std::string& host, int port) {
    return "http://" + NormalizeBindHost(host) + ":" + std::to_string(port);
}

bool IsReadableLuaFile(const std::string& path, std::string* error) {
    if (ComputerCpp::Trim(path).empty()) {
        if (error) {
            *error = "Lua app path is required.";
        }
        return false;
    }
    std::filesystem::path appPath(path);
    std::error_code ec;
    if (!std::filesystem::exists(appPath, ec) || ec) {
        if (error) {
            *error = "Lua app path does not exist: " + path;
        }
        return false;
    }
    if (!std::filesystem::is_regular_file(appPath, ec) || ec) {
        if (error) {
            *error = "Lua app path is not a file: " + path;
        }
        return false;
    }
    if (appPath.extension() != ".lua") {
        if (error) {
            *error = "Lua app path must end in .lua: " + path;
        }
        return false;
    }
    std::ifstream in(appPath);
    if (!in.good()) {
        if (error) {
            *error = "Lua app file is not readable: " + path;
        }
        return false;
    }
    return true;
}

std::vector<std::string> SplitTextList(const std::string& raw) {
    std::vector<std::string> out;
    std::string current;
    auto flush = [&] {
        std::string value = ComputerCpp::Trim(current);
        if (!value.empty()) {
            out.push_back(value);
        }
        current.clear();
    };
    for (char ch : raw) {
        if (ch == '\n' || ch == '\r' || ch == ',') {
            flush();
        } else {
            current.push_back(ch);
        }
    }
    flush();
    return out;
}

wxString JoinTextList(const std::vector<std::string>& values) {
    wxString out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out += "\n";
        }
        out += values[i];
    }
    return out;
}

bool CopyTextToClipboard(const std::string& value) {
    if (!wxTheClipboard || !wxTheClipboard->Open()) {
        return false;
    }
    wxTheClipboard->SetData(new wxTextDataObject(value));
    wxTheClipboard->Close();
    return true;
}

bool ResetMacPermissionService(const std::string& service) {
#ifdef __APPLE__
    constexpr const char* bundleId = "org.computercpp.app";
    std::string command = "/usr/bin/tccutil reset " + service + " " + bundleId;
    return wxExecute(command, wxEXEC_SYNC) == 0;
#else
    (void)service;
    return false;
#endif
}

bool RelaunchComputerCpp() {
#ifdef __APPLE__
    std::string bundlePath = ComputerCppBundlePath();
    if (bundlePath.empty()) {
        return false;
    }
    std::string script =
        "while kill -0 " + std::to_string(static_cast<long long>(getpid())) +
        " 2>/dev/null; do sleep 0.2; done; /usr/bin/open -n " + ShellQuote(bundlePath);
    std::string relaunch = "/bin/sh -c " + ShellQuote(script + " >/dev/null 2>&1 &");
    return wxExecute(relaunch, wxEXEC_ASYNC) != 0;
#else
    wxString executablePath = wxStandardPaths::Get().GetExecutablePath();
    return wxExecute("\"" + executablePath + "\"", wxEXEC_ASYNC) != 0;
#endif
}

wxString PermissionStatusMessage(const Platform::PermissionStatus& status) {
    wxString message;
    message << "Accessibility: " << (status.accessibility ? "granted" : "missing") << "\n";
    message << "Screen Recording: " << (status.screenCapture ? "granted" : "missing") << "\n";
    return message;
}

std::string BoolString(bool value) {
    return value ? "yes" : "no";
}

std::string PermissionStatusSummary(const Platform::PermissionStatus& status) {
    return "accessibility=" + BoolString(status.accessibility) +
        " screen_capture=" + BoolString(status.screenCapture);
}

std::tm PermissionTraceLocalTime(std::time_t time) {
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    return local;
}

long long PermissionTraceProcessId() {
#ifdef _WIN32
    return static_cast<long long>(_getpid());
#else
    return static_cast<long long>(getpid());
#endif
}

std::string PermissionTraceTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local = PermissionTraceLocalTime(time);

    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

void AppendAppLog(const std::string& category, const std::string& event) {
    try {
        std::filesystem::create_directories(ComputerCpp::AppLogPath().parent_path());
        std::ofstream log(ComputerCpp::AppLogPath(), std::ios::app);
        log << PermissionTraceTimestamp() << " computer.cpp " << category
            << " pid=" << PermissionTraceProcessId()
            << " event=" << event << "\n";
    } catch (...) {
    }
}

void AppendPermissionTrace(const std::string& event) {
    AppendAppLog("permissions", event);
}

bool ResetPermissionsAndRelaunch(wxString* errorMessage = nullptr) {
    bool allReset = ResetMacPermissionService("All");
    bool accessibilityReset = ResetMacPermissionService("Accessibility");
    bool screenCaptureReset = ResetMacPermissionService("ScreenCapture");
    if (!allReset && (!accessibilityReset || !screenCaptureReset)) {
        wxString message;
        message << "Permission reset did not complete.\n\n";
        message << "All reset: " << (allReset ? "ok" : "failed") << "\n";
        message << "Accessibility reset: " << (accessibilityReset ? "ok" : "failed") << "\n";
        message << "Screen Recording reset: " << (screenCaptureReset ? "ok" : "failed");
        if (errorMessage) {
            *errorMessage = message;
        } else {
            wxMessageBox(message, "ComputerCpp", wxOK | wxICON_ERROR);
        }
        return false;
    }

    if (!RelaunchComputerCpp()) {
        wxString message = "Permissions were reset, but ComputerCpp could not schedule its restart.";
        if (errorMessage) {
            *errorMessage = message;
        } else {
            wxMessageBox(message, "ComputerCpp", wxOK | wxICON_ERROR);
        }
        return false;
    }

    wxTheApp->CallAfter([] {
        wxExit();
    });
    return true;
}

void WaitForDaemonStopped(const std::string& session) {
    for (int i = 0; i < 50; ++i) {
        if (!IsDaemonReady(session)) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void PresentPermissionDialog(wxDialog* dialog) {
    if (!dialog) {
        return;
    }
    dialog->Show();
    Platform::ActivateAgentApp();
    dialog->Raise();
    dialog->SetFocus();
    dialog->RequestUserAttention();
}

void PresentSettingsDialog(wxDialog* dialog) {
    if (!dialog) {
        return;
    }
    dialog->Show();
    dialog->Iconize(false);
    dialog->Restore();
    Platform::ActivateAgentApp();
    dialog->Raise();
    dialog->SetFocus();
    dialog->RequestUserAttention();
    dialog->CallAfter([dialog] {
        if (!dialog) {
            return;
        }
        dialog->Show();
        dialog->Iconize(false);
        dialog->Restore();
        Platform::ActivateAgentApp();
        dialog->Raise();
        dialog->SetFocus();
        dialog->RequestUserAttention();
    });
}

struct LlmSettingsDialogCallbacks {
    std::function<void()> showPermissions;
    std::function<void()> configSaved;
};

class LlmSettingsDialog : public wxDialog {
    enum class SettingsPage : size_t {
        Profiles,
        Providers,
        Server,
        Recording,
        Advanced,
        Count
    };

    enum class StatusKind {
        Info,
        Success,
        Error
    };

    inline static constexpr std::array<
        const char*,
        static_cast<size_t>(SettingsPage::Count)>
        kSettingsPageLabels = {
            "Model Profiles",
            "AI Providers",
            "Local Server",
            "Recording",
            "Advanced"
        };

    static constexpr size_t PageIndex(SettingsPage page) {
        return static_cast<size_t>(page);
    }

public:
    explicit LlmSettingsDialog(LlmSettingsDialogCallbacks callbacks = {})
        : wxDialog(
              nullptr,
              wxID_ANY,
              "ComputerCpp Settings",
              wxDefaultPosition,
              wxDefaultSize,
              wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxSTAY_ON_TOP),
          callbacks_(std::move(callbacks)) {
        const wxSize initialSize = FromDIP(wxSize(1180, 760));
        const wxSize minimumSize = FromDIP(wxSize(960, 620));
        SetClientSize(initialSize);
        SetMinClientSize(minimumSize);

        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* content = new wxBoxSizer(wxHORIZONTAL);

        auto* navigationPanel = new wxPanel(this, wxID_ANY);
        auto* navigationSizer = new wxBoxSizer(wxVERTICAL);
        auto* navigationTitle = new wxStaticText(
            navigationPanel, wxID_ANY, "Configuration");
        wxFont navigationTitleFont = navigationTitle->GetFont();
        navigationTitleFont.SetPointSize(
            navigationTitleFont.GetPointSize() + 1);
        navigationTitleFont.SetWeight(wxFONTWEIGHT_BOLD);
        navigationTitle->SetFont(navigationTitleFont);
        navigationSizer->Add(
            navigationTitle, 0, wxLEFT | wxRIGHT | wxTOP, 16);
        auto* navigationHelp = new wxStaticText(
            navigationPanel, wxID_ANY, "Choose a settings area");
        navigationHelp->SetForegroundColour(
            wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        navigationSizer->Add(
            navigationHelp, 0, wxLEFT | wxRIGHT | wxTOP, 16);

        auto* navigationCard = new wxStaticBoxSizer(
            wxVERTICAL, navigationPanel, wxString());
        wxWindow* navigationCardParent = navigationCard->GetStaticBox();
        navigation_ = new wxListBox(
            navigationCardParent,
            wxID_ANY,
            wxDefaultPosition,
            wxDefaultSize,
            0,
            nullptr,
            wxLB_SINGLE | wxBORDER_NONE);
        for (const char* label : kSettingsPageLabels) {
            navigation_->Append(label);
        }
        wxFont navigationFont = navigation_->GetFont();
        navigationFont.SetPointSize(navigationFont.GetPointSize() + 1);
        navigation_->SetFont(navigationFont);
        int widestNavigationLabel = 0;
        for (unsigned int i = 0; i < navigation_->GetCount(); ++i) {
            widestNavigationLabel = std::max(
                widestNavigationLabel,
                navigation_->GetTextExtent(
                    navigation_->GetString(i)).GetWidth());
        }
        navigation_->SetMinSize(wxSize(
            std::max(
                FromDIP(184),
                widestNavigationLabel + FromDIP(40)),
            -1));
        navigationCard->Add(navigation_, 1, wxALL | wxEXPAND, 8);
        navigationSizer->Add(
            navigationCard, 1, wxALL | wxEXPAND, 12);
        navigationPanel->SetSizer(navigationSizer);
        content->Add(navigationPanel, 0, wxEXPAND);
        content->Add(
            new wxStaticLine(
                this,
                wxID_ANY,
                wxDefaultPosition,
                wxDefaultSize,
                wxLI_VERTICAL),
            0,
            wxTOP | wxBOTTOM | wxEXPAND,
            12);

        auto* contentColumn = new wxBoxSizer(wxVERTICAL);
        pages_ = new wxSimplebook(this, wxID_ANY);
        BuildProfilesPage();
        BuildProvidersPage();
        BuildServerPage();
        BuildRecordingPage();
        BuildConfigPage();
        wxASSERT(
            pages_->GetPageCount() ==
            PageIndex(SettingsPage::Count));
        contentColumn->Add(pages_, 1, wxEXPAND);

        statusMessage_ = new wxStaticText(this, wxID_ANY, "");
        statusMessage_->SetForegroundColour(
            wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        contentColumn->Add(
            statusMessage_, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 20);
        content->Add(contentColumn, 1, wxEXPAND);
        root->Add(content, 1, wxEXPAND);

        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        dirtyState_ = new wxStaticText(this, wxID_ANY, "");
        save_ = new wxButton(this, wxID_SAVE, "Save Changes");
        discard_ = new wxButton(this, wxID_ANY, "Discard");
        close_ = new wxButton(this, wxID_CANCEL, "Close");
        cleanSaveFont_ = save_->GetFont();
        dirtySaveFont_ = cleanSaveFont_;
        dirtySaveFont_.SetWeight(wxFONTWEIGHT_BOLD);
        buttons->Add(dirtyState_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
        buttons->Add(discard_, 0, wxRIGHT, 8);
        buttons->Add(save_, 0, wxRIGHT, 8);
        buttons->AddStretchSpacer();
        buttons->Add(close_, 0);
        root->Add(buttons, 0, wxALL | wxEXPAND, 16);

        SetSizer(root);
        SetEscapeId(wxID_CANCEL);
        navigation_->SetSelection(0);
        pages_->SetSelection(0);
        LoadConfig();
        CentreOnScreen();

        navigation_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) {
            const int selection = navigation_->GetSelection();
            if (selection != wxNOT_FOUND) {
                pages_->SetSelection(static_cast<size_t>(selection));
                if (static_cast<size_t>(selection) ==
                    PageIndex(SettingsPage::Recording)) {
                    RefreshRecordingPermissionStatus();
                }
            }
        });
        save_->Bind(wxEVT_BUTTON, &LlmSettingsDialog::OnSave, this);
        discard_->Bind(wxEVT_BUTTON, &LlmSettingsDialog::OnDiscard, this);
        close_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); });
        Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& event) {
            if (event.GetActive() &&
                navigation_->GetSelection() == static_cast<int>(
                    PageIndex(SettingsPage::Recording))) {
                RefreshRecordingPermissionStatus();
            }
            event.Skip();
        });
        Bind(wxEVT_CLOSE_WINDOW, &LlmSettingsDialog::OnCloseWindow, this);
    }

private:
    static constexpr const char* kLocalBaseUrl = "http://127.0.0.1:8000/v1";
    static constexpr const char* kOpenRouterBaseUrl = "https://openrouter.ai/api/v1";

    template <typename T>
    static std::vector<std::string> SortedNames(const std::map<std::string, T>& items) {
        std::vector<std::string> names;
        names.reserve(items.size());
        for (const auto& [name, _] : items) {
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    static wxString OptionalLongText(const std::optional<long>& value) {
        return value.has_value() ? wxString::Format("%ld", *value) : wxString();
    }

    static wxString OptionalDoubleText(const std::optional<double>& value) {
        if (!value.has_value()) {
            return "";
        }
        std::ostringstream out;
        out << *value;
        return out.str();
    }

    static std::string FieldValue(const wxTextCtrl* field) {
        return ComputerCpp::Trim(field->GetValue().ToStdString());
    }

    static std::string MaskApiKey(const std::string& value) {
        std::string key = ComputerCpp::Trim(value);
        if (key.empty()) {
            return {};
        }
        if (key.size() <= 8) {
            return std::string(key.size(), '*');
        }
        if (key.size() <= 16) {
            return key.substr(0, 3) + "***" + key.substr(key.size() - 3);
        }
        return key.substr(0, 4) + "***" + key.substr(key.size() - 4);
    }

    static std::string StripThinkBlocks(std::string text) {
        while (true) {
            std::string lowered = Lowercase(text);
            auto begin = lowered.find("<think>");
            if (begin == std::string::npos) {
                break;
            }
            auto end = lowered.find("</think>", begin);
            if (end == std::string::npos) {
                text.erase(begin);
                break;
            }
            text.erase(begin, end + 8 - begin);
        }
        std::string lowered = Lowercase(text);
        auto dangling = lowered.find("</think>");
        if (dangling != std::string::npos) {
            text.erase(dangling, 8);
        }
        return Trim(text);
    }

    static bool IsOkInferenceTestReply(const std::string& content) {
        std::string normalized = Lowercase(StripThinkBlocks(content));
        return normalized == "ok" || normalized == "\"ok\"" || normalized == "ok.";
    }

    static std::string UniqueName(const std::map<std::string, LlmProfileConfig>& items, const std::string& base) {
        if (!items.contains(base)) {
            return base;
        }
        for (int i = 2; i < 1000; ++i) {
            std::string candidate = base + "-" + std::to_string(i);
            if (!items.contains(candidate)) {
                return candidate;
            }
        }
        return base + "-new";
    }

    static std::string UniqueName(const std::map<std::string, LlmProviderConfig>& items, const std::string& base) {
        if (!items.contains(base)) {
            return base;
        }
        for (int i = 2; i < 1000; ++i) {
            std::string candidate = base + "-" + std::to_string(i);
            if (!items.contains(candidate)) {
                return candidate;
            }
        }
        return base + "-new";
    }

    static std::string UniqueName(const std::map<std::string, ServerAppConfig>& items, const std::string& base) {
        if (!items.contains(base)) {
            return base;
        }
        for (int i = 2; i < 1000; ++i) {
            std::string candidate = base + "-" + std::to_string(i);
            if (!items.contains(candidate)) {
                return candidate;
            }
        }
        return base + "-new";
    }

    static wxString OptionalIntText(const std::optional<int>& value) {
        return value.has_value() ? wxString::Format("%d", *value) : wxString();
    }

    static std::optional<int> ParsePortField(const wxTextCtrl* field, const std::string& label, std::string* error) {
        std::string value = FieldValue(field);
        if (value.empty()) {
            return std::nullopt;
        }
        int64_t parsed = 0;
        auto* begin = value.data();
        auto* end = begin + value.size();
        auto [ptr, ec] = std::from_chars(begin, end, parsed);
        if (ec != std::errc{} || ptr != end || parsed <= 0 || parsed > 65535) {
            if (error) {
                *error = label + " must be a port number from 1 to 65535.";
            }
            return std::nullopt;
        }
        return static_cast<int>(parsed);
    }

    wxTextCtrl* AddTextField(wxWindow* parent, wxFlexGridSizer* grid, const wxString& label, long style = 0, int minHeight = -1) {
        grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, 8);
        auto* field = new wxTextCtrl(parent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, style);
        if (minHeight > 0) {
            field->SetMinSize(wxSize(-1, minHeight));
        }
        grid->Add(field, 1, wxEXPAND | wxBOTTOM, 8);
        return field;
    }

    wxPanel* AddSettingsPage() {
        auto* page = new wxPanel(pages_, wxID_ANY);
        pages_->AddPage(page, "");
        return page;
    }

    void AddPageHeader(
        wxWindow* parent,
        wxBoxSizer* root,
        const wxString& title,
        const wxString& subtitle) {
        auto* heading = new wxStaticText(parent, wxID_ANY, title);
        wxFont headingFont = heading->GetFont();
        headingFont.SetPointSize(headingFont.GetPointSize() + 5);
        headingFont.SetWeight(wxFONTWEIGHT_BOLD);
        heading->SetFont(headingFont);
        root->Add(heading, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 22);

        auto* description = new wxStaticText(parent, wxID_ANY, subtitle);
        description->SetForegroundColour(
            wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        root->AddSpacer(6);
        root->Add(description, 0, wxLEFT | wxRIGHT | wxEXPAND, 22);
    }

    wxStaticText* AddHelperText(
        wxWindow* parent,
        wxSizer* sizer,
        const wxString& text,
        int border = 0) {
        auto* helper = new wxStaticText(parent, wxID_ANY, text);
        helper->SetForegroundColour(
            wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        sizer->Add(
            helper,
            0,
            (border > 0 ? wxLEFT | wxRIGHT : 0) | wxBOTTOM | wxEXPAND,
            border > 0 ? border : 6);
        return helper;
    }

    wxStaticBoxSizer* AddSectionCard(
        wxWindow* parent,
        wxBoxSizer* root,
        const wxString& title,
        const wxString& description = wxString(),
        int cardProportion = 0) {
        auto* heading = new wxStaticText(parent, wxID_ANY, title);
        wxFont headingFont = heading->GetFont();
        headingFont.SetPointSize(headingFont.GetPointSize() + 1);
        headingFont.SetWeight(wxFONTWEIGHT_BOLD);
        heading->SetFont(headingFont);
        root->Add(heading, 0, wxLEFT | wxRIGHT | wxEXPAND, 2);

        if (!description.empty()) {
            auto* helper = new wxStaticText(
                parent, wxID_ANY, description);
            helper->SetForegroundColour(
                wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
            root->Add(helper, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 2);
        }

        auto* card = new wxStaticBoxSizer(
            wxVERTICAL, parent, wxString());
        root->Add(card, cardProportion, wxTOP | wxEXPAND, 8);
        return card;
    }

    wxScrolledWindow* AddScrollableDetailPane(wxWindow* parent, wxBoxSizer* root) {
        auto* pane = new wxScrolledWindow(
            parent,
            wxID_ANY,
            wxDefaultPosition,
            wxDefaultSize,
            wxHSCROLL | wxVSCROLL | wxTAB_TRAVERSAL);
        pane->SetScrollRate(12, 12);
        root->Add(pane, 1, wxTOP | wxRIGHT | wxBOTTOM | wxEXPAND, 14);
        return pane;
    }

    void FinishScrollableDetailPane(wxScrolledWindow* pane, wxSizer* sizer) {
        pane->SetSizer(sizer);
        pane->FitInside();
    }

    void AddProfileListButtons(wxWindow* parent, wxBoxSizer* column) {
        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        auto* add = new wxButton(parent, wxID_ANY, "Add Profile");
        auto* remove = new wxButton(parent, wxID_ANY, "Delete...");
        buttons->Add(add, 1, wxRIGHT, 6);
        buttons->Add(remove, 1);
        column->Add(buttons, 0, wxTOP | wxEXPAND, 8);
        add->Bind(wxEVT_BUTTON, &LlmSettingsDialog::OnAddProfile, this);
        remove->Bind(wxEVT_BUTTON, &LlmSettingsDialog::OnDeleteProfile, this);
    }

    void BuildProfilesPage() {
        auto* page = AddSettingsPage();
        auto* pageRoot = new wxBoxSizer(wxVERTICAL);
        AddPageHeader(
            page,
            pageRoot,
            "Model Profiles",
            "Create reusable model configurations and choose which one is active.");
        auto* root = new wxBoxSizer(wxHORIZONTAL);

        auto* listSection = new wxBoxSizer(wxVERTICAL);
        auto* listColumn = AddSectionCard(
            page,
            listSection,
            "Saved profiles",
            "Select a profile to edit",
            1);
        wxWindow* listCardParent = listColumn->GetStaticBox();
        profileList_ = new wxListBox(
            listCardParent,
            wxID_ANY,
            wxDefaultPosition,
            wxSize(190, -1));
        listColumn->Add(profileList_, 1, wxEXPAND);
        AddProfileListButtons(listCardParent, listColumn);
        root->Add(listSection, 0, wxEXPAND | wxRIGHT, 18);

        auto* detailPane = AddScrollableDetailPane(page, root);
        auto* detail = new wxBoxSizer(wxVERTICAL);
        auto* modelBox = AddSectionCard(
            detailPane,
            detail,
            "Model setup",
            "The provider, model, and sampling defaults used by this profile.");
        wxWindow* modelCardParent = modelBox->GetStaticBox();
        auto* grid = new wxFlexGridSizer(2, 8, 10);
        grid->AddGrowableCol(1, 1);
        profileName_ = AddTextField(modelCardParent, grid, "Profile name");
        grid->Add(new wxStaticText(modelCardParent, wxID_ANY, "Provider"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, 8);
        profileProvider_ = new wxChoice(modelCardParent, wxID_ANY);
        grid->Add(profileProvider_, 1, wxEXPAND | wxBOTTOM, 8);
        profileModel_ = AddTextField(modelCardParent, grid, "Model");
        temperature_ = AddTextField(modelCardParent, grid, "Temperature");
        temperature_->SetHint("Provider default (0-2)");
        modelBox->Add(grid, 0, wxALL | wxEXPAND, 12);

        auto* profileActions = new wxBoxSizer(wxHORIZONTAL);
        activeProfileText_ = new wxStaticText(detailPane, wxID_ANY, "");
        setActiveProfile_ = new wxButton(detailPane, wxID_ANY, "Set Active");
        testProfile_ = new wxButton(detailPane, wxID_ANY, "Test Profile");
        profileActions->Add(activeProfileText_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
        profileActions->Add(setActiveProfile_, 0, wxRIGHT, 8);
        profileActions->Add(testProfile_, 0);
        detail->Add(profileActions, 0, wxTOP | wxBOTTOM | wxEXPAND, 12);

        auto* advanced = new wxCollapsiblePane(
            detailPane,
            wxID_ANY,
            "Advanced profile settings",
            wxDefaultPosition,
            wxDefaultSize,
            wxCP_NO_TLW_RESIZE);
        auto* advancedRoot = new wxBoxSizer(wxVERTICAL);
        auto* advancedGrid = new wxFlexGridSizer(2, 8, 10);
        advancedGrid->AddGrowableCol(1, 1);
        topP_ = AddTextField(advanced->GetPane(), advancedGrid, "Top P");
        topP_->SetHint("Provider default (0-1)");
        maxTokens_ = AddTextField(
            advanced->GetPane(), advancedGrid, "Maximum output tokens");
        maxTokens_->SetHint("Provider default");
        timeoutMs_ = AddTextField(
            advanced->GetPane(), advancedGrid, "Request timeout (ms)");
        profileParams_ = AddTextField(
            advanced->GetPane(),
            advancedGrid,
            "Extra parameters (JSON)",
            wxTE_MULTILINE,
            72);
        openRouterProvider_ = AddTextField(
            advanced->GetPane(),
            advancedGrid,
            "OpenRouter routing (JSON)",
            wxTE_MULTILINE,
            72);
        advancedRoot->Add(advancedGrid, 1, wxALL | wxEXPAND, 10);
        advanced->GetPane()->SetSizer(advancedRoot);
        detail->Add(advanced, 0, wxEXPAND);

        FinishScrollableDetailPane(detailPane, detail);
        pageRoot->Add(root, 1, wxALL | wxEXPAND, 22);
        page->SetSizer(pageRoot);

        profileList_->Bind(wxEVT_LISTBOX, &LlmSettingsDialog::OnProfileSelected, this);
        setActiveProfile_->Bind(wxEVT_BUTTON, &LlmSettingsDialog::OnSetActiveProfile, this);
        testProfile_->Bind(wxEVT_BUTTON, &LlmSettingsDialog::OnTestProfile, this);
        BindDirty(profileName_);
        BindDirty(profileProvider_);
        BindDirty(profileModel_);
        BindDirty(temperature_);
        BindDirty(topP_);
        BindDirty(maxTokens_);
        BindDirty(timeoutMs_);
        BindDirty(profileParams_);
        BindDirty(openRouterProvider_);
    }

    void BuildProvidersPage() {
        auto* page = AddSettingsPage();
        auto* pageRoot = new wxBoxSizer(wxVERTICAL);
        AddPageHeader(
            page,
            pageRoot,
            "AI Providers",
            "Configure the endpoints and credentials used to reach inference services.");
        auto* root = new wxBoxSizer(wxHORIZONTAL);

        auto* listSection = new wxBoxSizer(wxVERTICAL);
        auto* listColumn = AddSectionCard(
            page,
            listSection,
            "Configured providers",
            "Select a provider to edit",
            1);
        wxWindow* listCardParent = listColumn->GetStaticBox();
        providerList_ = new wxListBox(
            listCardParent,
            wxID_ANY,
            wxDefaultPosition,
            wxSize(190, -1));
        listColumn->Add(providerList_, 1, wxEXPAND);
        auto* listButtons = new wxBoxSizer(wxHORIZONTAL);
        auto* add = new wxButton(listCardParent, wxID_ANY, "Add Provider");
        auto* remove = new wxButton(listCardParent, wxID_ANY, "Delete...");
        listButtons->Add(add, 1, wxRIGHT, 6);
        listButtons->Add(remove, 1);
        listColumn->Add(listButtons, 0, wxTOP | wxEXPAND, 8);
        root->Add(listSection, 0, wxEXPAND | wxRIGHT, 18);

        auto* detailPane = AddScrollableDetailPane(page, root);
        auto* detail = new wxBoxSizer(wxVERTICAL);
        auto* connectionBox = AddSectionCard(
            detailPane,
            detail,
            "Endpoint",
            "Where ComputerCpp sends inference requests.");
        wxWindow* connectionCardParent = connectionBox->GetStaticBox();
        auto* grid = new wxFlexGridSizer(2, 8, 10);
        grid->AddGrowableCol(1, 1);
        providerName_ = AddTextField(connectionCardParent, grid, "Provider name");
        grid->Add(new wxStaticText(connectionCardParent, wxID_ANY, "API format"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, 8);
        providerType_ = new wxChoice(connectionCardParent, wxID_ANY);
        providerType_->Append("OpenRouter");
        providerType_->Append("OpenAI-compatible");
        grid->Add(providerType_, 1, wxEXPAND | wxBOTTOM, 8);
        baseUrl_ = AddTextField(connectionCardParent, grid, "Base URL");
        connectionBox->Add(grid, 0, wxALL | wxEXPAND, 12);
        detail->AddSpacer(18);

        auto* authenticationBox = AddSectionCard(
            detailPane,
            detail,
            "Authentication",
            "Choose whether this provider requires an API key.");
        wxWindow* authenticationCardParent = authenticationBox->GetStaticBox();
        providerNoApiKey_ = new wxRadioButton(
            authenticationCardParent,
            wxID_ANY,
            "No API key",
            wxDefaultPosition,
            wxDefaultSize,
            wxRB_GROUP);
        providerUseApiKey_ = new wxRadioButton(
            authenticationCardParent, wxID_ANY, "Use API key");
        authenticationBox->Add(providerNoApiKey_, 0, wxALL, 12);
        authenticationBox->Add(providerUseApiKey_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
        auto* apiKeyGrid = new wxFlexGridSizer(2, 8, 10);
        apiKeyGrid->AddGrowableCol(1, 1);
        apiKey_ = AddTextField(
            authenticationCardParent,
            apiKeyGrid,
            "API key",
            wxTE_PASSWORD);
        authenticationBox->Add(apiKeyGrid, 0, wxLEFT | wxRIGHT | wxEXPAND, 12);
        AddHelperText(
            authenticationCardParent,
            authenticationBox,
            "The key is stored in config.toml with owner-only file permissions.",
            12);

        FinishScrollableDetailPane(detailPane, detail);
        pageRoot->Add(root, 1, wxALL | wxEXPAND, 22);
        page->SetSizer(pageRoot);

        providerList_->Bind(wxEVT_LISTBOX, &LlmSettingsDialog::OnProviderSelected, this);
        providerType_->Bind(wxEVT_CHOICE, &LlmSettingsDialog::OnProviderTypeChanged, this);
        apiKey_->Bind(wxEVT_TEXT, &LlmSettingsDialog::OnApiKeyChanged, this);
        providerNoApiKey_->Bind(
            wxEVT_RADIOBUTTON, &LlmSettingsDialog::OnApiKeyModeChanged, this);
        providerUseApiKey_->Bind(
            wxEVT_RADIOBUTTON, &LlmSettingsDialog::OnApiKeyModeChanged, this);
        BindDirty(providerName_);
        BindDirty(baseUrl_);
        add->Bind(wxEVT_BUTTON, &LlmSettingsDialog::OnAddProvider, this);
        remove->Bind(wxEVT_BUTTON, &LlmSettingsDialog::OnDeleteProvider, this);
    }

    void BuildServerPage() {
        auto* page = AddSettingsPage();
        auto* pageRoot = new wxBoxSizer(wxVERTICAL);
        AddPageHeader(
            page,
            pageRoot,
            "Local Server",
            "Control how local clients connect and which app definitions they can use.");
        auto* root = new wxBoxSizer(wxHORIZONTAL);

        auto* listSection = new wxBoxSizer(wxVERTICAL);
        auto* listColumn = AddSectionCard(
            page,
            listSection,
            "Configured apps",
            "Select an app to edit",
            1);
        wxWindow* listCardParent = listColumn->GetStaticBox();
        serverAppList_ = new wxListBox(
            listCardParent,
            wxID_ANY,
            wxDefaultPosition,
            wxSize(200, -1));
        listColumn->Add(serverAppList_, 1, wxEXPAND);
        auto* listButtons = new wxBoxSizer(wxHORIZONTAL);
        auto* add = new wxButton(listCardParent, wxID_ANY, "Add App");
        auto* remove = new wxButton(listCardParent, wxID_ANY, "Delete...");
        listButtons->Add(add, 1, wxRIGHT, 6);
        listButtons->Add(remove, 1);
        listColumn->Add(listButtons, 0, wxTOP | wxEXPAND, 8);
        root->Add(listSection, 0, wxEXPAND | wxRIGHT, 18);

        auto* detailPane = AddScrollableDetailPane(page, root);
        auto* detail = new wxBoxSizer(wxVERTICAL);

        auto* serverBox = AddSectionCard(
            detailPane,
            detail,
            "Server endpoint",
            "The local address apps use to reach ComputerCpp.");
        wxWindow* serverCardParent = serverBox->GetStaticBox();
        auto* serverGrid = new wxFlexGridSizer(2, 8, 10);
        serverGrid->AddGrowableCol(1, 1);
        serverHost_ = AddTextField(serverCardParent, serverGrid, "Host");
        serverPort_ = AddTextField(serverCardParent, serverGrid, "Port");
        serverUrl_ = AddTextField(serverCardParent, serverGrid, "Server URL");
        serverUrl_->SetEditable(false);
        serverBox->Add(serverGrid, 0, wxALL | wxEXPAND, 12);
        auto* urlButtons = new wxBoxSizer(wxHORIZONTAL);
        auto* copyUrl = new wxButton(serverCardParent, wxID_ANY, "Copy URL");
        urlButtons->AddStretchSpacer();
        urlButtons->Add(copyUrl, 0);
        serverBox->Add(urlButtons, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
        detail->AddSpacer(18);

        auto* tokenBox = AddSectionCard(
            detailPane,
            detail,
            "Authentication",
            "A private token required by clients connecting to this server.");
        wxWindow* tokenCardParent = tokenBox->GetStaticBox();
        auto* tokenGrid = new wxFlexGridSizer(2, 8, 10);
        tokenGrid->AddGrowableCol(1, 1);
        serverAuthToken_ = AddTextField(
            tokenCardParent, tokenGrid, "Bearer token", wxTE_PASSWORD);
        tokenBox->Add(tokenGrid, 0, wxALL | wxEXPAND, 12);
        AddHelperText(
            tokenCardParent,
            tokenBox,
            "Apps must send this token when connecting to the server.",
            12);
        auto* tokenButtons = new wxBoxSizer(wxHORIZONTAL);
        regenerateServerToken_ = new wxButton(
            tokenCardParent, wxID_ANY, "Regenerate Token");
        auto* copyToken = new wxButton(
            tokenCardParent, wxID_ANY, "Copy Token");
        tokenButtons->AddStretchSpacer();
        tokenButtons->Add(copyToken, 0, wxRIGHT, 8);
        tokenButtons->Add(regenerateServerToken_, 0);
        tokenBox->Add(tokenButtons, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
        detail->AddSpacer(18);

        auto* originsBox = AddSectionCard(
            detailPane,
            detail,
            "Browser access",
            "Limit which web origins may connect to the local server.");
        wxWindow* originsCardParent = originsBox->GetStaticBox();
        serverAllowedOrigins_ = new wxTextCtrl(
            originsCardParent,
            wxID_ANY,
            "",
            wxDefaultPosition,
            wxSize(-1, 72),
            wxTE_MULTILINE);
        originsBox->Add(serverAllowedOrigins_, 0, wxALL | wxEXPAND, 12);
        AddHelperText(
            originsCardParent,
            originsBox,
            "Enter one browser origin per line. Leave blank to disable browser access.",
            12);
        detail->AddSpacer(18);

        auto* appBox = AddSectionCard(
            detailPane,
            detail,
            "App configuration",
            "Identify the app and choose the Lua definition it exposes.");
        wxWindow* appCardParent = appBox->GetStaticBox();
        auto* appGrid = new wxFlexGridSizer(2, 8, 10);
        appGrid->AddGrowableCol(1, 1);
        serverAppName_ = AddTextField(appCardParent, appGrid, "App ID");
        serverAppDisplayName_ = AddTextField(
            appCardParent, appGrid, "Display name");
        serverAppPath_ = AddTextField(appCardParent, appGrid, "Lua file");
        appBox->Add(appGrid, 0, wxALL | wxEXPAND, 12);
        auto* appButtons = new wxBoxSizer(wxHORIZONTAL);
        browseServerApp_ = new wxButton(
            appCardParent, wxID_ANY, "Choose...");
        appButtons->Add(browseServerApp_, 0);
        appButtons->AddStretchSpacer();
        appBox->Add(appButtons, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);

        FinishScrollableDetailPane(detailPane, detail);
        pageRoot->Add(root, 1, wxALL | wxEXPAND, 22);
        page->SetSizer(pageRoot);

        serverAppList_->Bind(wxEVT_LISTBOX, &LlmSettingsDialog::OnServerAppSelected, this);
        add->Bind(wxEVT_BUTTON, &LlmSettingsDialog::OnAddServerApp, this);
        remove->Bind(wxEVT_BUTTON, &LlmSettingsDialog::OnDeleteServerApp, this);
        browseServerApp_->Bind(wxEVT_BUTTON, &LlmSettingsDialog::OnBrowseServerApp, this);
        regenerateServerToken_->Bind(wxEVT_BUTTON, &LlmSettingsDialog::OnRegenerateServerToken, this);
        copyToken->Bind(wxEVT_BUTTON, &LlmSettingsDialog::OnCopyServerTokenFromSettings, this);
        copyUrl->Bind(wxEVT_BUTTON, &LlmSettingsDialog::OnCopyServerUrl, this);

        serverHost_->Bind(wxEVT_TEXT, &LlmSettingsDialog::OnServerEndpointChanged, this);
        serverPort_->Bind(wxEVT_TEXT, &LlmSettingsDialog::OnServerEndpointChanged, this);
        BindDirty(serverAuthToken_);
        BindDirty(serverAllowedOrigins_);
        BindDirty(serverAppName_);
        BindDirty(serverAppDisplayName_);
        BindDirty(serverAppPath_);
    }

    void BuildConfigPage() {
        auto* page = AddSettingsPage();
        auto* root = new wxBoxSizer(wxVERTICAL);
        AddPageHeader(
            page,
            root,
            "Advanced",
            "Inspect and manage ComputerCpp's configuration file.");
        auto* content = new wxBoxSizer(wxVERTICAL);
        auto* box = AddSectionCard(
            page,
            content,
            "Configuration file",
            "Open or reload the source of truth for these settings.");
        wxWindow* cardParent = box->GetStaticBox();
        AddHelperText(
            cardParent,
            box,
            "Providers, profiles, server settings, and recording preferences are stored here.",
            12);
        auto* grid = new wxFlexGridSizer(2, 8, 10);
        grid->AddGrowableCol(1, 1);
        configPath_ = AddTextField(cardParent, grid, "Location");
        configPath_->SetEditable(false);
        box->Add(grid, 0, wxLEFT | wxRIGHT | wxEXPAND, 12);
        auto* actions = new wxBoxSizer(wxHORIZONTAL);
        auto* copyPath = new wxButton(cardParent, wxID_ANY, "Copy Path");
        openConfig_ = new wxButton(cardParent, wxID_ANY, "Open Config");
        reload_ = new wxButton(cardParent, wxID_ANY, "Reload from Disk");
        actions->Add(copyPath, 0, wxRIGHT, 8);
        actions->Add(openConfig_, 0, wxRIGHT, 8);
        actions->Add(reload_, 0);
        box->Add(actions, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
        root->Add(content, 0, wxALL | wxEXPAND, 22);
        root->AddStretchSpacer();
        page->SetSizer(root);

        copyPath->Bind(wxEVT_BUTTON, &LlmSettingsDialog::OnCopyConfigPath, this);
        openConfig_->Bind(wxEVT_BUTTON, &LlmSettingsDialog::OnOpenConfig, this);
        reload_->Bind(wxEVT_BUTTON, &LlmSettingsDialog::OnReload, this);
    }

    void BuildRecordingPage() {
        auto* page = AddSettingsPage();
        auto* root = new wxBoxSizer(wxVERTICAL);
        AddPageHeader(
            page,
            root,
            "Command Recording",
            "Capture desktop activity while a top-level app command runs.");

        auto* content = new wxBoxSizer(wxVERTICAL);
        auto* box = AddSectionCard(
            page,
            content,
            "Recording behavior",
            "Choose whether top-level app commands create a desktop recording.");
        wxWindow* behaviorCardParent = box->GetStaticBox();
        recordingEnabled_ = new wxCheckBox(
            behaviorCardParent,
            wxID_ANY,
            "Record desktop activity");
        box->Add(recordingEnabled_, 0, wxALL | wxEXPAND, 12);
        AddHelperText(
            behaviorCardParent,
            box,
            "Creates one video for each top-level app command.",
            12);

#ifdef __APPLE__
        auto* permissionRow = new wxBoxSizer(wxHORIZONTAL);
        recordingPermissionStatus_ = new wxStaticText(
            behaviorCardParent, wxID_ANY, "");
        manageRecordingPermissions_ = new wxButton(
            behaviorCardParent, wxID_ANY, "Manage Permissions");
        permissionRow->Add(
            recordingPermissionStatus_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
        permissionRow->Add(manageRecordingPermissions_, 0);
        box->Add(permissionRow, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
#endif

        auto* warning = new wxStaticText(
            behaviorCardParent,
            wxID_ANY,
            "Privacy: recordings can include anything visible on your desktop, including "
            "other apps and notifications. Audio is never recorded.");
        warning->Wrap(FromDIP(720));
        box->Add(warning, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);

        content->AddSpacer(18);

        auto* storageBox = AddSectionCard(
            page,
            content,
            "Recording storage",
            "Review where recordings are kept and how long they remain available.");
        wxWindow* storageCardParent = storageBox->GetStaticBox();
        auto* pathGrid = new wxFlexGridSizer(2, 8, 10);
        pathGrid->AddGrowableCol(1, 1);
        recordingPath_ = AddTextField(
            storageCardParent, pathGrid, "Recording folder");
        recordingPath_->SetEditable(false);
        storageBox->Add(pathGrid, 0, wxALL | wxEXPAND, 12);

        openRecordings_ = new wxButton(
            storageCardParent, wxID_ANY, "Open Folder");
        recordingRetentionText_ = new wxStaticText(
            storageCardParent, wxID_ANY, "");
        auto* storageActions = new wxBoxSizer(wxHORIZONTAL);
        storageActions->Add(
            recordingRetentionText_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
        storageActions->Add(openRecordings_, 0);
        storageBox->Add(
            storageActions, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
        content->AddSpacer(18);

        auto* formatBox = AddSectionCard(
            page,
            content,
            "Output format",
            "Recording settings are fixed for consistent playback and debugging.");
        wxWindow* formatCardParent = formatBox->GetStaticBox();
        auto* details = new wxStaticText(
            formatCardParent,
            wxID_ANY,
            "H.264 MP4  |  15 fps  |  Up to 1920 px  |  Cursor included  |  No audio");
        details->SetMinSize(wxSize(-1, FromDIP(24)));
        formatBox->Add(details, 0, wxALL | wxEXPAND, 12);
        formatBox->SetMinSize(wxSize(-1, FromDIP(64)));
        root->Add(content, 0, wxALL | wxEXPAND, 22);
        root->AddStretchSpacer();
        page->SetSizer(root);

        BindDirty(recordingEnabled_);
        openRecordings_->Bind(wxEVT_BUTTON, &LlmSettingsDialog::OnOpenRecordings, this);
#ifdef __APPLE__
        manageRecordingPermissions_->Bind(
            wxEVT_BUTTON,
            [this](wxCommandEvent&) {
                if (callbacks_.showPermissions) {
                    callbacks_.showPermissions();
                }
            });
#endif
    }

    void SetStatus(
        const std::string& message,
        StatusKind kind = StatusKind::Info) {
        if (!statusMessage_) {
            return;
        }
        statusMessage_->SetLabel(wxString::FromUTF8(message));
        statusMessage_->SetForegroundColour(
            kind == StatusKind::Error
                ? wxColour(205, 67, 67)
                : kind == StatusKind::Success
                    ? wxColour(52, 150, 75)
                    : wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        statusMessage_->Wrap(std::max(
            FromDIP(300),
            GetClientSize().GetWidth() - FromDIP(240)));
        Layout();
    }

    void ShowResultDialog(const std::string& title, const std::string& message, long iconStyle) {
        wxMessageDialog dialog(this, message, title, wxOK | iconStyle);
        dialog.ShowModal();
    }

    void RefreshDirtyUi() {
        SetTitle(dirty_ ? "ComputerCpp Settings *" : "ComputerCpp Settings");
        if (dirtyState_) {
            if (loadFailed_) {
                dirtyState_->SetLabel("Configuration unavailable");
                dirtyState_->SetForegroundColour(wxColour(205, 67, 67));
            } else if (dirty_) {
                dirtyState_->SetLabel("Unsaved changes");
                dirtyState_->SetForegroundColour(wxColour(190, 110, 30));
            } else {
                dirtyState_->SetLabel("No unsaved changes");
                dirtyState_->SetForegroundColour(wxColour(52, 150, 75));
            }
        }
        if (save_) {
            save_->Enable(dirty_ && !loadFailed_);
            save_->SetLabel("Save Changes");
            save_->SetFont(dirty_ ? dirtySaveFont_ : cleanSaveFont_);
            save_->SetToolTip(loadFailed_
                ? "Reload a valid configuration before saving"
                : dirty_
                    ? "Write staged changes to config.toml"
                    : "No unsaved changes");
            if (dirty_ && !loadFailed_) {
                save_->SetDefault();
            }
        }
        if (discard_) {
            discard_->Enable(dirty_);
        }
        if (!dirty_ && close_) {
            close_->SetDefault();
        }
        Layout();
    }

    void SetDirty(
        bool dirty,
        const std::string& message = {},
        StatusKind kind = StatusKind::Info) {
        dirty_ = dirty;
        RefreshDirtyUi();
        if (!message.empty()) {
            SetStatus(message, kind);
        }
    }

    void MarkDirty(const std::string& message = {}) {
        if (loading_) {
            return;
        }
        SetDirty(true, message);
    }

    void BindDirty(wxTextCtrl* field) {
        field->Bind(wxEVT_TEXT, &LlmSettingsDialog::OnControlChanged, this);
    }

    void BindDirty(wxChoice* field) {
        field->Bind(wxEVT_CHOICE, &LlmSettingsDialog::OnControlChanged, this);
    }

    void BindDirty(wxCheckBox* field) {
        field->Bind(wxEVT_CHECKBOX, &LlmSettingsDialog::OnControlChanged, this);
    }

    void LoadConfig(const std::string& successMessage = {}) {
        std::string error;
        std::vector<std::string> warnings;
        config_ = LoadAppConfig(&error, &warnings);
        loadFailed_ = !error.empty();
        std::string loadStatus = ComputerCpp::Join(warnings, "\n");
        if (loadFailed_) {
            config_ = DefaultAppConfig();
            loadStatus = "Could not load config.toml: " + error +
                ". Open the file or reload after correcting it.";
        }
        if (config_.providers.empty()) {
            config_.providers = DefaultAppConfig().providers;
        }
        if (config_.profiles.empty()) {
            config_.profiles = DefaultAppConfig().profiles;
        }
        if (!loadFailed_ && ComputerCpp::EnsureServerAuthToken(config_)) {
            std::string saveError;
            SaveAppConfig(config_, &saveError);
        }
        ResetProviderAuthDrafts();
        if (configPath_) {
            configPath_->ChangeValue(ConfigPath().string());
        }
        if (recordingEnabled_) {
            recordingEnabled_->SetValue(config_.recording.enabled);
        }
        if (recordingPath_) {
            recordingPath_->ChangeValue(RecordingDir().string());
        }
        if (recordingRetentionText_) {
            recordingRetentionText_->SetLabel(
                config_.recording.retentionDays < 0
                    ? wxString("Recordings are kept until you delete them.")
                    : config_.recording.retentionDays == 0
                        ? wxString("Recordings are removed as soon as they finish.")
                        : wxString::Format(
                              "Recordings are removed after %d days.",
                              config_.recording.retentionDays));
        }
        RefreshRecordingPermissionStatus();
        PopulateProviderLists(FirstProviderName());
        PopulateProviderChoices();
        PopulateProfileList(config_.defaultProfile.empty() ? FirstProfileName() : config_.defaultProfile);
        LoadServerFields();
        PopulateServerAppList(FirstServerAppName());
        for (size_t i = 0; i < pages_->GetPageCount(); ++i) {
            if (i != PageIndex(SettingsPage::Advanced)) {
                pages_->GetPage(i)->Enable(!loadFailed_);
            }
        }
        if (loadFailed_) {
            const int advanced = static_cast<int>(
                PageIndex(SettingsPage::Advanced));
            navigation_->SetSelection(advanced);
            pages_->SetSelection(PageIndex(SettingsPage::Advanced));
        }
        SetDirty(false);
        const bool showingSuccess = loadStatus.empty() && !successMessage.empty();
        SetStatus(
            loadStatus.empty() ? successMessage : loadStatus,
            loadFailed_
                ? StatusKind::Error
                : showingSuccess
                    ? StatusKind::Success
                    : StatusKind::Info);
    }

    std::string FirstProfileName() const {
        return config_.profiles.empty() ? std::string() : config_.profiles.begin()->first;
    }

    std::string FirstProviderName() const {
        return config_.providers.empty() ? std::string() : config_.providers.begin()->first;
    }

    std::string FirstServerAppName() const {
        return config_.server.apps.empty() ? std::string() : config_.server.apps.begin()->first;
    }

    std::string SelectedString(wxListBox* list) const {
        int selection = list->GetSelection();
        if (selection == wxNOT_FOUND) {
            return {};
        }
        return list->GetString(selection).ToStdString();
    }

    std::string SelectedProfileName() const {
        const int selection = profileList_->GetSelection();
        if (selection == wxNOT_FOUND ||
            static_cast<size_t>(selection) >= profileListNames_.size()) {
            return {};
        }
        return profileListNames_[static_cast<size_t>(selection)];
    }

    void SelectProfileListValue(const std::string& value) {
        auto found = std::find(profileListNames_.begin(), profileListNames_.end(), value);
        if (found != profileListNames_.end()) {
            profileList_->SetSelection(
                static_cast<int>(std::distance(profileListNames_.begin(), found)));
        }
    }

    void SelectListValue(wxListBox* list, const std::string& value) {
        int index = list->FindString(value);
        if (index != wxNOT_FOUND) {
            list->SetSelection(index);
        }
    }

    void PopulateProfileList(const std::string& selected) {
        loading_ = true;
        profileList_->Clear();
        profileListNames_.clear();
        for (const auto& name : SortedNames(config_.profiles)) {
            profileListNames_.push_back(name);
            profileList_->Append(
                name == config_.defaultProfile
                    ? wxString::FromUTF8(name) + "  (Active)"
                    : wxString::FromUTF8(name));
        }
        std::string target = selected.empty() ? FirstProfileName() : selected;
        SelectProfileListValue(target);
        loading_ = false;
        LoadProfileFields(target);
    }

    void PopulateProviderLists(const std::string& selected) {
        loading_ = true;
        providerList_->Clear();
        for (const auto& name : SortedNames(config_.providers)) {
            providerList_->Append(name);
        }
        std::string target = selected.empty() ? FirstProviderName() : selected;
        SelectListValue(providerList_, target);
        loading_ = false;
        LoadProviderFields(target);
    }

    void PopulateProviderChoices() {
        std::string selected = profileProvider_->GetStringSelection().ToStdString();
        profileProvider_->Clear();
        for (const auto& name : SortedNames(config_.providers)) {
            profileProvider_->Append(name);
        }
        if (!selected.empty()) {
            profileProvider_->SetStringSelection(selected);
        }
    }

    void RefreshServerUrlPreview() {
        if (!serverUrl_ || !serverHost_ || !serverPort_) {
            return;
        }
        std::string host = NormalizeBindHost(FieldValue(serverHost_));
        std::string rawPort = FieldValue(serverPort_);
        int port = 0;
        auto [ptr, ec] = std::from_chars(
            rawPort.data(), rawPort.data() + rawPort.size(), port);
        if (ec != std::errc{} || ptr != rawPort.data() + rawPort.size() ||
            port <= 0 || port > 65535) {
            serverUrlValid_ = false;
            serverUrl_->ChangeValue("Enter a valid host and port");
            return;
        }
        serverUrlValid_ = true;
        serverUrl_->ChangeValue(ServerDisplayUrl(host, port));
    }

    void RefreshRecordingPermissionStatus() {
        if (!recordingPermissionStatus_) {
            return;
        }
        const bool granted = Platform::CheckPermissions(false).screenCapture;
        recordingPermissionStatus_->SetLabel(
            granted
                ? "Screen Recording permission granted"
                : "Screen Recording permission required");
        recordingPermissionStatus_->SetForegroundColour(
            granted ? wxColour(52, 150, 75) : wxColour(190, 110, 30));
        if (manageRecordingPermissions_) {
            manageRecordingPermissions_->SetLabel(
                granted ? "Manage Permissions" : "Grant Permission");
        }
        Layout();
    }

    void LoadServerFields() {
        loading_ = true;
        serverHost_->ChangeValue(config_.server.host);
        serverPort_->ChangeValue(wxString::Format("%d", config_.server.port));
        serverAuthToken_->ChangeValue(config_.server.authToken);
        serverAllowedOrigins_->ChangeValue(JoinTextList(config_.server.allowedOrigins));
        RefreshServerUrlPreview();
        loading_ = false;
    }

    void PopulateServerAppList(const std::string& selected) {
        loading_ = true;
        serverAppList_->Clear();
        for (const auto& name : SortedNames(config_.server.apps)) {
            const ServerAppConfig& app = config_.server.apps[name];
            serverAppList_->Append(app.displayName.empty() ? name : app.displayName);
        }
        loading_ = false;

        std::string target = selected.empty() ? FirstServerAppName() : selected;
        int index = 0;
        for (const auto& name : SortedNames(config_.server.apps)) {
            if (name == target) {
                serverAppList_->SetSelection(index);
                break;
            }
            ++index;
        }
        LoadServerAppFields(target);
    }

    void LoadServerAppFields(const std::string& name) {
        loading_ = true;
        activeServerApp_ = name;
        if (name.empty() || !config_.server.apps.contains(name)) {
            serverAppName_->ChangeValue("");
            serverAppDisplayName_->ChangeValue("");
            serverAppPath_->ChangeValue("");
            EnableServerAppFields(false);
            loading_ = false;
            return;
        }
        const ServerAppConfig& app = config_.server.apps[name];
        serverAppName_->ChangeValue(app.name);
        serverAppDisplayName_->ChangeValue(app.displayName);
        serverAppPath_->ChangeValue(app.path);
        EnableServerAppFields(true);
        loading_ = false;
    }

    void EnableServerAppFields(bool enabled) {
        for (auto* field : {serverAppName_, serverAppDisplayName_, serverAppPath_}) {
            if (field) {
                field->Enable(enabled);
            }
        }
        if (browseServerApp_) {
            browseServerApp_->Enable(enabled);
        }
    }

    void LoadProfileFields(const std::string& name) {
        if (name.empty() || !config_.profiles.contains(name)) {
            activeProfile_.clear();
            return;
        }
        loading_ = true;
        activeProfile_ = name;
        const LlmProfileConfig& profile = config_.profiles[name];
        profileName_->ChangeValue(profile.name);
        PopulateProviderChoices();
        if (!profileProvider_->SetStringSelection(profile.provider) && profileProvider_->GetCount() > 0) {
            profileProvider_->SetSelection(0);
        }
        profileModel_->ChangeValue(profile.model);
        temperature_->ChangeValue(OptionalDoubleText(profile.temperature));
        topP_->ChangeValue(OptionalDoubleText(profile.topP));
        maxTokens_->ChangeValue(OptionalLongText(profile.maxOutputTokens));
        timeoutMs_->ChangeValue(OptionalLongText(profile.timeoutMs));
        json extraParams = profile.params.is_object() ? profile.params : json::object();
        extraParams.erase("temperature");
        extraParams.erase("top_p");
        extraParams.erase("max_output_tokens");
        profileParams_->ChangeValue(extraParams.empty() ? "" : extraParams.dump(2));
        openRouterProvider_->ChangeValue(profile.openRouterProvider.empty() ? "" : profile.openRouterProvider.dump(2));
        activeProfileText_->SetLabel(config_.defaultProfile == profile.name
            ? "Active profile"
            : "Current active profile: " + config_.defaultProfile);
        setActiveProfile_->Show(config_.defaultProfile != profile.name);
        setActiveProfile_->GetParent()->Layout();
        loading_ = false;
    }

    void LoadProviderFields(const std::string& name) {
        if (name.empty() || !config_.providers.contains(name)) {
            activeProvider_.clear();
            return;
        }
        loading_ = true;
        activeProvider_ = name;
        const LlmProviderConfig& provider = config_.providers[name];
        const auto draft = providerApiKeyDrafts_.find(name);
        const auto mode = providerUseApiKeyModes_.find(name);
        const std::string apiKey = draft != providerApiKeyDrafts_.end()
            ? draft->second
            : provider.apiKey;
        const bool useApiKey = mode != providerUseApiKeyModes_.end()
            ? mode->second
            : !provider.apiKey.empty();
        providerName_->ChangeValue(provider.name);
        providerType_->SetSelection(provider.type == "openrouter" ? 0 : 1);
        baseUrl_->ChangeValue(provider.baseUrl);
        apiKey_->ChangeValue(apiKey);
        providerNoApiKey_->SetValue(!useApiKey);
        providerUseApiKey_->SetValue(useApiKey);
        apiKey_->Enable(useApiKey);
        apiKey_->SetToolTip(apiKey.empty()
            ? "Paste an API key to save it in config.toml"
            : "Current key: " + MaskApiKey(apiKey) + ". Paste over it to replace it.");
        loading_ = false;
    }

    void ResetProviderAuthDrafts() {
        providerApiKeyDrafts_.clear();
        providerUseApiKeyModes_.clear();
        for (const auto& [name, provider] : config_.providers) {
            providerApiKeyDrafts_[name] = provider.apiKey;
            providerUseApiKeyModes_[name] = !provider.apiKey.empty();
        }
    }

    std::string ProviderTypeValue() const {
        return providerType_->GetSelection() == 0 ? "openrouter" : "openai-compatible";
    }

    bool ParseJsonObjectField(const wxTextCtrl* field, const std::string& label, json& out) {
        std::string value = FieldValue(field);
        if (value.empty()) {
            out = json::object();
            return true;
        }
        json parsed = json::parse(value, nullptr, false);
        if (!parsed.is_object()) {
            SetStatus(label + " must be a JSON object.", StatusKind::Error);
            return false;
        }
        out = parsed;
        return true;
    }

    bool ApplyOptionalParam(LlmProfileConfig& profile, const wxTextCtrl* field, const std::string& key, std::string* error) {
        std::string value = FieldValue(field);
        if (value.empty()) {
            return true;
        }
        return SetProfileDefaultParam(profile, key, value, error);
    }

    bool FlushProfileFields() {
        if (activeProfile_.empty()) {
            return true;
        }
        std::string name = FieldValue(profileName_);
        if (name.empty()) {
            SetStatus("Profile name is required.", StatusKind::Error);
            return false;
        }
        if (name != activeProfile_ && config_.profiles.contains(name)) {
            SetStatus("Profile '" + name + "' already exists.", StatusKind::Error);
            return false;
        }
        std::string provider = profileProvider_->GetStringSelection().ToStdString();
        if (provider.empty() || !config_.providers.contains(provider)) {
            SetStatus(
                "Choose a provider for profile '" + name + "'.",
                StatusKind::Error);
            return false;
        }

        json extraParams;
        json openRouterRouting;
        if (!ParseJsonObjectField(profileParams_, "Extra Params JSON", extraParams) ||
            !ParseJsonObjectField(openRouterProvider_, "OpenRouter Routing JSON", openRouterRouting)) {
            return false;
        }

        LlmProfileConfig profile;
        profile.name = name;
        profile.provider = provider;
        profile.model = FieldValue(profileModel_);
        profile.params = extraParams;
        profile.openRouterProvider = openRouterRouting;
        std::string error;
        if (!ApplyOptionalParam(profile, temperature_, "temperature", &error) ||
            !ApplyOptionalParam(profile, topP_, "top_p", &error) ||
            !ApplyOptionalParam(profile, maxTokens_, "max_output_tokens", &error) ||
            !ApplyOptionalParam(profile, timeoutMs_, "timeout_ms", &error)) {
            SetStatus(error, StatusKind::Error);
            return false;
        }

        if (name != activeProfile_) {
            config_.profiles.erase(activeProfile_);
            if (config_.defaultProfile == activeProfile_) {
                config_.defaultProfile = name;
            }
        }
        config_.profiles[name] = profile;
        if (config_.defaultProfile.empty()) {
            config_.defaultProfile = name;
        }
        activeProfile_ = name;
        return true;
    }

    bool FlushProviderFields() {
        if (activeProvider_.empty()) {
            return true;
        }
        std::string name = FieldValue(providerName_);
        if (name.empty()) {
            SetStatus("Provider name is required.", StatusKind::Error);
            return false;
        }
        if (name != activeProvider_ && config_.providers.contains(name)) {
            SetStatus("Provider '" + name + "' already exists.", StatusKind::Error);
            return false;
        }
        const std::string newApiKey = FieldValue(apiKey_);
        if (providerUseApiKey_->GetValue() && newApiKey.empty()) {
            SetStatus(
                "Enter an API key or select No API key.",
                StatusKind::Error);
            apiKey_->SetFocus();
            return false;
        }

        std::string error;
        std::string oldProviderName = activeProvider_;
        if (!SetProviderConfig(config_, name, ProviderTypeValue(), FieldValue(baseUrl_), &error)) {
            SetStatus(error, StatusKind::Error);
            return false;
        }
        LlmProviderConfig& provider = config_.providers[name];
        if (name != oldProviderName) {
            config_.providers.erase(oldProviderName);
            for (auto& [_, profile] : config_.profiles) {
                if (profile.provider == oldProviderName) {
                    profile.provider = name;
                }
            }
        }

        const bool useApiKey = providerUseApiKey_->GetValue();
        if (name != oldProviderName) {
            providerApiKeyDrafts_.erase(oldProviderName);
            providerUseApiKeyModes_.erase(oldProviderName);
        }
        providerApiKeyDrafts_[name] = newApiKey;
        providerUseApiKeyModes_[name] = useApiKey;
        if (!useApiKey) {
            provider.apiKey.clear();
        } else {
            provider.apiKey = newApiKey;
        }

        activeProvider_ = name;
        if (name != oldProviderName && profileProvider_->GetStringSelection().ToStdString() == oldProviderName) {
            PopulateProviderChoices();
            profileProvider_->SetStringSelection(name);
        }
        return true;
    }

    bool FlushServerFields() {
        std::string host = NormalizeBindHost(FieldValue(serverHost_));
        if (host.empty()) {
            host = "127.0.0.1";
        }
        std::string portError;
        auto port = ParsePortField(serverPort_, "Port", &portError);
        if (!port.has_value()) {
            SetStatus(
                portError.empty() ? "Port is required." : portError,
                StatusKind::Error);
            return false;
        }

        config_.server.host = host;
        config_.server.port = *port;
        config_.server.authToken = FieldValue(serverAuthToken_);
        if (config_.server.authToken.empty()) {
            config_.server.authToken = GenerateServerAuthToken();
            serverAuthToken_->ChangeValue(config_.server.authToken);
        }
        config_.server.allowedOrigins = SplitTextList(serverAllowedOrigins_->GetValue().ToStdString());

        if (!FlushServerAppFields()) {
            return false;
        }
        for (const auto& [name, app] : config_.server.apps) {
            if (!IsValidServerAppName(name)) {
                SetStatus(
                    "Server app stable name must match "
                    "[A-Za-z0-9][A-Za-z0-9._-]*.",
                    StatusKind::Error);
                return false;
            }
            std::string error;
            if (!IsReadableLuaFile(app.path, &error)) {
                SetStatus(
                    "Server app '" + name + "': " + error,
                    StatusKind::Error);
                return false;
            }
        }
        return true;
    }

    bool FlushServerAppFields() {
        if (activeServerApp_.empty()) {
            return true;
        }
        std::string name = FieldValue(serverAppName_);
        if (!IsValidServerAppName(name)) {
            SetStatus(
                "Server app stable name must match "
                "[A-Za-z0-9][A-Za-z0-9._-]*.",
                StatusKind::Error);
            return false;
        }
        if (name != activeServerApp_ && config_.server.apps.contains(name)) {
            SetStatus(
                "Server app '" + name + "' already exists.",
                StatusKind::Error);
            return false;
        }
        std::string displayName = FieldValue(serverAppDisplayName_);
        if (displayName.empty()) {
            displayName = name;
        }
        std::string path = FieldValue(serverAppPath_);
        std::string fileError;
        if (!IsReadableLuaFile(path, &fileError)) {
            SetStatus(fileError, StatusKind::Error);
            return false;
        }
        ServerAppConfig app;
        app.name = name;
        app.displayName = displayName;
        app.path = path;
        if (name != activeServerApp_) {
            config_.server.apps.erase(activeServerApp_);
        }
        config_.server.apps[name] = app;
        activeServerApp_ = name;
        return true;
    }

    bool SaveChanges(bool showStatus) {
        if (loadFailed_) {
            SetStatus(
                "Reload a valid configuration before saving changes.",
                StatusKind::Error);
            return false;
        }
        if (!FlushAllFields()) {
            return false;
        }
        std::string error;
        if (!SaveAppConfig(config_, &error)) {
            SetStatus(error, StatusKind::Error);
            return false;
        }
        ResetProviderAuthDrafts();
        RefreshAfterMutation(activeProfile_, activeProvider_);
        std::string message = "Saved changes.";
        SetDirty(
            false,
            showStatus ? message : "",
            StatusKind::Success);
        if (callbacks_.configSaved) {
            callbacks_.configSaved();
        }
        return true;
    }

    bool FlushAllFields() {
        if (recordingEnabled_) {
            config_.recording.enabled = recordingEnabled_->GetValue();
        }
        return FlushProviderFields() && FlushProfileFields() && FlushServerFields();
    }

    void RefreshAfterMutation(const std::string& profile, const std::string& provider) {
        PopulateProviderLists(provider.empty() ? activeProvider_ : provider);
        PopulateProviderChoices();
        PopulateProfileList(profile.empty() ? activeProfile_ : profile);
        LoadServerFields();
        PopulateServerAppList(activeServerApp_);
    }

    void OnProfileSelected(wxCommandEvent&) {
        if (loading_) {
            return;
        }
        std::string previous = activeProfile_;
        if (!FlushProfileFields()) {
            loading_ = true;
            SelectProfileListValue(previous);
            loading_ = false;
            return;
        }
        LoadProfileFields(SelectedProfileName());
    }

    void OnProviderSelected(wxCommandEvent&) {
        if (loading_) {
            return;
        }
        std::string previous = activeProvider_;
        if (!FlushProviderFields()) {
            loading_ = true;
            SelectListValue(providerList_, previous);
            loading_ = false;
            return;
        }
        PopulateProviderChoices();
        LoadProviderFields(SelectedString(providerList_));
    }

    void OnServerAppSelected(wxCommandEvent&) {
        if (loading_) {
            return;
        }
        std::string previous = activeServerApp_;
        if (!FlushServerAppFields()) {
            loading_ = true;
            PopulateServerAppList(previous);
            loading_ = false;
            return;
        }
        int selection = serverAppList_->GetSelection();
        std::string selected;
        if (selection != wxNOT_FOUND) {
            auto names = SortedNames(config_.server.apps);
            if (static_cast<size_t>(selection) < names.size()) {
                selected = names[static_cast<size_t>(selection)];
            }
        }
        LoadServerAppFields(selected);
    }

    void OnProviderTypeChanged(wxCommandEvent&) {
        if (loading_) {
            return;
        }
        std::string type = ProviderTypeValue();
        std::string current = FieldValue(baseUrl_);
        if (type == "openrouter" && (current.empty() || current == kLocalBaseUrl)) {
            baseUrl_->ChangeValue(kOpenRouterBaseUrl);
            providerUseApiKey_->SetValue(true);
            providerNoApiKey_->SetValue(false);
            apiKey_->Enable(true);
        } else if (type == "openai-compatible" && (current.empty() || current == kOpenRouterBaseUrl)) {
            baseUrl_->ChangeValue(kLocalBaseUrl);
        }
        MarkDirty();
    }

    void OnApiKeyChanged(wxCommandEvent&) {
        if (loading_) {
            return;
        }
        if (!FieldValue(apiKey_).empty()) {
            providerUseApiKey_->SetValue(true);
            providerNoApiKey_->SetValue(false);
            apiKey_->Enable(true);
            MarkDirty("API key will be saved when you click Save Changes.");
            return;
        }
        MarkDirty();
    }

    void OnApiKeyModeChanged(wxCommandEvent&) {
        if (loading_) {
            return;
        }
        const bool useKey = providerUseApiKey_->GetValue();
        apiKey_->Enable(useKey);
        MarkDirty(
            useKey
                ? "Enter the API key, then save changes."
                : "The saved API key will be removed when you save changes.");
        Layout();
    }

    void OnServerEndpointChanged(wxCommandEvent&) {
        if (loading_) {
            return;
        }
        RefreshServerUrlPreview();
        MarkDirty();
    }

    void OnControlChanged(wxCommandEvent&) {
        MarkDirty();
    }

    void OnAddProfile(wxCommandEvent&) {
        if (!FlushAllFields()) {
            return;
        }
        std::string name = UniqueName(config_.profiles, "profile");
        LlmProfileConfig profile;
        profile.name = name;
        profile.provider = activeProvider_.empty() ? FirstProviderName() : activeProvider_;
        profile.model = profile.provider == "openrouter" ? "openrouter/auto" : "qwen36-27b";
        profile.timeoutMs = 180000;
        config_.profiles[name] = profile;
        RefreshAfterMutation(name, activeProvider_);
        MarkDirty("Created profile '" + name + "'. Unsaved changes.");
    }

    void OnDeleteProfile(wxCommandEvent&) {
        if (activeProfile_.empty() || config_.profiles.size() <= 1) {
            SetStatus("At least one profile is required.", StatusKind::Error);
            return;
        }
        std::string removed = activeProfile_;
        if (wxMessageBox(
                "Delete profile '" + removed + "'?\n\nThis change is not written until you save.",
                "Delete Profile",
                wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
                this) != wxYES) {
            return;
        }
        config_.profiles.erase(removed);
        if (config_.defaultProfile == removed) {
            config_.defaultProfile = FirstProfileName();
        }
        activeProfile_.clear();
        RefreshAfterMutation(config_.defaultProfile, activeProvider_);
        MarkDirty("Deleted profile '" + removed + "'. Unsaved changes.");
    }

    void OnAddProvider(wxCommandEvent&) {
        if (!FlushAllFields()) {
            return;
        }
        std::string name = UniqueName(config_.providers, "provider");
        LlmProviderConfig provider;
        provider.name = name;
        provider.type = "openai-compatible";
        provider.baseUrl = kLocalBaseUrl;
        config_.providers[name] = provider;
        providerApiKeyDrafts_[name] = "";
        providerUseApiKeyModes_[name] = false;
        RefreshAfterMutation(activeProfile_, name);
        MarkDirty("Created provider '" + name + "'. Unsaved changes.");
    }

    void OnDeleteProvider(wxCommandEvent&) {
        if (activeProvider_.empty() || config_.providers.size() <= 1) {
            SetStatus("At least one provider is required.", StatusKind::Error);
            return;
        }
        for (const auto& [name, profile] : config_.profiles) {
            if (profile.provider == activeProvider_) {
                SetStatus(
                    "Provider is used by profile '" + name + "'.",
                    StatusKind::Error);
                return;
            }
        }
        std::string removed = activeProvider_;
        if (wxMessageBox(
                "Delete provider '" + removed + "'?\n\nThis change is not written until you save.",
                "Delete Provider",
                wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
                this) != wxYES) {
            return;
        }
        config_.providers.erase(removed);
        providerApiKeyDrafts_.erase(removed);
        providerUseApiKeyModes_.erase(removed);
        activeProvider_.clear();
        RefreshAfterMutation(activeProfile_, FirstProviderName());
        MarkDirty("Deleted provider '" + removed + "'. Unsaved changes.");
    }

    void OnAddServerApp(wxCommandEvent&) {
        if (!FlushAllFields()) {
            return;
        }
        std::string name = UniqueName(config_.server.apps, "app");
        ServerAppConfig app;
        app.name = name;
        app.displayName = "App";
        config_.server.apps[name] = app;
        activeServerApp_ = name;
        PopulateServerAppList(name);
        MarkDirty("Created server app '" + name + "'. Add a readable .lua path before saving.");
    }

    void OnDeleteServerApp(wxCommandEvent&) {
        if (activeServerApp_.empty()) {
            SetStatus("Choose a server app first.", StatusKind::Error);
            return;
        }
        std::string removed = activeServerApp_;
        if (wxMessageBox(
                "Delete app '" + removed + "'?\n\nThis change is not written until you save.",
                "Delete Server App",
                wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
                this) != wxYES) {
            return;
        }
        config_.server.apps.erase(removed);
        activeServerApp_.clear();
        PopulateServerAppList(FirstServerAppName());
        MarkDirty("Deleted server app '" + removed + "'. Unsaved changes.");
    }

    void OnBrowseServerApp(wxCommandEvent&) {
        wxFileDialog dialog(
            this,
            "Choose Lua app",
            "",
            serverAppPath_ ? serverAppPath_->GetValue() : wxString(),
            "Lua files (*.lua)|*.lua|All files|*",
            wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() != wxID_OK) {
            return;
        }
        serverAppPath_->ChangeValue(dialog.GetPath());
        MarkDirty("Server app path changed. Unsaved changes.");
    }

    void OnRegenerateServerToken(wxCommandEvent&) {
        if (wxMessageBox(
                "Regenerate the server token?\n\nExisting clients will stop connecting after you save the new token.",
                "Regenerate Server Token",
                wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
                this) != wxYES) {
            return;
        }
        serverAuthToken_->ChangeValue(GenerateServerAuthToken());
        MarkDirty("Bearer token regenerated. Save changes before using it.");
    }

    void OnCopyServerTokenFromSettings(wxCommandEvent&) {
        std::string token = FieldValue(serverAuthToken_);
        if (token.empty()) {
            SetStatus("No bearer token to copy.", StatusKind::Error);
            return;
        }
        const bool copied = CopyTextToClipboard(token);
        SetStatus(
            copied ? "Bearer token copied." : "Could not open clipboard.",
            copied ? StatusKind::Success : StatusKind::Error);
    }

    void OnCopyServerUrl(wxCommandEvent&) {
        const std::string url = FieldValue(serverUrl_);
        if (!serverUrlValid_) {
            SetStatus(
                "Enter a valid host and port before copying the URL.",
                StatusKind::Error);
            return;
        }
        const bool copied = CopyTextToClipboard(url);
        SetStatus(
            copied ? "Server URL copied." : "Could not open clipboard.",
            copied ? StatusKind::Success : StatusKind::Error);
    }

    void OnSave(wxCommandEvent&) {
        SaveChanges(true);
    }

    void OnSetActiveProfile(wxCommandEvent&) {
        if (!FlushAllFields()) {
            return;
        }
        if (activeProfile_.empty()) {
            SetStatus("Choose a profile first.", StatusKind::Error);
            return;
        }
        config_.defaultProfile = activeProfile_;
        RefreshAfterMutation(activeProfile_, activeProvider_);
        MarkDirty("Active profile set to '" + activeProfile_ + "'. Unsaved changes.");
    }

    void OnTestProfile(wxCommandEvent&) {
        if (dirty_) {
            SetStatus("Save changes before testing inference.");
            ShowResultDialog(
                "Inference Test",
                "Save changes before testing inference so the endpoint, model, and API key match config.toml.",
                wxICON_INFORMATION);
            return;
        }
        if (!FlushAllFields()) {
            return;
        }
        if (activeProfile_.empty()) {
            SetStatus("Choose a profile first.", StatusKind::Error);
            ShowResultDialog("Inference Test", "Choose a profile first.", wxICON_INFORMATION);
            return;
        }
        json resolved = Inference::ResolveChatConfig({{"profile", activeProfile_}});
        if (!resolved.value("ok", false)) {
            std::string message = resolved.value("error", "invalid config");
            SetStatus(
                "Inference test could not resolve this profile.",
                StatusKind::Error);
            ShowResultDialog("Inference Test Failed", message, wxICON_ERROR);
            return;
        }
        auto resolvedData = resolved.value("data", json::object());
        std::string testedProvider = resolvedData.value("provider", "");
        std::string testedModel = resolvedData.value("model", "");
        std::string testedBaseUrl = resolvedData.value("baseUrl", "");

        SetStatus("Testing " + activeProfile_ + ": " + testedProvider + " " + testedModel + " at " + testedBaseUrl + "...");
        Update();
        wxBusyCursor busy;
        json response = Inference::ChatCompletion({
            {"profile", activeProfile_},
            {"timeoutMs", 30000},
            {"max_output_tokens", 512},
            {"temperature", 0},
            {"messages", json::array({
                {{"role", "system"}, {"content", "You are handling a connection test. Do not explain. Return only the requested text."}},
                {{"role", "user"}, {"content", "/no_think\nReply with exactly OK."}}
            })}
        });
        if (!response.value("ok", false)) {
            std::string code = response.value("code", "error");
            std::string message = response.value("error", "inference test failed");
            SetStatus(
                "Inference test failed. See details dialog.",
                StatusKind::Error);
            ShowResultDialog(
                "Inference Test Failed",
                "Profile: " + activeProfile_ +
                    "\nProvider: " + testedProvider +
                    "\nModel: " + testedModel +
                    "\nEndpoint: " + testedBaseUrl +
                    "\nCode: " + code +
                    "\n\n" + message,
                wxICON_ERROR);
            return;
        }
        auto data = response.value("data", json::object());
        std::string content = data.value("content", "");
        if (!IsOkInferenceTestReply(content)) {
            std::string shownContent = content.empty() ? std::string("<empty>") : content;
            std::string finishReason = data.value("finishReason", "");
            std::string reasoningContent = data.value("reasoningContent", "");
            std::string extra;
            if (!finishReason.empty()) {
                extra += "\nFinish reason: " + finishReason;
            }
            if (content.empty() && !reasoningContent.empty()) {
                extra += "\n\nThe endpoint returned reasoning_content but no visible assistant content. For Qwen-style thinking models, disable thinking in the deployment/template or keep /no_think in the prompt.";
            }
            SetStatus(
                "Inference reached the endpoint, but the reply was not OK.",
                StatusKind::Error);
            ShowResultDialog(
                "Inference Test Reached Endpoint",
                "Profile: " + activeProfile_ +
                    "\nProvider: " + testedProvider +
                    "\nModel: " + testedModel +
                    "\nEndpoint: " + testedBaseUrl +
                    "\n\nExpected reply: OK\nActual reply: " + shownContent +
                    extra +
                    "\n\nCheck the model id and chat-completions compatibility.",
                wxICON_WARNING);
            return;
        }
        SetStatus("Inference OK.", StatusKind::Success);
        ShowResultDialog(
            "Inference Test Passed",
            "Profile: " + activeProfile_ +
                "\nProvider: " + data.value("provider", "") +
                "\nModel: " + data.value("model", "") +
                "\n\nThe endpoint replied OK.",
            wxICON_INFORMATION);
    }

    void OnReload(wxCommandEvent&) {
        if (dirty_) {
            int answer = wxMessageBox(
                "Discard unsaved settings changes?",
                "Unsaved Changes",
                wxYES_NO | wxICON_QUESTION,
                this);
            if (answer != wxYES) {
                return;
            }
        }
        LoadConfig("Reloaded configuration from disk.");
    }

    void OnDiscard(wxCommandEvent&) {
        if (!dirty_) {
            return;
        }
        if (wxMessageBox(
                "Discard all unsaved settings changes?",
                "Discard Changes",
                wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
                this) != wxYES) {
            return;
        }
        LoadConfig("Discarded unsaved changes and reloaded config.toml.");
    }

    void OnCopyConfigPath(wxCommandEvent&) {
        const bool copied = CopyTextToClipboard(ConfigPath().string());
        SetStatus(
            copied ? "Configuration path copied." : "Could not open clipboard.",
            copied ? StatusKind::Success : StatusKind::Error);
    }

    void OnOpenConfig(wxCommandEvent&) {
        if (!wxLaunchDefaultApplication(ConfigPath().string())) {
            SetStatus(
                "Could not open config.toml in the default application.",
                StatusKind::Error);
        } else {
            SetStatus(
                "Opened config.toml in the default application.",
                StatusKind::Success);
        }
    }

    void OnOpenRecordings(wxCommandEvent&) {
        try {
            EnsureDirectory(RecordingDir());
        } catch (const std::exception& ex) {
            ShowResultDialog("Recording Folder", ex.what(), wxICON_ERROR);
            return;
        }
        if (!wxLaunchDefaultApplication(RecordingDir().string())) {
            ShowResultDialog(
                "Recording Folder",
                "Could not open:\n" + RecordingDir().string(),
                wxICON_ERROR);
        } else {
            SetStatus("Opened the recording folder.", StatusKind::Success);
        }
    }

    void OnCloseWindow(wxCloseEvent& event) {
        if (dirty_) {
            int answer = wxMessageBox(
                "Save settings changes before closing?",
                "Unsaved Changes",
                wxYES_NO | wxCANCEL | wxICON_QUESTION,
                this);
            if (answer == wxCANCEL) {
                if (event.CanVeto()) {
                    event.Veto();
                }
                return;
            }
            if (answer == wxYES && !SaveChanges(false)) {
                if (event.CanVeto()) {
                    event.Veto();
                }
                return;
            }
        }
        Destroy();
    }

    AppConfig config_;
    LlmSettingsDialogCallbacks callbacks_;
    bool loading_ = false;
    bool dirty_ = false;
    bool loadFailed_ = false;
    std::string activeProfile_;
    std::string activeProvider_;
    std::string activeServerApp_;

    wxSimplebook* pages_ = nullptr;
    wxListBox* navigation_ = nullptr;
    wxListBox* profileList_ = nullptr;
    std::vector<std::string> profileListNames_;
    wxTextCtrl* profileName_ = nullptr;
    wxChoice* profileProvider_ = nullptr;
    wxTextCtrl* profileModel_ = nullptr;
    wxTextCtrl* temperature_ = nullptr;
    wxTextCtrl* topP_ = nullptr;
    wxTextCtrl* maxTokens_ = nullptr;
    wxTextCtrl* timeoutMs_ = nullptr;
    wxTextCtrl* profileParams_ = nullptr;
    wxTextCtrl* openRouterProvider_ = nullptr;
    wxStaticText* activeProfileText_ = nullptr;
    wxButton* setActiveProfile_ = nullptr;
    wxButton* testProfile_ = nullptr;

    wxListBox* providerList_ = nullptr;
    wxTextCtrl* providerName_ = nullptr;
    wxChoice* providerType_ = nullptr;
    wxTextCtrl* baseUrl_ = nullptr;
    wxTextCtrl* apiKey_ = nullptr;
    wxRadioButton* providerNoApiKey_ = nullptr;
    wxRadioButton* providerUseApiKey_ = nullptr;
    std::map<std::string, std::string> providerApiKeyDrafts_;
    std::map<std::string, bool> providerUseApiKeyModes_;

    wxListBox* serverAppList_ = nullptr;
    wxTextCtrl* serverHost_ = nullptr;
    wxTextCtrl* serverPort_ = nullptr;
    wxTextCtrl* serverUrl_ = nullptr;
    bool serverUrlValid_ = false;
    wxTextCtrl* serverAuthToken_ = nullptr;
    wxTextCtrl* serverAllowedOrigins_ = nullptr;
    wxTextCtrl* serverAppName_ = nullptr;
    wxTextCtrl* serverAppDisplayName_ = nullptr;
    wxTextCtrl* serverAppPath_ = nullptr;
    wxButton* browseServerApp_ = nullptr;
    wxButton* regenerateServerToken_ = nullptr;

    wxTextCtrl* configPath_ = nullptr;
    wxCheckBox* recordingEnabled_ = nullptr;
    wxTextCtrl* recordingPath_ = nullptr;
    wxButton* openRecordings_ = nullptr;
    wxStaticText* recordingPermissionStatus_ = nullptr;
    wxStaticText* recordingRetentionText_ = nullptr;
    wxButton* manageRecordingPermissions_ = nullptr;
    wxStaticText* statusMessage_ = nullptr;
    wxStaticText* dirtyState_ = nullptr;
    wxButton* save_ = nullptr;
    wxButton* discard_ = nullptr;
    wxButton* reload_ = nullptr;
    wxButton* openConfig_ = nullptr;
    wxButton* close_ = nullptr;
    wxFont cleanSaveFont_;
    wxFont dirtySaveFont_;
};

class PermissionSetupDialog : public wxDialog {
public:
    PermissionSetupDialog()
        : wxDialog(nullptr, wxID_ANY, "ComputerCpp Permissions", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxSTAY_ON_TOP),
          timer_(this) {
        AppendPermissionTrace("permission_dialog_constructed");
        auto* root = new wxBoxSizer(wxVERTICAL);
        content_ = new wxScrolledWindow(
            this,
            wxID_ANY,
            wxDefaultPosition,
            wxDefaultSize,
            wxVSCROLL | wxTAB_TRAVERSAL);
        content_->SetScrollRate(0, 12);
        auto* contentRoot = new wxBoxSizer(wxVERTICAL);

        auto* title = new wxStaticText(content_, wxID_ANY, "Permissions");
        wxFont titleFont = title->GetFont();
        titleFont.SetPointSize(titleFont.GetPointSize() + 6);
        titleFont.SetWeight(wxFONTWEIGHT_BOLD);
        title->SetFont(titleFont);
        contentRoot->Add(
            title, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 22);

        auto* subtitle = new wxStaticText(
            content_,
            wxID_ANY,
            "ComputerCpp needs two permissions to observe and control the desktop.");
        subtitle->SetForegroundColour(
            wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        contentRoot->AddSpacer(6);
        contentRoot->Add(
            subtitle, 0, wxLEFT | wxRIGHT | wxEXPAND, 22);

        auto* summaryHeading = new wxStaticText(
            content_, wxID_ANY, "Permission readiness");
        wxFont summaryHeadingFont = summaryHeading->GetFont();
        summaryHeadingFont.SetPointSize(
            summaryHeadingFont.GetPointSize() + 1);
        summaryHeadingFont.SetWeight(wxFONTWEIGHT_BOLD);
        summaryHeading->SetFont(summaryHeadingFont);
        contentRoot->Add(
            summaryHeading,
            0,
            wxLEFT | wxRIGHT | wxTOP | wxEXPAND,
            22);
        auto* summaryBox = new wxStaticBoxSizer(
            wxVERTICAL, content_, wxString());
        summary_ = new wxStaticText(
            summaryBox->GetStaticBox(), wxID_ANY, "");
        wxFont summaryFont = summary_->GetFont();
        summaryFont.SetPointSize(summaryFont.GetPointSize() + 2);
        summaryFont.SetWeight(wxFONTWEIGHT_BOLD);
        summary_->SetFont(summaryFont);
        summary_->Wrap(FromDIP(620));
        summaryBox->Add(summary_, 0, wxALL | wxEXPAND, 14);
        contentRoot->Add(
            summaryBox,
            0,
            wxLEFT | wxRIGHT | wxTOP | wxEXPAND,
            8);

        AddPermissionSection(content_,
                             contentRoot,
                             "1  Accessibility",
                             "Inspect interface state and send mouse and keyboard input.",
                             accessibilityStatus_,
                             accessibilityDetail_,
                             accessibilityRequest_,
                             accessibilityTest_);
        AddPermissionSection(content_,
                             contentRoot,
                             "2  Screen Recording",
                             "Capture screenshots for observation and verification.",
                             screenStatus_,
                             screenDetail_,
                             screenRequest_,
                             screenTest_);

        accessibilityRequest_->SetLabel("Open System Settings");
        accessibilityTest_->SetLabel("Test Access");
        screenRequest_->SetLabel("Open System Settings");
        screenTest_->SetLabel("Test Capture");

        troubleshooting_ = new wxCollapsiblePane(
            content_,
            wxID_ANY,
            "Troubleshooting",
            wxDefaultPosition,
            wxDefaultSize,
            wxCP_NO_TLW_RESIZE);
        auto* troubleshootingSizer = new wxBoxSizer(wxVERTICAL);
        auto* restartHelp = new wxStaticText(
            troubleshooting_->GetPane(),
            wxID_ANY,
            "Restart after changing permissions, or reset them if macOS remains out of sync.");
        restartHelp->SetForegroundColour(
            wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        restartHelp->Wrap(FromDIP(640));
        resetOrRestart_ = new wxButton(
            troubleshooting_->GetPane(),
            wxID_ANY,
            "Restart ComputerCpp");
        troubleshootingSizer->Add(
            restartHelp, 0, wxALL | wxEXPAND, 12);
        troubleshootingSizer->Add(
            resetOrRestart_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
        troubleshooting_->GetPane()->SetSizerAndFit(
            troubleshootingSizer);
        contentRoot->Add(
            troubleshooting_,
            0,
            wxLEFT | wxRIGHT | wxTOP | wxBOTTOM | wxEXPAND,
            22);

        content_->SetSizer(contentRoot);
        content_->FitInside();
        root->Add(content_, 1, wxEXPAND);

        auto* footer = new wxBoxSizer(wxHORIZONTAL);
        runAllChecks_ = new wxButton(this, wxID_ANY, "Run All Checks");
        close_ = new wxButton(this, wxID_CANCEL, "Done");
        footer->AddStretchSpacer();
        footer->Add(runAllChecks_, 0, wxRIGHT, 8);
        footer->Add(close_, 0);
        root->Add(footer, 0, wxALL | wxEXPAND, 22);

        SetEscapeId(wxID_CANCEL);
        SetSizer(root);
        SetClientSize(FromDIP(wxSize(800, 720)));
        SetMinClientSize(FromDIP(wxSize(700, 560)));
        CentreOnScreen();

        accessibilityRequest_->Bind(wxEVT_BUTTON, &PermissionSetupDialog::OnRequestAccessibility, this);
        accessibilityTest_->Bind(wxEVT_BUTTON, &PermissionSetupDialog::OnTestAccessibility, this);
        screenRequest_->Bind(wxEVT_BUTTON, &PermissionSetupDialog::OnRequestScreenRecording, this);
        screenTest_->Bind(wxEVT_BUTTON, &PermissionSetupDialog::OnTestScreenRecording, this);
        resetOrRestart_->Bind(wxEVT_BUTTON, &PermissionSetupDialog::OnResetOrRestart, this);
        troubleshooting_->Bind(
            wxEVT_COLLAPSIBLEPANE_CHANGED,
            [this](wxCollapsiblePaneEvent& event) {
                troubleshooting_->GetPane()->Layout();
                troubleshooting_->InvalidateBestSize();
                content_->GetSizer()->Layout();
                content_->FitInside();
                if (!event.GetCollapsed()) {
                    CallAfter([this] {
                        content_->GetSizer()->Layout();
                        content_->FitInside();
                        content_->Scroll(
                            -1, content_->GetVirtualSize().GetHeight());
                    });
                }
                event.Skip();
            });
        runAllChecks_->Bind(wxEVT_BUTTON, &PermissionSetupDialog::OnRunAllChecks, this);
        close_->Bind(wxEVT_BUTTON, &PermissionSetupDialog::OnCancel, this);
        Bind(wxEVT_TIMER, &PermissionSetupDialog::OnTimer, this);
        Bind(wxEVT_CLOSE_WINDOW, &PermissionSetupDialog::OnClose, this);

        lastStatus_ = Platform::CheckPermissions(false);
        ApplyStatus(lastStatus_);
        timer_.Start(750);
    }

private:
    enum class SettingsHandoff {
        None,
        Accessibility,
        ScreenRecording
    };

    void AddPermissionSection(wxWindow* parent,
                              wxBoxSizer* root,
                              const wxString& title,
                              const wxString& description,
                              wxStaticText*& statusLabel,
                              wxStaticText*& detailLabel,
                              wxButton*& requestButton,
                              wxButton*& testButton) {
        auto* heading = new wxStaticText(parent, wxID_ANY, title);
        wxFont headingFont = heading->GetFont();
        headingFont.SetPointSize(headingFont.GetPointSize() + 1);
        headingFont.SetWeight(wxFONTWEIGHT_BOLD);
        heading->SetFont(headingFont);
        root->Add(
            heading,
            0,
            wxLEFT | wxRIGHT | wxTOP | wxEXPAND,
            22);
        auto* box = new wxStaticBoxSizer(
            wxVERTICAL, parent, wxString());
        wxWindow* cardParent = box->GetStaticBox();

        auto* statusRow = new wxBoxSizer(wxHORIZONTAL);
        auto* descriptionLabel = new wxStaticText(
            cardParent, wxID_ANY, description);
        descriptionLabel->Wrap(FromDIP(410));
        statusLabel = new wxStaticText(cardParent, wxID_ANY, "");
        wxFont statusFont = statusLabel->GetFont();
        statusFont.SetWeight(wxFONTWEIGHT_BOLD);
        statusLabel->SetFont(statusFont);
        statusRow->Add(descriptionLabel, 1, wxRIGHT | wxALIGN_CENTER_VERTICAL, 12);
        statusRow->Add(statusLabel, 0, wxALIGN_TOP);
        box->Add(statusRow, 0, wxALL | wxEXPAND, 14);

        detailLabel = new wxStaticText(cardParent, wxID_ANY, "");
        detailLabel->Wrap(FromDIP(620));
        box->Add(detailLabel, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 14);

        auto* buttonRow = new wxBoxSizer(wxHORIZONTAL);
        requestButton = new wxButton(
            cardParent, wxID_ANY, "Open System Settings");
        testButton = new wxButton(cardParent, wxID_ANY, "Test");
        buttonRow->Add(requestButton, 0);
        buttonRow->AddSpacer(8);
        buttonRow->Add(testButton, 0);
        buttonRow->AddStretchSpacer();
        box->Add(buttonRow, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 14);

        root->Add(
            box,
            0,
            wxLEFT | wxRIGHT | wxTOP | wxEXPAND,
            8);
    }

    void SetPermissionStatus(wxStaticText* label, bool granted) {
        label->SetLabel(granted ? "Granted" : "Missing");
        label->SetForegroundColour(granted ? wxColour(62, 142, 78) : wxColour(190, 80, 60));
    }

    wxString DefaultAccessibilityDetail(bool granted) const {
        return granted
            ? "Granted. Use Test Access to verify the current macOS status."
            : "Open System Settings, enable ComputerCpp under Accessibility, then return here.";
    }

    wxString DefaultScreenDetail(bool granted) const {
        return granted
            ? "Granted. Test Capture verifies that screenshots work."
            : "Open System Settings and enable ComputerCpp under Screen Recording. If it is missing, use + to choose the running app.";
    }

    void ApplyStatus(const Platform::PermissionStatus& status) {
        SetPermissionStatus(accessibilityStatus_, status.accessibility);
        SetPermissionStatus(screenStatus_, status.screenCapture);

        const bool ready = status.accessibility && status.screenCapture;
        summary_->SetLabel(ready
            ? "ComputerCpp is ready — all required permissions are granted."
            : "ComputerCpp needs attention — complete each missing permission below.");
        summary_->SetForegroundColour(
            ready ? wxColour(52, 150, 75) : wxColour(190, 110, 30));
        summary_->Wrap(FromDIP(620));

        accessibilityRequest_->SetLabel(
            status.accessibility
                ? "Open System Settings"
                : "Grant in System Settings");
        accessibilityRequest_->Show(true);
        accessibilityTest_->Show(status.accessibility);
        screenRequest_->SetLabel(
            status.screenCapture
                ? "Open System Settings"
                : "Grant in System Settings");
        screenRequest_->Show(true);
        screenTest_->Show(status.screenCapture);
        resetOrRestart_->SetLabel(ready ? "Restart ComputerCpp" : "Reset Permissions && Restart");

        accessibilityDetail_->SetLabel(accessibilityResult_.empty() ? DefaultAccessibilityDetail(status.accessibility) : accessibilityResult_);
        accessibilityDetail_->Wrap(FromDIP(620));
        screenDetail_->SetLabel(screenResult_.empty() ? DefaultScreenDetail(status.screenCapture) : screenResult_);
        screenDetail_->Wrap(FromDIP(620));

        if (content_) {
            content_->Layout();
            content_->FitInside();
        }
        Layout();
    }

    void BeginSettingsHandoff(SettingsHandoff handoff) {
        settingsHandoff_ = handoff;
        settingsHandoffStarted_ = std::chrono::steady_clock::now();
        Hide();
    }

    bool HandoffPermissionGranted(const Platform::PermissionStatus& status) const {
        switch (settingsHandoff_) {
            case SettingsHandoff::Accessibility:
                return status.accessibility;
            case SettingsHandoff::ScreenRecording:
                return status.screenCapture;
            case SettingsHandoff::None:
                return false;
        }
        return false;
    }

    bool ShouldStayHiddenForSettings(const Platform::PermissionStatus& status) {
        if (settingsHandoff_ == SettingsHandoff::None) {
            return false;
        }
        if (HandoffPermissionGranted(status)) {
            return false;
        }
        auto elapsed = std::chrono::steady_clock::now() - settingsHandoffStarted_;
        if (settingsHandoff_ == SettingsHandoff::Accessibility ||
            settingsHandoff_ == SettingsHandoff::ScreenRecording) {
            return elapsed < std::chrono::minutes(2);
        }
        return false;
    }

    void RefreshPermissionState() {
        Platform::PermissionStatus previous = lastStatus_;
        Platform::PermissionStatus status = Platform::CheckPermissions(false);
        if (previous.accessibility != status.accessibility) {
            accessibilityResult_.clear();
        }
        if (previous.screenCapture != status.screenCapture) {
            screenResult_.clear();
        }
        lastStatus_ = status;
        ApplyStatus(status);
        if (ShouldStayHiddenForSettings(status)) {
            return;
        }
        if (settingsHandoff_ != SettingsHandoff::None) {
            settingsHandoff_ = SettingsHandoff::None;
            PresentPermissionDialog(this);
        }
    }

    void OnTimer(wxTimerEvent&) {
        RefreshPermissionState();
    }

    void OnRequestAccessibility(wxCommandEvent&) {
        AppendPermissionTrace("accessibility_button_clicked before_status=" + PermissionStatusSummary(Platform::CheckPermissions(false)));
        accessibilityResult_ = "Request sent. Use Apple's prompt to open Accessibility settings, enable ComputerCpp, then return here.";
        ApplyStatus(Platform::CheckPermissions(false));
        Platform::RequestAccessibilityPermission();
        AppendPermissionTrace("accessibility_button_after_native_request after_status=" + PermissionStatusSummary(Platform::CheckPermissions(false)));
        BeginSettingsHandoff(SettingsHandoff::Accessibility);
        RefreshPermissionState();
    }

    void OnTestAccessibility(wxCommandEvent&) {
        Platform::PermissionStatus status = Platform::CheckPermissions(false);
        accessibilityResult_ = status.accessibility
            ? "Test passed. macOS reports ComputerCpp as a trusted Accessibility client."
            : "Test failed. Accessibility is still missing.";
        lastStatus_ = status;
        ApplyStatus(status);
    }

    void OnRequestScreenRecording(wxCommandEvent&) {
        AppendPermissionTrace("screen_button_clicked before_status=" + PermissionStatusSummary(Platform::CheckPermissions(false)));
        screenResult_ = "Request sent. If ComputerCpp is still missing, use + in Screen Recording and choose the running ComputerCpp.app.";
        ApplyStatus(Platform::CheckPermissions(false));
        Platform::RequestScreenCapturePermission();
        AppendPermissionTrace("screen_button_after_native_request after_status=" + PermissionStatusSummary(Platform::CheckPermissions(false)));
        BeginSettingsHandoff(SettingsHandoff::ScreenRecording);
        RefreshPermissionState();
    }

    void OnTestScreenRecording(wxCommandEvent&) {
        Platform::PermissionStatus status = Platform::CheckPermissions(false);
        if (!status.screenCapture) {
            screenResult_ = "Test failed. Screen Recording is still missing.";
            lastStatus_ = status;
            ApplyStatus(status);
            return;
        }

        screenResult_ = "Testing screenshot capture...";
        lastStatus_ = status;
        ApplyStatus(status);

        wxBusyCursor busy;
        const std::string path = TemporaryScreenshotPath(
            "computer.cpp-permission-test.png");
        bool ok = Platform::SaveScreenshot(path);
        screenResult_ = ok
            ? "Test passed. Screenshot capture works."
            : "Test failed. Restart ComputerCpp, then test again. If it still fails, reset permissions.";
        RefreshPermissionState();
    }

    void OnRunAllChecks(wxCommandEvent&) {
        Platform::PermissionStatus status = Platform::CheckPermissions(false);
        accessibilityResult_ = status.accessibility
            ? "Test passed. macOS reports ComputerCpp as a trusted Accessibility client."
            : "Test failed. Accessibility is still missing.";

        if (!status.screenCapture) {
            screenResult_ = "Test failed. Screen Recording is still missing.";
            lastStatus_ = status;
            ApplyStatus(status);
            return;
        }

        screenResult_ = "Testing screenshot capture...";
        lastStatus_ = status;
        ApplyStatus(status);
        wxBusyCursor busy;
        const std::string path = TemporaryScreenshotPath(
            "computer.cpp-permission-test.png");
        screenResult_ = Platform::SaveScreenshot(path)
            ? "Test passed. Screenshot capture works."
            : "Test failed. Restart ComputerCpp, then test again. If it still fails, reset permissions.";
        RefreshPermissionState();
    }

    void OnResetOrRestart(wxCommandEvent&) {
        timer_.Stop();
        Platform::PermissionStatus status = Platform::CheckPermissions(false);
        if (status.accessibility && status.screenCapture) {
            AppendPermissionTrace("restart_requested permissions_granted status=" + PermissionStatusSummary(status));
            ApplyStatus(status);
            summary_->SetLabel("Restarting ComputerCpp...");
            Layout();
            if (!RelaunchComputerCpp()) {
                summary_->SetLabel("Could not schedule ComputerCpp restart.");
                timer_.Start(750);
                return;
            }
            wxTheApp->CallAfter([] {
                wxExit();
            });
            return;
        }

        if (wxMessageBox(
                "Reset Accessibility and Screen Recording permissions?\n\nComputerCpp will restart and you will need to grant both permissions again.",
                "Reset Permissions",
                wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
                this) != wxYES) {
            timer_.Start(750);
            return;
        }

        summary_->SetLabel("Resetting permissions and restarting ComputerCpp...");
        AppendPermissionTrace("reset_permissions_requested before_status=" + PermissionStatusSummary(status));
        Layout();
        wxString error;
        if (!ResetPermissionsAndRelaunch(&error)) {
            summary_->SetLabel(error);
            summary_->Wrap(FromDIP(620));
            timer_.Start(750);
            Layout();
        }
    }

    void OnCancel(wxCommandEvent&) {
        Close();
    }

    void OnClose(wxCloseEvent&) {
        timer_.Stop();
        Platform::PermissionStatus status = Platform::CheckPermissions(false);
        AppendPermissionTrace("permission_dialog_closed status=" + PermissionStatusSummary(status));
        Destroy();
    }

    wxScrolledWindow* content_ = nullptr;
    wxStaticText* summary_ = nullptr;
    wxStaticText* accessibilityStatus_ = nullptr;
    wxStaticText* accessibilityDetail_ = nullptr;
    wxStaticText* screenStatus_ = nullptr;
    wxStaticText* screenDetail_ = nullptr;
    wxButton* accessibilityRequest_ = nullptr;
    wxButton* accessibilityTest_ = nullptr;
    wxButton* screenRequest_ = nullptr;
    wxButton* screenTest_ = nullptr;
    wxButton* resetOrRestart_ = nullptr;
    wxButton* runAllChecks_ = nullptr;
    wxButton* close_ = nullptr;
    wxCollapsiblePane* troubleshooting_ = nullptr;
    wxTimer timer_;
    Platform::PermissionStatus lastStatus_;
    wxString accessibilityResult_;
    wxString screenResult_;
    SettingsHandoff settingsHandoff_ = SettingsHandoff::None;
    std::chrono::steady_clock::time_point settingsHandoffStarted_;
};

wxBEGIN_EVENT_TABLE(TrayIcon, wxTaskBarIcon)
    EVT_MENU(ID_PERMISSIONS, TrayIcon::OnPermissions)
    EVT_MENU(ID_SETTINGS, TrayIcon::OnSettings)
    EVT_MENU(ID_RECORDING_TOGGLE, TrayIcon::OnRecordingToggle)
    EVT_MENU(ID_SHOW_LOGS, TrayIcon::OnShowLogs)
    EVT_MENU(ID_CHECK_UPDATES, TrayIcon::OnCheckForUpdates)
    EVT_MENU(ID_START_SERVER, TrayIcon::OnStartServer)
    EVT_MENU(ID_STOP_SERVER, TrayIcon::OnStopServer)
    EVT_MENU(ID_GOBII_CONNECT, TrayIcon::OnGobiiConnect)
    EVT_MENU(ID_GOBII_STATUS, TrayIcon::OnGobiiStatus)
    EVT_MENU(
        ID_GOBII_PAUSE_RESUME,
        TrayIcon::OnGobiiPauseResume)
    EVT_MENU(
        ID_GOBII_DISCONNECT,
        TrayIcon::OnGobiiDisconnect)
    EVT_MENU(ID_GOBII_MANAGE, TrayIcon::OnGobiiManage)
    EVT_MENU(ID_STATE, TrayIcon::OnState)
    EVT_MENU(ID_TEST_SCREENSHOT, TrayIcon::OnTestScreenshot)
    EVT_MENU(ID_TEST_MOUSE, TrayIcon::OnTestMouse)
    EVT_TASKBAR_RIGHT_UP(TrayIcon::OnTaskbarRightUp)
    EVT_END_PROCESS(ID_SERVER_PROCESS, TrayIcon::OnServerProcessEnded)
    EVT_TIMER(ID_SERVER_TIMER, TrayIcon::OnServerTimer)
    EVT_MENU(ID_QUIT, TrayIcon::OnQuit)
wxEND_EVENT_TABLE()

class TrayConfiguredServerController final :
    public ConfiguredServerController {
public:
    explicit TrayConfiguredServerController(TrayIcon& owner)
        : owner_(owner) {}

    bool EnsureRunning(std::string& error) override {
        error.clear();
        if (wxIsMainThread()) {
            error = "configured server startup cannot run on the UI thread";
            return false;
        }
        auto startPromise =
            std::make_shared<std::promise<void>>();
        auto startFuture = startPromise->get_future();
        wxTheApp->CallAfter([
            this,
            startPromise
        ] {
            owner_.StartConfiguredServer();
            startPromise->set_value();
        });
        startFuture.wait();
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(8);
        bool serverRunning = false;
        while (std::chrono::steady_clock::now() < deadline) {
            const ConfiguredServerInfo info = Status();
            if (info.running) {
                serverRunning = true;
                if (ConfiguredServerCatalogReady(info)) {
                    return true;
                }
            }
            if (!info.error.empty()) {
                error = info.error;
                return false;
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100));
        }
        error = serverRunning
            ? "local configured MCP server catalog did not become ready"
            : "local configured MCP server did not become ready";
        return false;
    }

    ConfiguredServerInfo Status() const override {
        if (wxIsMainThread()) {
            ConfiguredServerInfo info;
            info.error =
                "configured server status cannot run on the UI thread";
            return info;
        }
        auto promise =
            std::make_shared<
                std::promise<ConfiguredServerInfo>>();
        auto future = promise->get_future();
        wxTheApp->CallAfter([
            this,
            promise
        ] {
            ConfiguredServerInfo info;
            info.running =
                owner_.server_.status ==
                    TrayIcon::ServerStatus::Running;
            info.host = owner_.server_.host;
            info.port = owner_.server_.port;
            info.bearerToken = owner_.serverAuthToken_;
            info.internalControlToken =
                owner_.serverInternalControlToken_;
            info.error =
                owner_.server_.status ==
                    TrayIcon::ServerStatus::Failed
                ? owner_.server_.failure
                : "";
            for (const auto& [name, app] :
                owner_.configuredApps_) {
                if (app.status != "invalid") {
                    info.apps[name] =
                        app.displayName + "\n" +
                        app.schemaSha256;
                }
            }
            promise->set_value(std::move(info));
        });
        return future.get();
    }

    void Stop() override {
        wxTheApp->CallAfter([this] {
            owner_.StopConfiguredServer();
        });
    }

private:
    TrayIcon& owner_;
};

TrayIcon::TrayIcon() {
#ifdef __APPLE__
    nativeTrayIcon_ = CreateNativeTrayIcon(this);
    AppendPermissionTrace("tray_set_native_icon result=" + BoolString(nativeTrayIcon_ != nullptr) +
                          " bundle_path=" + ComputerCppBundlePath());
#else
    const bool iconSet = SetIcon(CreateComputerTrayIcon(), "ComputerCpp");
    AppendPermissionTrace("tray_set_icon result=" + BoolString(iconSet) +
                          " bundle_path=" + ComputerCppBundlePath());
#endif
    serverTimer_ = std::make_unique<wxTimer>(this, ID_SERVER_TIMER);
    {
        std::string configError;
        AppConfig config = LoadAppConfig(&configError);
        if (configError.empty() &&
            EnsureGobiiDesktopApp(config, &configError)) {
            SaveAppConfig(config, &configError);
        }
        if (!configError.empty()) {
            AppendAppLog(
                "gobii",
                "default_app_registration_failed error=" +
                    configError);
        }
    }
    updateFlow_ = std::make_unique<TrayUpdateFlow>([this] {
#ifdef __APPLE__
        DestroyNativeTrayIcon(nativeTrayIcon_);
        nativeTrayIcon_ = nullptr;
#else
        RemoveIcon();
#endif
        wxExit();
    });
    StartOwnedDaemon();
    RefreshConfiguredServer(true);
    AdoptExistingServer(true);
#if defined(__APPLE__) || defined(_WIN32)
    configuredServerController_ =
        std::make_unique<TrayConfiguredServerController>(*this);
    auto http = std::shared_ptr<GobiiHttpTransport>(
        CreateCurlGobiiHttpTransport());
    gobiiController_ =
        std::make_unique<GobiiConnectionController>(
            http,
            CreateGobiiCredentialStore(),
            CreateGobiiArtifactUploader(http),
            *configuredServerController_,
            [](const std::string& url) {
                return wxLaunchDefaultBrowser(
                    wxString::FromUTF8(url));
            },
            [] {
                const auto permissions =
                    Platform::CheckPermissions(false);
                const auto session =
                    Platform::GetDesktopSessionState();
                return permissions.accessibility &&
                    permissions.screenCapture &&
                    (!session.detectionSupported ||
                     (session.available &&
                      session.onConsole &&
                      session.loginDone &&
                      !session.screenLocked));
            });
    gobiiController_->SetObserver([](
        const GobiiConnectionStatus& status) {
        wxTheApp->CallAfter([status] {
            AppendAppLog(
                "gobii",
                std::string("state=") +
                    GobiiConnectionStateName(status.state) +
                    (status.lastError.empty()
                        ? ""
                        : " error=" + status.lastError));
        });
    });
    gobiiController_->Initialize();
#endif
#ifdef __APPLE__
    wxTheApp->CallAfter([this] {
        Platform::PermissionStatus status = Platform::CheckPermissions(false);
        AppendPermissionTrace("tray_started status=" + PermissionStatusSummary(status) +
                              " bundle_path=" + ComputerCppBundlePath());
        if (!status.accessibility || !status.screenCapture) {
            SetUpPermissionsIfNeeded(false);
        }
    });
#endif
}

TrayIcon::~TrayIcon() {
    if (gobiiController_) {
        gobiiController_->Shutdown();
        gobiiController_.reset();
    }
#ifdef __APPLE__
    DestroyNativeTrayIcon(nativeTrayIcon_);
    nativeTrayIcon_ = nullptr;
#endif
    if (permissionDialog_) {
        permissionDialog_->Destroy();
        permissionDialog_ = nullptr;
    }
    if (settingsDialog_) {
        settingsDialog_->Destroy();
        settingsDialog_ = nullptr;
    }
    if (gobiiDialog_) {
        gobiiDialog_->Destroy();
        gobiiDialog_ = nullptr;
    }
    updateFlow_.reset();
    if (serverTimer_) {
        serverTimer_->Stop();
    }
    StopServerBlocking();
    serverTimer_.reset();
    StopDaemon("default");
    if (daemonThread_.joinable()) {
        daemonThread_.join();
    }
}

void TrayIcon::StartOwnedDaemon() {
    if (daemonStarted_) {
        return;
    }

    StopDaemon("default");
    WaitForDaemonStopped("default");

    daemonStarted_ = true;
    daemonThread_ = std::thread([] {
        DaemonOptions options;
        options.session = "default";
        RunDaemon(options);
    });
}

wxMenu* TrayIcon::CreatePopupMenu() {
    RefreshConfiguredServer();

    wxMenu* menu = new wxMenu;
#if defined(__APPLE__) || defined(_WIN32)
    if (gobiiController_) {
        const GobiiConnectionStatus status =
            gobiiController_->Status();
        wxString label = "Gobii: ";
        switch (status.state) {
            case GobiiConnectionState::Connected:
                label += status.currentRequestId.empty()
                    ? "Connected"
                    : "Agent is using this computer";
                break;
            case GobiiConnectionState::Paused:
                label += "Paused";
                break;
            case GobiiConnectionState::Pairing:
            case GobiiConnectionState::PairingPending:
                label += "Waiting for approval…";
                break;
            case GobiiConnectionState::Connecting:
                label += "Connecting…";
                break;
            case GobiiConnectionState::PermissionsRequired:
                label += "Permissions required";
                break;
            case GobiiConnectionState::UpdateRequired:
                label += "Update required";
                break;
            case GobiiConnectionState::AuthenticationExpired:
                label += "Authentication expired";
                break;
            case GobiiConnectionState::Error:
                label += "Error";
                break;
            case GobiiConnectionState::Disconnected:
                label += "Not Connected";
                break;
        }
        auto* statusItem = menu->Append(wxID_ANY, label);
        statusItem->Enable(false);
        if (status.state ==
            GobiiConnectionState::Disconnected ||
            status.state ==
            GobiiConnectionState::AuthenticationExpired ||
            status.state == GobiiConnectionState::Error) {
            menu->Append(
                ID_GOBII_CONNECT,
                "Connect to Gobii…");
        } else if (status.state ==
            GobiiConnectionState::Paused) {
            menu->Append(
                ID_GOBII_PAUSE_RESUME,
                "Resume Agent Access");
        } else if (status.state ==
            GobiiConnectionState::Connected) {
            menu->Append(
                ID_GOBII_PAUSE_RESUME,
                "Pause Agent Access");
        }
        if (!status.deviceId.empty() ||
            status.state == GobiiConnectionState::Error) {
            menu->Append(
                ID_GOBII_STATUS,
                "Gobii Connection…");
        }
        if (!status.deviceId.empty()) {
            menu->Append(
                ID_GOBII_MANAGE,
                "Manage in Gobii…");
            menu->Append(
                ID_GOBII_DISCONNECT,
                "Disconnect from Gobii");
        }
        menu->AppendSeparator();
    }
#endif
    wxString serverStatus;
    switch (server_.status) {
        case ServerStatus::Running:
            serverStatus = "Server: running on :" + std::to_string(server_.port);
            break;
        case ServerStatus::Starting:
            serverStatus = "Server: starting…";
            break;
        case ServerStatus::Stopping:
            serverStatus = "Server: stopping…";
            break;
        case ServerStatus::Failed:
            serverStatus = "Server: failed";
            break;
        case ServerStatus::Stopped:
            serverStatus = configuredApps_.empty()
                ? "Server: no apps configured"
                : "Server: stopped";
            break;
    }
    if (serverRestartRequired_ &&
        (server_.status == ServerStatus::Running ||
         server_.status == ServerStatus::Starting)) {
        serverStatus += " (restart required)";
    }
    wxMenuItem* serverStatusItem = menu->Append(wxID_ANY, serverStatus);
    serverStatusItem->Enable(false);
    wxMenuItem* startServer = menu->Append(ID_START_SERVER, "Start Server");
    startServer->Enable(
        !configuredApps_.empty() &&
        (server_.status == ServerStatus::Stopped ||
         (server_.status == ServerStatus::Failed && server_.pid <= 0)));
    wxMenuItem* stopServer = menu->Append(ID_STOP_SERVER, "Stop Server");
    stopServer->Enable(
        server_.status == ServerStatus::Running ||
        server_.status == ServerStatus::Starting ||
        (server_.status == ServerStatus::Failed && server_.pid > 0));
    if (!configuredApps_.empty()) {
        menu->AppendSeparator();
    }
    for (const auto& [_, app] : configuredApps_) {
        const std::string icon =
            app.status == "ready" ? "🟢 " :
            app.status == "invalid" ? "⚠️ " :
            app.status == "restart_required" ? "🟡 " : "⚪ ";
        const std::string status =
            app.status == "ready" ? "Ready" :
            app.status == "invalid" ? "Invalid" :
            app.status == "restart_required" ? "Restart Required" : "Configured";
        wxString label = icon + app.displayName + " — " + status;
        if (app.status == "invalid" && !app.error.empty()) {
            std::string detail = app.error;
            std::replace(detail.begin(), detail.end(), '\n', ' ');
            if (detail.size() > 120) {
                detail.resize(117);
                detail += "...";
            }
            label += ": " + detail;
        }
        wxMenuItem* item = menu->Append(wxID_ANY, label);
        item->Enable(false);
    }
    menu->AppendSeparator();

#ifdef __APPLE__
    menu->Append(ID_PERMISSIONS, "Permissions");
#endif
    menu->Append(ID_SETTINGS, "Settings...");
    std::string configError;
    const AppConfig config = LoadAppConfig(&configError);
    wxMenuItem* recordingToggle = menu->AppendCheckItem(
        ID_RECORDING_TOGGLE,
        "Record app commands");
    recordingToggle->Check(configError.empty() && config.recording.enabled);
    const auto now = std::chrono::steady_clock::now();
    if (activeRecordingCountRefreshedAt_.time_since_epoch().count() == 0 ||
        now - activeRecordingCountRefreshedAt_ >= std::chrono::seconds(2)) {
        cachedActiveRecordingCount_ = ActiveRecordingCount();
        activeRecordingCountRefreshedAt_ = now;
    }
    wxMenuItem* recordingStatus = menu->Append(
        wxID_ANY,
        "Active recordings: " + std::to_string(cachedActiveRecordingCount_));
    recordingStatus->Enable(false);

    wxMenu* advanced = new wxMenu;
    advanced->Append(ID_STATE, "Show State");
    advanced->Append(ID_TEST_SCREENSHOT, "Test Screenshot");
    advanced->Append(ID_TEST_MOUSE, "Test Mouse Move");
    advanced->Append(ID_SHOW_LOGS, "Show Logs");
    menu->AppendSubMenu(advanced, "Advanced");

    menu->AppendSeparator();
    menu->Append(ID_CHECK_UPDATES, "Check for Updates...");
    menu->Append(ID_QUIT, "Quit");
    return menu;
}

void TrayIcon::OnPermissions(wxCommandEvent&) {
    SetUpPermissionsIfNeeded(false);
}

void TrayIcon::OnSettings(wxCommandEvent&) {
    if (settingsDialog_) {
        PresentSettingsDialog(settingsDialog_);
        return;
    }
    settingsDialog_ = new LlmSettingsDialog({
#ifdef __APPLE__
        [this] { SetUpPermissionsIfNeeded(false); },
#else
        {},
#endif
        [this] { RefreshConfiguredServer(true); },
    });
    settingsDialog_->Bind(wxEVT_DESTROY, [this](wxWindowDestroyEvent&) {
        settingsDialog_ = nullptr;
        RefreshConfiguredServer(true);
    });
    PresentSettingsDialog(settingsDialog_);
}

void TrayIcon::OnRecordingToggle(wxCommandEvent& event) {
    std::string error;
    AppConfig config = LoadAppConfig(&error);
    if (!error.empty()) {
        wxMessageBox(error, "ComputerCpp Recording", wxOK | wxICON_ERROR);
        return;
    }
    config.recording.enabled = event.IsChecked();
    if (!SaveAppConfig(config, &error)) {
        wxMessageBox(
            "Could not save recording setting:\n" + error,
            "ComputerCpp Recording",
            wxOK | wxICON_ERROR);
        return;
    }
    if (config.recording.enabled && !Platform::CheckPermissions(false).screenCapture) {
        Platform::RequestScreenCapturePermission();
        if (!Platform::CheckPermissions(false).screenCapture) {
            wxMessageBox(
                "Recording is enabled, but Screen Recording permission is missing. "
                "Commands will continue normally and recording attempts will be reported as failed.",
                "ComputerCpp Recording",
                wxOK | wxICON_WARNING);
        }
    }
}

void TrayIcon::OnShowLogs(wxCommandEvent&) {
    const std::filesystem::path logPath = ComputerCpp::AppLogPath();
    {
        std::filesystem::create_directories(logPath.parent_path());
        std::ofstream log(logPath, std::ios::app);
    }
    AppendAppLog("tray", "show_logs_requested path=" + logPath.string());

    bool opened = false;
#ifdef __APPLE__
    std::string command = "/usr/bin/open -a Console " + ShellQuote(logPath.string()) + " >/dev/null 2>&1";
    opened = wxExecute(command, wxEXEC_SYNC) == 0;
#endif
    if (!opened) {
        opened = wxLaunchDefaultApplication(logPath.string());
    }
    if (!opened) {
        wxString message;
        message << "Could not open log file:\n" << logPath.string();
        wxMessageBox(message, "ComputerCpp Logs", wxOK | wxICON_ERROR);
    }
}

void TrayIcon::OnCheckForUpdates(wxCommandEvent&) {
    if (updateFlow_) {
        updateFlow_->CheckForUpdates();
    }
}

void TrayIcon::OnStartServer(wxCommandEvent&) {
    StartConfiguredServer();
}

void TrayIcon::OnStopServer(wxCommandEvent&) {
    StopConfiguredServer();
}

void TrayIcon::OnGobiiStatus(wxCommandEvent&) {
    if (!gobiiController_) return;
    if (gobiiDialog_) {
        gobiiDialog_->Raise();
        return;
    }
    gobiiDialog_ =
        new GobiiConnectionDialog(
            *gobiiController_,
            {
                [this] { SetUpPermissionsIfNeeded(true); },
                [this] {
                    if (updateFlow_) {
                        updateFlow_->CheckForUpdates();
                    }
                },
            });
    gobiiDialog_->Bind(
        wxEVT_DESTROY,
        [this](wxWindowDestroyEvent&) {
            gobiiDialog_ = nullptr;
        });
    gobiiDialog_->Show();
}

void TrayIcon::OnGobiiConnect(wxCommandEvent&) {
    if (!gobiiController_) return;
    wxCommandEvent statusEvent;
    OnGobiiStatus(statusEvent);
}

void TrayIcon::OnGobiiPauseResume(wxCommandEvent&) {
    if (!gobiiController_) return;
    if (gobiiController_->Status().state ==
        GobiiConnectionState::Paused) {
        gobiiController_->Resume();
    } else {
        gobiiController_->Pause();
    }
}

void TrayIcon::OnGobiiDisconnect(wxCommandEvent&) {
    if (!gobiiController_) return;
    if (wxMessageBox(
            "Disconnect this computer from Gobii and remove its "
            "local secure credential?",
            "Disconnect from Gobii",
            wxYES_NO | wxNO_DEFAULT | wxICON_WARNING) == wxYES) {
        gobiiController_->Disconnect();
    }
}

void TrayIcon::OnGobiiManage(wxCommandEvent&) {
    std::string error;
    const AppConfig config = LoadAppConfig(&error);
    if (error.empty()) {
        wxLaunchDefaultBrowser(wxString::FromUTF8(
            config.gobii.baseUrl +
            "/app/integrations"));
    }
}

void TrayIcon::OnServerProcessEnded(wxProcessEvent& event) {
    if (server_.pid != event.GetPid()) {
        return;
    }
    const ServerStatus previous = server_.status;
    RemoveTrayAppServerStateForPid(server_.statePath, server_.pid, nullptr);
    ClearServerProcess();
    if (previous == ServerStatus::Stopping) {
        server_.status = server_.failure.empty()
            ? ServerStatus::Stopped
            : ServerStatus::Failed;
        if (!server_.failure.empty()) {
            QueueServerNotification(server_.failure);
        }
    } else {
        server_.status = ServerStatus::Failed;
        server_.failure = previous == ServerStatus::Starting
            ? "server exited before becoming healthy"
            : "server process exited unexpectedly";
        QueueServerNotification(server_.failure);
    }
    RefreshConfiguredServer(true);
}

void TrayIcon::OnServerTimer(wxTimerEvent&) {
    PollServer();
}

void TrayIcon::RefreshConfiguredServer(bool force) {
    const auto now = std::chrono::steady_clock::now();
    if (!force &&
        configuredServerRefreshedAt_.time_since_epoch().count() != 0 &&
        now - configuredServerRefreshedAt_ < std::chrono::seconds(2)) {
        return;
    }

    std::string error;
    const AppConfig config = LoadAppConfig(&error);
    if (!error.empty()) {
        return;
    }
    configuredServerRefreshedAt_ = now;
    const bool hasActiveServer =
        server_.status == ServerStatus::Starting ||
        server_.status == ServerStatus::Running ||
        server_.status == ServerStatus::Stopping ||
        (server_.status == ServerStatus::Failed && server_.pid > 0);
    const std::string configSignature = ServerConfigSignature(config);
    if (hasActiveServer) {
        if (!server_.configSignature.empty() &&
            server_.configSignature != configSignature) {
            serverRestartRequired_ = true;
        }
    } else {
        serverAuthToken_ = config.server.authToken;
        server_.configSignature.clear();
        serverRestartRequired_ = false;
    }

    std::map<std::string, ConfiguredAppStatus> refreshed;
    for (const auto& [name, app] : config.server.apps) {
        ConfiguredAppStatus status;
        status.displayName = app.displayName.empty() ? name : app.displayName;
        status.path = AbsolutePathString(app.path);
        if (status.path.empty()) {
            status.path = app.path;
        }
        auto previous = configuredApps_.find(name);
        if (serverRestartRequired_ &&
            server_.status == ServerStatus::Running) {
            status.status = "restart_required";
        } else if (server_.status == ServerStatus::Running &&
            previous != configuredApps_.end() &&
            previous->second.displayName == status.displayName &&
            previous->second.path == status.path) {
            status.status = previous->second.status;
            status.error = previous->second.error;
            status.schemaSha256 = previous->second.schemaSha256;
        } else if (server_.status == ServerStatus::Running) {
            status.status = "restart_required";
        }
        refreshed[name] = std::move(status);
    }
    configuredApps_ = std::move(refreshed);
}

void TrayIcon::ApplyServerHealth(const std::string& responseBody) {
    json health = json::parse(responseBody, nullptr, false);
    if (health.is_discarded() || !health.is_object() ||
        !health.contains("apps") || !health["apps"].is_object()) {
        return;
    }
    const json& apps = health["apps"];
    bool registryMismatch = apps.size() != configuredApps_.size();
    for (auto& [name, configured] : configuredApps_) {
        if (!apps.contains(name) || !apps[name].is_object()) {
            configured.status = "restart_required";
            configured.error.clear();
            registryMismatch = true;
            continue;
        }
        const json& served = apps[name];
        const std::string servedPath = served.value("path", "");
        const std::string servedDisplayName =
            served.value("displayName", configured.displayName);
        if ((!servedPath.empty() && servedPath != configured.path) ||
            servedDisplayName != configured.displayName) {
            configured.status = "restart_required";
            configured.error.clear();
            registryMismatch = true;
            continue;
        }
        configured.status = served.value("status", "invalid");
        configured.error =
            served.contains("error") && served["error"].is_string()
            ? served["error"].get<std::string>()
            : "";
        configured.schemaSha256 =
            served.value("schemaSha256", "");
    }
    serverRestartRequired_ = serverRestartRequired_ || registryMismatch;
}

void TrayIcon::AdoptExistingServer(bool removeInvalidState) {
    RefreshConfiguredServer(true);
    std::string configError;
    const AppConfig config = LoadAppConfig(&configError);
    if (!configError.empty()) {
        return;
    }
    const std::filesystem::path statePath = TrayAppServerStatePath();
    auto state = LoadTrayAppServerState(statePath, nullptr);
    if (!state) {
        if (removeInvalidState) {
            RemoveTrayAppServerState(statePath, nullptr);
        }
        return;
    }
    const bool validCommand =
        state->configured &&
        LooksLikeConfiguredServerProcess(state->pid);
    const bool validListener =
        NormalizeBindHost(state->host) == NormalizeBindHost(config.server.host) &&
        state->port == config.server.port;
    std::string healthBody;
    const bool valid =
        validCommand &&
        validListener &&
        IsProcessAlive(state->pid) &&
        HttpHealthOk(*state, serverAuthToken_, 1000, &healthBody);
    if (!valid) {
        if (removeInvalidState &&
            state->configured &&
            !IsProcessAlive(state->pid)) {
            RemoveTrayAppServerStateForPid(statePath, state->pid, nullptr);
        }
        return;
    }
    server_.host = state->host;
    server_.port = state->port;
    server_.pid = state->pid;
    server_.url = state->url;
    serverInternalControlToken_ = state->internalControlToken;
    server_.process = nullptr;
    server_.status = ServerStatus::Running;
    server_.statePath = statePath;
    server_.failure.clear();
    server_.configSignature = ServerConfigSignature(config);
    serverRestartRequired_ = false;
    ApplyServerHealth(healthBody);
    AppendAppLog(
        "server",
        "adopted configured url=" + server_.url +
        " pid=" + std::to_string(server_.pid));
}

void TrayIcon::CleanupLegacyServers() {
    std::vector<std::filesystem::path> paths =
        ListTrayAppServerStatePaths(nullptr);
    paths.push_back(TrayAppServerStatePath());
    for (const auto& path : paths) {
        auto state = LoadTrayAppServerState(path, nullptr);
        if (!state || state->configured) {
            continue;
        }
        if (!IsProcessAlive(state->pid)) {
            RemoveTrayAppServerState(path, nullptr);
            continue;
        }
        if (!LooksLikeTrayAppServerProcess(*state)) {
            continue;
        }
        AppendAppLog(
            "server",
            "stopping_legacy pid=" + std::to_string(state->pid) +
            " url=" + state->url);
        if (!RequestServerShutdown(*state, serverAuthToken_)) {
            SignalServerProcess(state->pid, wxSIGTERM, true);
        }
        if (!WaitForProcessExit(state->pid, false)) {
            SignalServerProcess(state->pid, wxSIGKILL, true);
            WaitForProcessExit(state->pid, false);
        }
        RemoveTrayAppServerStateForPid(path, state->pid, nullptr);
    }
}

void TrayIcon::StartConfiguredServer() {
    if (server_.status == ServerStatus::Running ||
        server_.status == ServerStatus::Starting ||
        server_.status == ServerStatus::Stopping) {
        return;
    }
    std::string error;
    std::vector<std::string> warnings;
    AppConfig config = LoadAppConfig(&error, &warnings);
    if (!error.empty()) {
        wxMessageBox(error, "ComputerCpp Server", wxOK | wxICON_ERROR);
        return;
    }
    for (const auto& warning : warnings) {
        AppendAppLog("server", "config_migration_warning " + warning);
    }
    if (EnsureServerAuthToken(config)) {
        if (!SaveAppConfig(config, &error)) {
            wxMessageBox(
                "Could not save generated server token:\n" + error,
                "ComputerCpp Server",
                wxOK | wxICON_ERROR);
            return;
        }
    }
    if (config.server.apps.empty()) {
        wxMessageBox(
            "Configure at least one Lua app in Settings > Server first.",
            "ComputerCpp Server",
            wxOK | wxICON_INFORMATION);
        return;
    }
    serverAuthToken_ = config.server.authToken;
    RefreshConfiguredServer(true);
    AdoptExistingServer(false);
    if (server_.status == ServerStatus::Running) {
        return;
    }
    const std::filesystem::path singletonStatePath =
        TrayAppServerStatePath();
    auto existingState =
        LoadTrayAppServerState(singletonStatePath, nullptr);
    if (existingState && existingState->configured) {
        if (IsProcessAlive(existingState->pid)) {
            std::string existingHealthBody;
            const bool healthMatches = HttpHealthOk(
                *existingState,
                serverAuthToken_,
                1000,
                &existingHealthBody);
#if defined(_WIN32)
            const bool verifiedProcess = healthMatches;
#else
            const bool verifiedProcess =
                LooksLikeConfiguredServerProcess(existingState->pid);
#endif
            if (!verifiedProcess) {
                RemoveTrayAppServerStateForPid(
                    singletonStatePath,
                    existingState->pid,
                    nullptr);
            } else {
                server_.host = existingState->host;
                server_.port = existingState->port;
                server_.pid = existingState->pid;
                server_.url = existingState->url;
                server_.process = nullptr;
                server_.statePath = singletonStatePath;
                server_.status = ServerStatus::Failed;
                server_.configSignature.clear();
                serverRestartRequired_ = true;
                if (healthMatches) {
                    ApplyServerHealth(existingHealthBody);
                }
                server_.failure =
                    "The existing configured server on port " +
                    std::to_string(server_.port) +
                    " no longer matches Settings. Stop it, then start the server again.";
                QueueServerNotification(server_.failure);
                return;
            }
        }
        RemoveTrayAppServerStateForPid(
            singletonStatePath,
            existingState->pid,
            nullptr);
    }
    CleanupLegacyServers();

    const std::string host = NormalizeBindHost(config.server.host);
    if (!IsTcpPortAvailable(host, config.server.port)) {
        server_.status = ServerStatus::Failed;
        server_.failure =
            "configured port " + std::to_string(config.server.port) +
            " is not available";
        QueueServerNotification(server_.failure);
        return;
    }

    const std::filesystem::path cliPath = ComputerCppCliHelperPath();
    std::error_code existsError;
    if (!std::filesystem::exists(cliPath, existsError) || existsError) {
        server_.status = ServerStatus::Failed;
        server_.failure =
            "could not find bundled CLI helper: " + cliPath.string();
        QueueServerNotification(server_.failure);
        return;
    }

    server_.host = host;
    server_.port = config.server.port;
    server_.url = ServerDisplayUrl(config.server.host, config.server.port);
    server_.statePath = TrayAppServerStatePath();
    server_.status = ServerStatus::Starting;
    server_.failure.clear();
    server_.configSignature = ServerConfigSignature(config);
    serverRestartRequired_ = false;
    server_.shutdownStage = 0;
    server_.deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    server_.nextHealthProbe = std::chrono::steady_clock::now();
    serverInternalControlToken_ = GenerateServerAuthToken();

    std::vector<std::wstring> argStorage;
    auto addArg = [&argStorage](const std::string& value) {
        argStorage.push_back(wxString::FromUTF8(value).ToStdWstring());
    };
    auto addLiteralArg = [&argStorage](const wchar_t* value) {
        argStorage.emplace_back(value);
    };
    addArg(cliPath.string());
    addLiteralArg(L"app");
    addLiteralArg(L"serve");
    addLiteralArg(L"--configured");
    addLiteralArg(L"--tray-state-file");
    addArg(server_.statePath.string());
    std::vector<const wchar_t*> argv;
    argv.reserve(argStorage.size() + 1);
    for (const auto& arg : argStorage) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);

    wxString previousLogFile;
    const bool hadPreviousLogFile = wxGetEnv("COMPUTER_CPP_LOG_FILE", &previousLogFile);
    wxSetEnv("COMPUTER_CPP_LOG_FILE", wxString::FromUTF8(ComputerCpp::AppLogPath().string()));
    wxString previousInternalToken;
    const bool hadPreviousInternalToken = wxGetEnv(
        "COMPUTER_CPP_GOBII_INTERNAL_CONTROL_TOKEN",
        &previousInternalToken);
    wxSetEnv(
        "COMPUTER_CPP_GOBII_INTERNAL_CONTROL_TOKEN",
        wxString::FromUTF8(serverInternalControlToken_));
    AppendAppLog(
        "server",
        "start_requested configured listen=" + host + ":" +
        std::to_string(config.server.port));

    server_.process = new wxProcess(this, ID_SERVER_PROCESS);
    server_.pid = wxExecute(argv.data(), wxEXEC_ASYNC, server_.process);
    if (hadPreviousLogFile) {
        wxSetEnv("COMPUTER_CPP_LOG_FILE", previousLogFile);
    } else {
        wxUnsetEnv("COMPUTER_CPP_LOG_FILE");
    }
    if (hadPreviousInternalToken) {
        wxSetEnv(
            "COMPUTER_CPP_GOBII_INTERNAL_CONTROL_TOKEN",
            previousInternalToken);
    } else {
        wxUnsetEnv("COMPUTER_CPP_GOBII_INTERNAL_CONTROL_TOKEN");
    }

    if (server_.pid == 0) {
        ReleaseServerProcess(server_);
        server_.status = ServerStatus::Failed;
        server_.failure = "failed to launch the server process";
        QueueServerNotification(server_.failure);
        return;
    }

    if (serverTimer_ && !serverTimer_->IsRunning()) {
        serverTimer_->Start(250);
    }
}

void TrayIcon::StopConfiguredServer() {
    if (server_.status != ServerStatus::Running &&
        server_.status != ServerStatus::Starting &&
        !(server_.status == ServerStatus::Failed && server_.pid > 0)) {
        return;
    }
    server_.status = ServerStatus::Stopping;
    server_.failure.clear();
    AppendAppLog(
        "server",
        "stop_requested configured pid=" + std::to_string(server_.pid));

    const TrayAppServerState state = CurrentServerState();
    const bool shutdownRequested = server_.pid > 0 &&
        RequestServerShutdown(state, serverAuthToken_);
    server_.shutdownStage = shutdownRequested ? 0 : 1;
    if (!shutdownRequested && server_.pid > 0) {
        SignalServerProcess(
            server_.pid,
            wxSIGTERM,
            server_.process != nullptr);
    }
    server_.deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    if (serverTimer_ && !serverTimer_->IsRunning()) {
        serverTimer_->Start(250);
    }
}

void TrayIcon::PollServer() {
    const auto now = std::chrono::steady_clock::now();
    if (server_.status == ServerStatus::Starting) {
        if (ProcessHasExited(server_.pid, server_.process != nullptr)) {
            RemoveTrayAppServerStateForPid(
                server_.statePath,
                server_.pid,
                nullptr);
            ClearServerProcess();
            server_.status = ServerStatus::Failed;
            server_.failure = "server exited before becoming healthy";
            QueueServerNotification(server_.failure);
        } else if (now >= server_.nextHealthProbe) {
            server_.nextHealthProbe =
                now + std::chrono::milliseconds(500);
            const TrayAppServerState state = CurrentServerState();
            std::string healthBody;
            if (HttpHealthOk(
                    state,
                    serverAuthToken_,
                    100,
                    &healthBody)) {
                server_.status = ServerStatus::Running;
                server_.failure.clear();
                ApplyServerHealth(healthBody);
                AppendAppLog(
                    "server",
                    "started configured url=" + server_.url +
                    " pid=" + std::to_string(server_.pid));
            }
        }
        if (server_.status == ServerStatus::Starting &&
            now >= server_.deadline) {
            SignalServerProcess(
                server_.pid,
                wxSIGTERM,
                server_.process != nullptr);
            server_.status = ServerStatus::Stopping;
            server_.shutdownStage = 1;
            server_.failure =
                "server did not become healthy within five seconds";
            server_.deadline = now + std::chrono::seconds(2);
        }
    } else if (server_.status == ServerStatus::Stopping) {
        if (ProcessHasExited(server_.pid, server_.process != nullptr)) {
            const std::string failure = server_.failure;
            RemoveTrayAppServerStateForPid(
                server_.statePath,
                server_.pid,
                nullptr);
            ClearServerProcess();
            server_.status = failure.empty()
                ? ServerStatus::Stopped
                : ServerStatus::Failed;
            if (!failure.empty()) {
                QueueServerNotification(failure);
            }
            RefreshConfiguredServer(true);
        } else if (now >= server_.deadline) {
            if (server_.shutdownStage == 0) {
                SignalServerProcess(
                    server_.pid,
                    wxSIGTERM,
                    server_.process != nullptr);
                server_.shutdownStage = 1;
                server_.deadline = now + std::chrono::seconds(2);
            } else if (server_.shutdownStage == 1) {
                SignalServerProcess(
                    server_.pid,
                    wxSIGKILL,
                    server_.process != nullptr);
                server_.shutdownStage = 2;
                server_.deadline = now + std::chrono::seconds(2);
            } else {
                server_.status = ServerStatus::Failed;
                server_.failure = server_.failure.empty()
                    ? "server process did not stop"
                    : server_.failure + "; process did not stop";
                QueueServerNotification(server_.failure);
            }
        }
    }
    if (server_.status != ServerStatus::Starting &&
        server_.status != ServerStatus::Stopping &&
        serverTimer_) {
        serverTimer_->Stop();
    }
}

void TrayIcon::QueueServerNotification(std::string message) {
    if (message.empty()) {
        return;
    }
    pendingServerNotifications_.push_back(std::move(message));
    if (serverNotificationScheduled_ || serverNotificationShowing_) {
        return;
    }
    serverNotificationScheduled_ = true;
    CallAfter([this] {
        ShowPendingServerNotifications();
    });
}

void TrayIcon::ShowPendingServerNotifications() {
    serverNotificationScheduled_ = false;
    if (serverNotificationShowing_ || pendingServerNotifications_.empty()) {
        return;
    }
    std::vector<std::string> notifications;
    notifications.swap(pendingServerNotifications_);
    std::ostringstream message;
    if (notifications.size() == 1) {
        message << notifications.front();
    } else {
        message << "Server errors:";
        for (const auto& notification : notifications) {
            message << "\n\n• " << notification;
        }
    }
    serverNotificationShowing_ = true;
    wxMessageBox(message.str(), "ComputerCpp Server", wxOK | wxICON_ERROR);
    serverNotificationShowing_ = false;
    if (!pendingServerNotifications_.empty()) {
        serverNotificationScheduled_ = true;
        CallAfter([this] {
            ShowPendingServerNotifications();
        });
    }
}

void TrayIcon::ReleaseServerProcess(ManagedServer& server) {
    if (!server.process) {
        return;
    }
    server.process->Detach();
    delete server.process;
    server.process = nullptr;
}

TrayAppServerState TrayIcon::CurrentServerState() const {
    TrayAppServerState state;
    state.version = 2;
    state.configured = true;
    state.pid = server_.pid;
    state.host = server_.host;
    state.port = server_.port;
    state.url = server_.url;
    state.internalControlToken = serverInternalControlToken_;
    return state;
}

void TrayIcon::ClearServerProcess() {
    ReleaseServerProcess(server_);
    server_.pid = 0;
    server_.port = 0;
    server_.url.clear();
    serverInternalControlToken_.clear();
}

void TrayIcon::StopServerBlocking() {
    std::string configError;
    const AppConfig config = LoadAppConfig(&configError);
    const std::string token = !serverAuthToken_.empty()
        ? serverAuthToken_
        : (configError.empty() ? config.server.authToken : std::string());
    if (server_.pid <= 0 || !IsProcessAlive(server_.pid)) {
        ReleaseServerProcess(server_);
        return;
    }
    const TrayAppServerState state = CurrentServerState();
    if (!RequestServerShutdown(state, token)) {
        SignalServerProcess(
            server_.pid,
            wxSIGTERM,
            server_.process != nullptr);
    }
    if (!WaitForProcessExit(server_.pid, server_.process != nullptr)) {
        SignalServerProcess(
            server_.pid,
            wxSIGKILL,
            server_.process != nullptr);
        WaitForProcessExit(server_.pid, server_.process != nullptr);
    }
    RemoveTrayAppServerStateForPid(
        server_.statePath,
        server_.pid,
        nullptr);
    ClearServerProcess();
    server_.status = ServerStatus::Stopped;
    server_.configSignature.clear();
    serverRestartRequired_ = false;
}

void TrayIcon::SetUpPermissionsIfNeeded(bool notifyWhenGranted) {
    (void)notifyWhenGranted;
#ifndef __APPLE__
    return;
#else
    if (permissionDialog_) {
        PresentPermissionDialog(permissionDialog_);
        return;
    }

    permissionDialog_ = new PermissionSetupDialog();
    permissionDialog_->Bind(wxEVT_DESTROY, [this](wxWindowDestroyEvent&) {
        permissionDialog_ = nullptr;
    });
    PresentPermissionDialog(permissionDialog_);
#endif
}

void TrayIcon::OnState(wxCommandEvent&) {
    auto app = Platform::GetFrontmostApp();
    auto focused = Platform::GetFocusedElementInfo();
    wxString message;
    if (app.available) {
        message << "Frontmost: " << app.name << " [" << app.bundleId << "] pid=" << app.pid << "\n";
    } else {
        message << "Frontmost: unknown\n";
    }
    message << "Focused element: " << (focused.available ? focused.role : "none") << "\n";
    message << "Text input: " << (focused.acceptsTextInput ? "yes" : "no") << "\n";
    wxMessageBox(message, "ComputerCpp State", wxOK | wxICON_INFORMATION);
}

void TrayIcon::OnTestScreenshot(wxCommandEvent&) {
    std::thread([] {
        std::string path = TemporaryScreenshotPath(
            "computer.cpp-test-screenshot.png");
        bool ok = Platform::SaveScreenshot(path);
        wxTheApp->CallAfter([ok, path] {
            wxMessageBox(ok ? "Saved screenshot to " + path : "Screenshot failed",
                         "ComputerCpp Screenshot",
                         wxOK | (ok ? wxICON_INFORMATION : wxICON_ERROR));
        });
    }).detach();
}

void TrayIcon::OnTestMouse(wxCommandEvent&) {
    std::thread([] {
        Platform::PermissionStatus status = Platform::CheckPermissions(false);
        if (!status.accessibility) {
            wxTheApp->CallAfter([] {
                wxMessageBox("Mouse move test failed: Accessibility permission is missing.",
                             "ComputerCpp Mouse",
                             wxOK | wxICON_ERROR);
            });
            return;
        }
        int width = 0;
        int height = 0;
        Platform::GetScreenSize(width, height);
        double cx = width / 2.0;
        double cy = height / 2.0;
        double radius = std::max(80.0, std::min(width, height) / 12.0);
        double startX = 0.0;
        double startY = 0.0;
        Platform::GetCursorPosition(startX, startY);
        for (int i = 0; i <= 80; ++i) {
            double angle = i * (2.0 * M_PI / 80.0);
            Platform::MoveMouse(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius);
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        double endX = 0.0;
        double endY = 0.0;
        Platform::GetCursorPosition(endX, endY);
        double distance = std::hypot(endX - startX, endY - startY);
        bool moved = distance > 8.0;
        wxTheApp->CallAfter([moved, distance] {
            std::ostringstream message;
            if (moved) {
                message << "Mouse move test passed. Cursor moved "
                        << std::fixed << std::setprecision(1) << distance << " px.";
            } else {
                message << "Mouse move test failed. Cursor only moved "
                        << std::fixed << std::setprecision(1) << distance
                        << " px. Restart ComputerCpp after granting Accessibility, then test again.";
            }
            wxMessageBox(message.str(),
                         "ComputerCpp Mouse",
                         wxOK | (moved ? wxICON_INFORMATION : wxICON_ERROR));
        });
    }).detach();
}

void TrayIcon::OnTaskbarRightUp(wxTaskBarIconEvent&) {
    std::unique_ptr<wxMenu> menu(CreatePopupMenu());
    if (menu) {
        PopupMenu(menu.get());
    }
}

void TrayIcon::OnQuit(wxCommandEvent&) {
    RemoveIcon();
    wxExit();
}

}
