#include "DaemonBrowser.h"

#include "computer_cpp/AppConfig.h"
#include "computer_cpp/Browser.h"
#include "computer_cpp/StringUtils.h"

#include "DaemonParsing.h"
#include "DaemonProtocol.h"
#include "../core/CurlHandle.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include "computer_cpp/WindowsUtil.h"
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

using json = nlohmann::json;

namespace ComputerCpp {
namespace {

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
bool g_winsockInitialized = false;

bool EnsureSocketsInitialized() {
    if (g_winsockInitialized) {
        return true;
    }
    WSADATA data {};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return false;
    }
    g_winsockInitialized = true;
    return true;
}

void CloseSocket(SocketHandle socket) {
    if (socket != kInvalidSocket) {
        closesocket(socket);
    }
}
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;

bool EnsureSocketsInitialized() {
    return true;
}

void CloseSocket(SocketHandle socket) {
    if (socket != kInvalidSocket) {
        close(socket);
    }
}
#endif

struct ParsedWebSocketUrl {
    std::string host;
    int port = 0;
    std::string path;
};

struct SocketOwner {
    SocketHandle socket = kInvalidSocket;

    ~SocketOwner() {
        CloseSocket(socket);
    }

    SocketOwner() = default;
    SocketOwner(const SocketOwner&) = delete;
    SocketOwner& operator=(const SocketOwner&) = delete;
    SocketOwner(SocketOwner&& other) noexcept : socket(other.socket) {
        other.socket = kInvalidSocket;
    }
    SocketOwner& operator=(SocketOwner&& other) noexcept {
        if (this != &other) {
            CloseSocket(socket);
            socket = other.socket;
            other.socket = kInvalidSocket;
        }
        return *this;
    }

    bool valid() const {
        return socket != kInvalidSocket;
    }
};

size_t CurlWriteString(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string HttpGet(std::string_view url, long timeoutMs, long* status = nullptr) {
    CurlHandle curl;
    if (!curl.valid()) {
        return "";
    }
    std::string body;
    std::string urlString(url);
    curl_easy_setopt(curl.get(), CURLOPT_URL, urlString.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, timeoutMs);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, CurlWriteString);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "computer.cpp/0.1");
    CURLcode code = curl_easy_perform(curl.get());
    long httpStatus = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &httpStatus);
    if (status) {
        *status = httpStatus;
    }
    if (code != CURLE_OK || httpStatus < 200 || httpStatus >= 300) {
        return "";
    }
    return body;
}

std::string CdpBaseUrl(const std::string& host, int port) {
    return "http://" + host + ":" + std::to_string(port);
}

bool ProbeCdp(const std::string& host, int port) {
    return !HttpGet(CdpBaseUrl(host, port) + "/json/version", 500).empty();
}

bool LaunchBrowserForCdp(
    const BrowserDescriptor& browser,
    const std::filesystem::path& userDataDir,
    const std::string& proxyServer,
    int port
) {
    const std::string flag = "--remote-debugging-port=" + std::to_string(port);
    const std::string userDataFlag = "--user-data-dir=" + userDataDir.string();
    const std::string proxyFlag = "--proxy-server=" + proxyServer;
#if defined(__APPLE__)
    pid_t pid = 0;
    std::vector<std::string> args = {
        "/usr/bin/open", "-n", "-a", browser.applicationName,
        "--args", flag, userDataFlag, "--no-first-run", "--no-default-browser-check"};
    if (!proxyServer.empty()) args.push_back(proxyFlag);
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    int rc = posix_spawn(&pid, "/usr/bin/open", nullptr, nullptr, argv.data(), environ);
    if (rc != 0) {
        return false;
    }
    int status = 0;
    return waitpid(pid, &status, 0) >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#elif defined(_WIN32)
    if (browser.executable.empty()) return false;
    std::vector<std::string> args = {browser.executable, flag, userDataFlag,
        "--no-first-run", "--no-default-browser-check"};
    if (!proxyServer.empty()) args.push_back(proxyFlag);
    return Windows::LaunchDetached(args);
#else
    if (browser.executable.empty()) return false;
    std::vector<std::string> args = {browser.executable, flag, userDataFlag,
        "--no-first-run", "--no-default-browser-check"};
    if (!proxyServer.empty()) args.push_back(proxyFlag);
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    pid_t pid = 0;
    return posix_spawn(&pid, browser.executable.c_str(), nullptr, nullptr, argv.data(), environ) == 0;
#endif
}

std::optional<int> ReadActivePort(const std::filesystem::path& userDataDir) {
    std::ifstream input(userDataDir / "DevToolsActivePort");
    int port = 0;
    if (input >> port && port > 0 && port <= 65535) return port;
    return std::nullopt;
}

std::optional<int> EnvPort() {
    const char* raw = std::getenv("COMPUTER_CPP_CHROME_CDP_PORT");
    if (!raw || !*raw) return std::nullopt;
    try {
        int value = std::stoi(raw);
        if (value > 0 && value <= 65535) return value;
    } catch (...) {
    }
    return std::nullopt;
}

class BrowserLaunchLock {
public:
    explicit BrowserLaunchLock(const std::filesystem::path& path) {
#if defined(_WIN32)
        for (int attempt = 0; attempt < 100; ++attempt) {
            handle_ = CreateFileW(
                path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle_ != INVALID_HANDLE_VALUE) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
#else
        fd_ = ::open(path.c_str(), O_CREAT | O_RDWR, 0600);
        if (fd_ >= 0) {
            bool acquired = false;
            for (int attempt = 0; attempt < 100; ++attempt) {
                if (::flock(fd_, LOCK_EX | LOCK_NB) == 0) {
                    acquired = true;
                    break;
                }
                if (errno != EINTR && errno != EWOULDBLOCK && errno != EAGAIN) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!acquired) {
                ::close(fd_);
                fd_ = -1;
            }
        }
#endif
    }

    ~BrowserLaunchLock() {
#if defined(_WIN32)
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
        if (fd_ >= 0) {
            ::flock(fd_, LOCK_UN);
            ::close(fd_);
        }
#endif
    }

    bool valid() const {
#if defined(_WIN32)
        return handle_ != INVALID_HANDLE_VALUE;
#else
        return fd_ >= 0;
#endif
    }

private:
#if defined(_WIN32)
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};

std::optional<ParsedWebSocketUrl> ParseWebSocketUrl(const std::string& url) {
    std::string_view view(url);
    constexpr std::string_view wsPrefix = "ws://";
    if (view.substr(0, wsPrefix.size()) != wsPrefix) {
        return std::nullopt;
    }
    view.remove_prefix(wsPrefix.size());
    auto slash = view.find('/');
    std::string_view authority = slash == std::string_view::npos ? view : view.substr(0, slash);
    std::string path = slash == std::string_view::npos ? "/" : std::string(view.substr(slash));
    if (authority.empty()) {
        return std::nullopt;
    }
    std::string host;
    int port = 80;
    auto colon = authority.rfind(':');
    if (colon != std::string_view::npos) {
        host = std::string(authority.substr(0, colon));
        auto parsed = ParseIntegerStrict<int>(authority.substr(colon + 1));
        if (!parsed || *parsed <= 0 || *parsed > 65535) {
            return std::nullopt;
        }
        port = *parsed;
    } else {
        host = std::string(authority);
    }
    if (host.empty()) {
        return std::nullopt;
    }
    return ParsedWebSocketUrl{host, port, path};
}

struct SelectedTarget {
    std::string webSocketUrl;
    std::string id;
    std::string url;
    std::string title;
    std::string browserContextId;
};

std::optional<SelectedTarget> ChooseTarget(
    const json& targets,
    const std::string& targetId,
    const std::string& targetUrlPrefix,
    const std::string& targetTitle,
    const std::string& browserContextId
) {
    if (!targets.is_array()) {
        return std::nullopt;
    }
    std::optional<SelectedTarget> firstPage;
    for (const auto& target : targets) {
        if (!target.is_object() ||
            target.value("type", "") != "page" ||
            !target.contains("webSocketDebuggerUrl") ||
            !target["webSocketDebuggerUrl"].is_string()) {
            continue;
        }
        if (!browserContextId.empty() && target.value("browserContextId", "") != browserContextId) {
            continue;
        }
        const std::string id = target.value("id", "");
        const std::string url = target.value("url", "");
        const std::string title = target.value("title", "");
        const std::string ws = target["webSocketDebuggerUrl"].get<std::string>();
        SelectedTarget selected{ws, id, url, title, target.value("browserContextId", "")};
        if (!targetId.empty() && id == targetId) {
            return selected;
        }
        if (!targetId.empty()) {
            continue;
        }
        const bool urlMatches = targetUrlPrefix.empty() || url.rfind(targetUrlPrefix, 0) == 0;
        const bool titleMatches = targetTitle.empty() || title == targetTitle;
        if ((!targetUrlPrefix.empty() || !targetTitle.empty()) && urlMatches && titleMatches) {
            return selected;
        }
        if (!firstPage) {
            firstPage = selected;
        }
    }
    if (!targetId.empty() || !targetUrlPrefix.empty() || !targetTitle.empty()) {
        return std::nullopt;
    }
    return firstPage;
}

std::vector<SelectedTarget> MatchingTargets(
    const json& targets,
    const std::string& targetId,
    const std::string& targetUrlPrefix,
    const std::string& targetTitle,
    const std::string& browserContextId
) {
    std::vector<SelectedTarget> matches;
    if (!targets.is_array()) return matches;
    for (const auto& target : targets) {
        if (!target.is_object() || target.value("type", "") != "page" ||
            !target.contains("webSocketDebuggerUrl") ||
            !target["webSocketDebuggerUrl"].is_string()) {
            continue;
        }
        const std::string id = target.value("id", "");
        const std::string url = target.value("url", "");
        const std::string title = target.value("title", "");
        if (!targetId.empty() && id != targetId) continue;
        if (!targetUrlPrefix.empty() && url.rfind(targetUrlPrefix, 0) != 0) continue;
        if (!targetTitle.empty() && title != targetTitle) continue;
        if (!browserContextId.empty() && target.value("browserContextId", "") != browserContextId) continue;
        matches.push_back({
            target["webSocketDebuggerUrl"].get<std::string>(),
            id,
            url,
            title,
            target.value("browserContextId", "")
        });
    }
    return matches;
}

bool SetSocketTimeouts(SocketHandle socket, int timeoutMs) {
    if (timeoutMs <= 0) {
        return true;
    }
#if defined(_WIN32)
    DWORD timeout = static_cast<DWORD>(timeoutMs);
    return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0 &&
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0;
#else
    timeval tv {};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

SocketOwner ConnectTcp(const std::string& host, int port, int timeoutMs) {
    SocketOwner owner;
    if (!EnsureSocketsInitialized()) {
        return owner;
    }
    addrinfo hints {};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* results = nullptr;
    std::string service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &results) != 0) {
        return owner;
    }
    for (addrinfo* item = results; item != nullptr; item = item->ai_next) {
        SocketHandle candidate = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (candidate == kInvalidSocket) {
            continue;
        }
        if (!SetSocketTimeouts(candidate, timeoutMs)) {
            CloseSocket(candidate);
            continue;
        }
        if (connect(candidate, item->ai_addr, static_cast<int>(item->ai_addrlen)) == 0) {
            owner.socket = candidate;
            break;
        }
        CloseSocket(candidate);
    }
    freeaddrinfo(results);
    return owner;
}

bool SendAll(SocketHandle socket, const uint8_t* data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
#if defined(_WIN32)
        int n = send(socket, reinterpret_cast<const char*>(data + sent), static_cast<int>(std::min<size_t>(size - sent, 64 * 1024)), 0);
#else
        ssize_t n = send(socket, data + sent, size - sent, 0);
#endif
        if (n <= 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool RecvExact(SocketHandle socket, uint8_t* data, size_t size) {
    size_t read = 0;
    while (read < size) {
#if defined(_WIN32)
        int n = recv(socket, reinterpret_cast<char*>(data + read), static_cast<int>(std::min<size_t>(size - read, 64 * 1024)), 0);
#else
        ssize_t n = recv(socket, data + read, size - read, 0);
#endif
        if (n <= 0) {
            return false;
        }
        read += static_cast<size_t>(n);
    }
    return true;
}

bool SendTextFrame(SocketHandle socket, const std::string& text) {
    std::vector<uint8_t> frame;
    frame.push_back(0x81);
    const uint64_t length = text.size();
    if (length < 126) {
        frame.push_back(static_cast<uint8_t>(0x80 | length));
    } else if (length <= 0xffff) {
        frame.push_back(0x80 | 126);
        frame.push_back(static_cast<uint8_t>((length >> 8) & 0xff));
        frame.push_back(static_cast<uint8_t>(length & 0xff));
    } else {
        frame.push_back(0x80 | 127);
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.push_back(static_cast<uint8_t>((length >> shift) & 0xff));
        }
    }
    std::array<uint8_t, 4> mask {};
    std::random_device random;
    for (auto& byte : mask) {
        byte = static_cast<uint8_t>(random());
    }
    frame.insert(frame.end(), mask.begin(), mask.end());
    for (size_t i = 0; i < text.size(); ++i) {
        frame.push_back(static_cast<uint8_t>(text[i]) ^ mask[i % mask.size()]);
    }
    return SendAll(socket, frame.data(), frame.size());
}

std::optional<std::string> ReceiveTextFrame(SocketHandle socket) {
    while (true) {
        uint8_t header[2] {};
        if (!RecvExact(socket, header, sizeof(header))) {
            return std::nullopt;
        }
        const uint8_t opcode = header[0] & 0x0f;
        uint64_t length = header[1] & 0x7f;
        const bool masked = (header[1] & 0x80) != 0;
        if (length == 126) {
            uint8_t extended[2] {};
            if (!RecvExact(socket, extended, sizeof(extended))) {
                return std::nullopt;
            }
            length = (static_cast<uint64_t>(extended[0]) << 8) | extended[1];
        } else if (length == 127) {
            uint8_t extended[8] {};
            if (!RecvExact(socket, extended, sizeof(extended))) {
                return std::nullopt;
            }
            length = 0;
            for (uint8_t byte : extended) {
                length = (length << 8) | byte;
            }
        }
        std::array<uint8_t, 4> mask {};
        if (masked && !RecvExact(socket, mask.data(), mask.size())) {
            return std::nullopt;
        }
        if (length > 32 * 1024 * 1024) {
            return std::nullopt;
        }
        std::vector<uint8_t> payload(static_cast<size_t>(length));
        if (length > 0 && !RecvExact(socket, payload.data(), payload.size())) {
            return std::nullopt;
        }
        if (masked) {
            for (size_t i = 0; i < payload.size(); ++i) {
                payload[i] ^= mask[i % mask.size()];
            }
        }
        if (opcode == 0x1) {
            return std::string(payload.begin(), payload.end());
        }
        if (opcode == 0x8) {
            return std::nullopt;
        }
        if (opcode == 0x9) {
            const std::array<uint8_t, 2> pong = {0x8a, 0x00};
            SendAll(socket, pong.data(), pong.size());
        }
    }
}

bool WebSocketUpgrade(SocketHandle socket, const ParsedWebSocketUrl& url) {
    const std::string key = "Y29tcHV0ZXIuY3BwLWNkcA==";
    std::ostringstream request;
    request << "GET " << url.path << " HTTP/1.1\r\n"
            << "Host: " << url.host << ":" << url.port << "\r\n"
            << "Upgrade: websocket\r\n"
            << "Connection: Upgrade\r\n"
            << "Sec-WebSocket-Key: " << key << "\r\n"
            << "Sec-WebSocket-Version: 13\r\n\r\n";
    std::string payload = request.str();
    if (!SendAll(socket, reinterpret_cast<const uint8_t*>(payload.data()), payload.size())) {
        return false;
    }
    std::string response;
    std::array<uint8_t, 1024> buffer {};
    while (response.find("\r\n\r\n") == std::string::npos && response.size() < 8192) {
#if defined(_WIN32)
        int n = recv(socket, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
#else
        ssize_t n = recv(socket, buffer.data(), buffer.size(), 0);
#endif
        if (n <= 0) {
            return false;
        }
        response.append(reinterpret_cast<const char*>(buffer.data()), static_cast<size_t>(n));
    }
    return response.rfind("HTTP/1.1 101", 0) == 0 || response.rfind("HTTP/1.0 101", 0) == 0;
}

bool TargetHasDocumentFocus(const SelectedTarget& target, int timeoutMs) {
    auto parsedUrl = ParseWebSocketUrl(target.webSocketUrl);
    if (!parsedUrl) return false;
    auto socket = ConnectTcp(parsedUrl->host, parsedUrl->port, timeoutMs);
    if (!socket.valid() || !WebSocketUpgrade(socket.socket, *parsedUrl)) return false;

    const json message = {
        {"id", 1},
        {"method", "Runtime.evaluate"},
        {"params", {
            {"expression", "document.hasFocus()"},
            {"returnByValue", true},
            {"awaitPromise", true},
            {"userGesture", false}
        }}
    };
    if (!SendTextFrame(socket.socket, message.dump())) return false;
    for (int i = 0; i < 100; ++i) {
        auto responseText = ReceiveTextFrame(socket.socket);
        if (!responseText) return false;
        const json response = json::parse(*responseText, nullptr, false);
        if (response.is_discarded() || response.value("id", 0) != 1) continue;
        if (response.contains("error")) return false;
        const json result = response.value("result", json::object());
        if (result.contains("exceptionDetails")) return false;
        const json remote = result.value("result", json::object());
        return remote.contains("value") && remote["value"].is_boolean() && remote["value"].get<bool>();
    }
    return false;
}

std::optional<SelectedTarget> ChooseFocusedTarget(
    const json& targets,
    const std::string& targetId,
    const std::string& targetUrlPrefix,
    const std::string& targetTitle,
    const std::string& browserContextId,
    int timeoutMs
) {
    for (const auto& candidate : MatchingTargets(
             targets, targetId, targetUrlPrefix, targetTitle, browserContextId)) {
        if (TargetHasDocumentFocus(candidate, timeoutMs)) return candidate;
    }
    return std::nullopt;
}

std::string ToLowerAscii(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool LooksMutatingScript(const std::string& script) {
    std::string lower = ToLowerAscii(script);
    const std::array<std::string_view, 11> blocked = {
        ".click(",
        "dispatchevent",
        ".focus(",
        ".blur(",
        ".submit(",
        "setattribute(",
        "removeattribute(",
        "appendchild(",
        "removechild(",
        "insertbefore(",
        "execcommand("
    };
    for (std::string_view needle : blocked) {
        if (lower.find(needle) != std::string::npos) {
            return true;
        }
    }
    lower.erase(std::remove_if(lower.begin(), lower.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }), lower.end());
    return lower.find(".value=") != std::string::npos ||
        lower.find(".checked=") != std::string::npos ||
        lower.find(".selected=") != std::string::npos;
}

std::optional<std::int64_t> CdpBrowserProcessId(
    const std::string& host,
    int port,
    int timeoutMs
) {
    const std::string versionBody = HttpGet(CdpBaseUrl(host, port) + "/json/version", timeoutMs);
    const json version = json::parse(versionBody, nullptr, false);
    if (version.is_discarded() || !version.contains("webSocketDebuggerUrl") ||
        !version["webSocketDebuggerUrl"].is_string()) {
        return std::nullopt;
    }
    auto parsedUrl = ParseWebSocketUrl(version["webSocketDebuggerUrl"].get<std::string>());
    if (!parsedUrl) return std::nullopt;
    auto socket = ConnectTcp(parsedUrl->host, parsedUrl->port, timeoutMs);
    if (!socket.valid() || !WebSocketUpgrade(socket.socket, *parsedUrl)) return std::nullopt;

    const json message = {
        {"id", 1},
        {"method", "SystemInfo.getProcessInfo"},
        {"params", json::object()}
    };
    if (!SendTextFrame(socket.socket, message.dump())) return std::nullopt;
    for (int i = 0; i < 100; ++i) {
        auto responseText = ReceiveTextFrame(socket.socket);
        if (!responseText) return std::nullopt;
        const json response = json::parse(*responseText, nullptr, false);
        if (response.is_discarded() || response.value("id", 0) != 1) continue;
        const json processes = response.value("result", json::object()).value("processInfo", json::array());
        if (!processes.is_array()) return std::nullopt;
        for (const auto& process : processes) {
            if (process.is_object() && process.value("type", "") == "browser" &&
                process.contains("id") && process["id"].is_number_integer()) {
                return process["id"].get<std::int64_t>();
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

json CdpEvaluate(
    const std::string& host,
    int port,
    const std::string& targetId,
    const std::string& targetUrlPrefix,
    const std::string& targetTitle,
    bool targetFocused,
    const std::string& browserContextId,
    const std::string& script,
    int timeoutMs
) {
    std::string targetsBody = HttpGet(CdpBaseUrl(host, port) + "/json/list", timeoutMs);
    if (targetsBody.empty()) {
        return Error("Chrome DevTools endpoint is not available on " + host + ":" + std::to_string(port), "browser_debug_unavailable");
    }
    json targets = json::parse(targetsBody, nullptr, false);
    if (targets.is_discarded()) {
        return Error("Chrome DevTools target list was not valid JSON", "browser_debug_invalid_response");
    }
    auto selectedTarget = targetFocused
        ? ChooseFocusedTarget(targets, targetId, targetUrlPrefix, targetTitle, browserContextId, timeoutMs)
        : ChooseTarget(targets, targetId, targetUrlPrefix, targetTitle, browserContextId);
    if (!selectedTarget) {
        return Error("no debuggable browser page target was found", "browser_target_not_found");
    }
    auto parsedUrl = ParseWebSocketUrl(selectedTarget->webSocketUrl);
    if (!parsedUrl) {
        return Error("Chrome DevTools target returned an unsupported WebSocket URL", "browser_debug_invalid_response");
    }
    auto socket = ConnectTcp(parsedUrl->host, parsedUrl->port, timeoutMs);
    if (!socket.valid()) {
        return Error("could not connect to Chrome DevTools WebSocket", "browser_debug_unavailable");
    }
    if (!WebSocketUpgrade(socket.socket, *parsedUrl)) {
        return Error("Chrome DevTools WebSocket upgrade failed", "browser_debug_unavailable");
    }

    json message = {
        {"id", 1},
        {"method", "Runtime.evaluate"},
        {"params", {
            {"expression", script},
            {"returnByValue", true},
            {"awaitPromise", true},
            {"userGesture", false}
        }}
    };
    if (!SendTextFrame(socket.socket, message.dump())) {
        return Error("could not send Chrome DevTools evaluation request", "browser_debug_unavailable");
    }
    for (int i = 0; i < 100; ++i) {
        auto responseText = ReceiveTextFrame(socket.socket);
        if (!responseText) {
            return Error("Chrome DevTools WebSocket closed before evaluation completed", "browser_debug_unavailable");
        }
        json response = json::parse(*responseText, nullptr, false);
        if (response.is_discarded() || response.value("id", 0) != 1) {
            continue;
        }
        if (response.contains("error")) {
            return Error(response["error"].value("message", "Chrome DevTools evaluation failed"), "browser_eval_failed");
        }
        const json result = response.value("result", json::object());
        if (result.contains("exceptionDetails")) {
            return Error("browser JavaScript evaluation threw an exception", "browser_eval_failed");
        }
        const json remote = result.value("result", json::object());
        json data = {
            {"backend", "cdp"},
            {"host", host},
            {"port", port},
            {"targetId", selectedTarget->id},
            {"targetUrl", selectedTarget->url},
            {"browserContextId", selectedTarget->browserContextId},
            {"targetUrlPrefix", targetUrlPrefix},
            {"targetTitle", targetTitle},
            {"targetFocused", targetFocused},
            {"type", remote.value("type", "")}
        };
        if (remote.contains("value")) {
            data["value"] = remote["value"];
        } else {
            data["value"] = remote.value("description", "");
        }
        if (targetUrlPrefix.empty()) {
            if (auto browserPid = CdpBrowserProcessId(host, port, timeoutMs)) {
                data["browserPid"] = *browserPid;
            }
        }
        return Ok(std::move(data));
    }
    return Error("Chrome DevTools evaluation timed out", "browser_eval_timeout");
}

} // namespace

ManagedBrowserSession ResolveManagedBrowserSession(
    const json& params,
    bool launch,
    bool includePid
) {
    ManagedBrowserSession session;
    std::string configError;
    const AppConfig config = LoadAppConfig(&configError);
    if (!configError.empty()) {
        session.code = "browser_config_invalid";
        session.error = configError;
        return session;
    }

    const bool explicitBrowser = params.contains("browser");
    const bool explicitProfile = params.contains("profile");
    const std::string requestedBrowser = explicitBrowser
        ? params.value("browser", "")
        : config.browser.defaultBrowser;
    session.browser = NormalizeBrowserId(requestedBrowser);
    session.profile = explicitProfile
        ? params.value("profile", "")
        : config.browser.profile;
    if (!IsSupportedBrowserId(session.browser)) {
        session.code = "browser_automation_unavailable";
        session.error = "browser '" + requestedBrowser +
            "' does not support managed Chromium automation";
        return session;
    }
    if (!IsValidBrowserProfileName(session.profile)) {
        session.code = "invalid_browser_profile";
        session.error = "browser profile must match [A-Za-z0-9][A-Za-z0-9._-]*";
        return session;
    }

    const BrowserDescriptor descriptor = DescribeBrowser(session.browser);
    session.applicationName = descriptor.applicationName;
    const char* envHost = std::getenv("COMPUTER_CPP_CHROME_CDP_HOST");
    const auto envPort = EnvPort();
    const bool explicitEndpoint = params.contains("host") || params.contains("port") ||
        (envHost && *envHost) || envPort.has_value();
    std::filesystem::path userDataDir =
        ManagedBrowserDataDir(session.browser, session.profile);
    const char* legacyUserDataDir =
        std::getenv("COMPUTER_CPP_CHROME_USER_DATA_DIR");
    const bool legacyUserDataOverride =
        session.browser == "chrome" && session.profile == "default" &&
        legacyUserDataDir && *legacyUserDataDir;
    if (!legacyUserDataOverride &&
        session.browser == config.browser.defaultBrowser &&
        session.profile == config.browser.profile &&
        !config.browser.userDataDir.empty()) {
        userDataDir = config.browser.userDataDir;
    }
    const std::string proxyServer =
        session.browser == config.browser.defaultBrowser &&
            session.profile == config.browser.profile
        ? config.browser.proxyServer
        : "";

    if (explicitEndpoint) {
        session.managed = false;
        session.host = params.contains("host")
            ? params.value("host", "127.0.0.1")
            : envHost && *envHost ? std::string(envHost) : "127.0.0.1";
        session.port = params.contains("port")
            ? params.value("port", 9222)
            : envPort.value_or(9222);
        if (session.host != "127.0.0.1" && session.host != "localhost" &&
            session.host != "::1") {
            session.code = "invalid_browser_eval";
            session.error = "browser inspection only supports loopback Chrome DevTools hosts";
            return session;
        }
        if (ProbeCdp(session.host, session.port)) {
            session.ok = true;
        } else if (!launch) {
            session.code = "browser_debug_unavailable";
            session.error = "Chrome DevTools endpoint is not available on " +
                session.host + ":" + std::to_string(session.port);
            return session;
        } else if (!descriptor.installed) {
            session.code = "browser_automation_unavailable";
            session.error = descriptor.displayName +
                " is not installed; choose an installed browser in ComputerCpp settings";
            return session;
        } else {
            PrepareManagedBrowserDataDir(userDataDir);
            BrowserLaunchLock lock(userDataDir / ".computer-cpp-launch.lock");
            if (!lock.valid()) {
                session.code = "browser_launch_failed";
                session.error = "could not lock managed browser profile '" + session.profile + "'";
                return session;
            }
            if (ProbeCdp(session.host, session.port)) {
                session.ok = true;
            } else {
                if (!LaunchBrowserForCdp(
                        descriptor, userDataDir, proxyServer, session.port)) {
                    session.code = "browser_launch_failed";
                    session.error = "could not launch " + descriptor.displayName;
                    return session;
                }
                for (int attempt = 0; attempt < 100; ++attempt) {
                    if (ProbeCdp(session.host, session.port)) {
                        session.ok = true;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        }
    } else {
        session.managed = true;
        session.host = "127.0.0.1";
        if (auto activePort = ReadActivePort(userDataDir);
            activePort && ProbeCdp(session.host, *activePort)) {
            session.port = *activePort;
            session.ok = true;
        } else if (!launch) {
            session.code = "browser_debug_unavailable";
            session.error = "managed " + descriptor.displayName +
                " profile '" + session.profile + "' is not running";
            return session;
        } else if (!descriptor.installed) {
            session.code = "browser_automation_unavailable";
            session.error = descriptor.displayName +
                " is not installed; choose an installed browser in ComputerCpp settings";
            return session;
        } else {
            PrepareManagedBrowserDataDir(userDataDir);
            BrowserLaunchLock lock(userDataDir / ".computer-cpp-launch.lock");
            if (!lock.valid()) {
                session.code = "browser_launch_failed";
                session.error = "could not lock managed browser profile '" + session.profile + "'";
                return session;
            }
            if (auto activePort = ReadActivePort(userDataDir);
                activePort && ProbeCdp(session.host, *activePort)) {
                session.port = *activePort;
                session.ok = true;
            } else {
                std::error_code ec;
                std::filesystem::remove(userDataDir / "DevToolsActivePort", ec);
                if (!LaunchBrowserForCdp(descriptor, userDataDir, proxyServer, 0)) {
                    session.code = "browser_launch_failed";
                    session.error = "could not launch " + descriptor.displayName;
                    return session;
                }
                for (int attempt = 0; attempt < 100; ++attempt) {
                    auto activePort = ReadActivePort(userDataDir);
                    if (activePort && ProbeCdp(session.host, *activePort)) {
                        session.port = *activePort;
                        session.ok = true;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        }
    }

    if (!session.ok) {
        session.code = "browser_debug_unavailable";
        session.error = "Chrome DevTools did not become available for " +
            descriptor.displayName + " profile '" + session.profile + "'";
        return session;
    }
    if (includePid) {
        if (auto pid = CdpBrowserProcessId(session.host, session.port, 2000)) {
            session.pid = static_cast<int>(*pid);
        }
    }
    return session;
}

json RunBrowserEvalCommand(const json& params) {
    if (auto unknown = UnknownParam(params, {
            "script",
            "targetId",
            "targetUrlPrefix",
            "targetTitle",
            "targetFocused",
            "browserContextId",
            "browser",
            "profile",
            "host",
            "port",
            "launch",
            "timeoutMs",
            "readOnly",
            "controlScope",
            "controlSession",
            "controlSessionToken"
        })) {
        return Error("unknown browser_eval parameter: " + *unknown, "invalid_browser_eval");
    }
    auto script = StringParam(params, "script", "");
    auto targetId = StringParam(params, "targetId", "");
    auto targetUrlPrefix = StringParam(params, "targetUrlPrefix", "");
    auto targetTitle = StringParam(params, "targetTitle", "");
    auto targetFocused = BoolParam(params, "targetFocused", false);
    auto browserContextId = StringParam(params, "browserContextId", "");
    auto browser = StringParam(params, "browser", "");
    auto profile = StringParam(params, "profile", "");
    auto host = StringParam(params, "host", "");
    auto port = IntParam(params, "port", 0);
    auto launch = BoolParam(params, "launch", true);
    auto timeoutMs = IntParam(params, "timeoutMs", 5000);
    auto readOnly = BoolParam(params, "readOnly", true);
    if (!script || !targetId || !targetUrlPrefix || !targetTitle || !targetFocused || !browserContextId || !browser || !profile || !host || !port || !launch || !timeoutMs || !readOnly) {
        return Error("browser_eval requires string script/targetId/targetUrlPrefix/targetTitle/browserContextId/browser/profile/host, integer port/timeoutMs, and boolean targetFocused/launch/readOnly", "invalid_browser_eval");
    }
    if (IsBlank(*script)) {
        return Error("browser_eval script must be non-empty", "invalid_browser_eval");
    }
    if (params.contains("browser") && IsBlank(*browser)) {
        return Error("browser_eval browser must be non-empty when provided", "invalid_browser_eval");
    }
    if (params.contains("profile") && !IsValidBrowserProfileName(*profile)) {
        return Error("browser_eval profile must match [A-Za-z0-9][A-Za-z0-9._-]*", "invalid_browser_eval");
    }
    if (params.contains("host") && *host != "127.0.0.1" && *host != "localhost" && *host != "::1") {
        return Error("browser_eval only supports loopback Chrome DevTools hosts", "invalid_browser_eval");
    }
    if (params.contains("port") && (*port <= 0 || *port > 65535)) {
        return Error("browser_eval port must be 1..65535", "invalid_browser_eval");
    }
    if (*timeoutMs < 500 || *timeoutMs > 30000) {
        return Error("browser_eval timeoutMs must be 500..30000", "invalid_browser_eval");
    }
    if (!*readOnly) {
        return Error("browser_eval only supports read-only inspection scripts", "invalid_browser_eval");
    }
    if (LooksMutatingScript(*script)) {
        return Error("browser_eval rejected a script that appears to mutate browser/UI state", "invalid_browser_eval");
    }
    const ManagedBrowserSession session = ResolveManagedBrowserSession(params, *launch);
    if (!session.ok) {
        return Error(session.error, session.code);
    }
    json response = CdpEvaluate(session.host, session.port, *targetId,
        *targetUrlPrefix, *targetTitle, *targetFocused, *browserContextId,
        *script, *timeoutMs);
    if (response.value("ok", false) && response.contains("data")) {
        response["data"]["browser"] = session.browser;
        response["data"]["profile"] = session.profile;
        response["data"]["managed"] = session.managed;
        if (session.pid > 0) response["data"]["browserPid"] = session.pid;
    }
    return response;
}

} // namespace ComputerCpp
