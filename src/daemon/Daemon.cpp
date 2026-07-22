#include "computer_cpp/Daemon.h"

#include "computer_cpp/AppPaths.h"
#include "computer_cpp/InferenceClient.h"
#include "computer_cpp/StringUtils.h"
#include "computer_cpp/WindowsUtil.h"

#include "DaemonArtifacts.h"
#include "DaemonBatch.h"
#include "DaemonBrowser.h"
#include "DaemonControlSession.h"
#include "DaemonDesktop.h"
#include "DaemonIdle.h"
#include "DaemonInput.h"
#include "DaemonProtocol.h"
#include "DaemonJson.h"
#include "DaemonImage.h"
#include "DaemonMetadata.h"
#include "DaemonObservation.h"
#include "DaemonSocket.h"
#include "DaemonScreenshot.h"
#include "DaemonSnapshot.h"
#include "DaemonTargetCommand.h"
#include "DaemonTargetRefs.h"
#include "DaemonTargetResolve.h"
#include "DaemonTargetText.h"
#include "DaemonParsing.h"

#include <algorithm>
#include <array>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

using json = nlohmann::json;

namespace ComputerCpp {
namespace {

using ParamsHandler = json (*)(const json&);
using NoParamsHandler = json (*)();

struct ParamsRoute {
    std::string_view method;
    ParamsHandler run;
};

struct NoParamsRoute {
    std::string_view method;
    NoParamsHandler run;
};

constexpr auto kParamsRoutes = std::to_array<ParamsRoute>({
    {"permissions", RunPermissionsCommand},
    {"open_permissions", RunOpenPermissionsCommand},
    {"window_bounds", RunWindowBoundsCommand},
    {"window_list", RunWindowListCommand},
    {"app_activate_pid", RunAppActivatePidCommand},
    {"screenshot", RunScreenshotCommand},
    {"image_info", RunImageInfo},
    {"image_split", RunImageSplit},
    {"llm_chat", Inference::ChatCompletion},
    {"mouse_down", RunMouseDownCommand},
    {"mouse_up", RunMouseUpCommand},
    {"wait", RunWaitCommand},
    {"press", RunPressCommand},
    {"clipboard_write", RunClipboardWriteCommand},
    {"browser_eval", RunBrowserEvalCommand},
});

constexpr auto kNoParamsRoutes = std::to_array<NoParamsRoute>({
    {"window_active", RunWindowActiveCommand},
    {"app_active", RunAppActiveCommand},
    {"clipboard_read", RunClipboardReadCommand},
    {"clipboard_paste", RunClipboardPasteCommand},
});

std::optional<json> RunParamsRoute(std::string_view method, const json& params) {
    for (const auto& route : kParamsRoutes) {
        if (method == route.method) {
            return route.run(params);
        }
    }
    return std::nullopt;
}

std::optional<json> RunNoParamsRoute(std::string_view method) {
    for (const auto& route : kNoParamsRoutes) {
        if (method == route.method) {
            return route.run();
        }
    }
    return std::nullopt;
}

} // namespace

json HandleDaemonRequest(const std::string& session, const json& request) {
    ScopedControlActivity activity;

    try {
        if (!request.is_object()) {
            return Error("request must be an object", "bad_request");
        }
        const auto finish = [&](json response) {
            if (request.contains("id") && request.at("id").is_string()) {
                response["id"] = request["id"];
            }
            return response;
        };
        if (auto unknown = UnknownParam(request, {"id", "method", "params", "controlSession", "controlSessionToken"})) {
            if (*unknown == "controlScope") {
                return finish(Error("request controlScope must be provided inside params", "bad_request"));
            }
            return finish(Error("unknown request parameter: " + *unknown, "bad_request"));
        }
        if (request.contains("id") && !request.at("id").is_string()) {
            return Error("request id must be a string", "bad_request");
        }
        if (request.contains("id") && IsBlank(request.at("id").get<std::string>())) {
            return Error("request id must be non-empty", "bad_request");
        }
        auto methodParam = StringParam(request, "method", "");
        if (!methodParam) {
            return finish(Error("request method must be a string", "bad_request"));
        }
        std::string method = *methodParam;
        if (IsBlank(method)) {
            return finish(Error("request method must be non-empty", "bad_request"));
        }
        json params = request.contains("params") ? request.at("params") : json::object();
        if (!params.is_object()) {
            return finish(Error("request params must be an object", "bad_request"));
        }
        if (auto tokenError = InvalidControlSessionTokenError(request, params)) {
            return finish(Error(*tokenError, "invalid_control_session"));
        }
        if (auto scopeError = InvalidControlScopeError(params)) {
            return finish(Error(*scopeError, "invalid_control_session"));
        }

        if (method == "ping") {
            return finish(Ok({{"session", session}, {"message", "pong"}}));
        }

        if (method == "capabilities") {
            return finish(Ok(CapabilitiesJson()));
        }

        if (method == "schema") {
            return finish(Ok(SchemaJson()));
        }

        if (IsControlSessionCommand(method)) {
            return finish(RunControlSessionCommand(session, method, params));
        }

        if (method == "batch") {
            return finish(RunBatchCommand(session, request, params, HandleDaemonRequest));
        }

        if (method == "shutdown") {
            RequestDaemonStop();
            return finish(Ok({{"stopping", true}}));
        }

        // Pure observation and delay calls do not manipulate desktop state and
        // can safely run without holding the exclusive input lease.
        if (method == "browser_eval" &&
            params.contains("launch") && params["launch"].is_boolean() &&
            !params["launch"].get<bool>()) {
            return finish(RunBrowserEvalCommand(params));
        }
        if (method == "wait" &&
            params.contains("delayMs") && params["delayMs"].is_number_integer() &&
            !params.contains("frontmost") && !params.contains("stableScreenMs")) {
            return finish(RunWaitCommand(params));
        }

        auto controlGate = RequireControlSessionForRequest(request, method, params);
        if (!controlGate.value("ok", false)) {
            return finish(controlGate);
        }
        std::string activeControlToken = ControlSessionTokenFromRequest(request, params);

        if (auto routed = RunParamsRoute(method, params)) {
            return finish(*routed);
        }
        if (auto routed = RunNoParamsRoute(method)) {
            return finish(*routed);
        }

        if (method == "state") return finish(RunStateCommand(session));
        if (method == "window_close") return finish(RunWindowCloseCommand(params, activeControlToken));
        if (method == "app_launch") return finish(RunAppLaunchCommand(params, activeControlToken));
        if (method == "open_url") return finish(RunOpenUrlCommand(params, activeControlToken));

        if (IsObservationCommand(method)) {
            return finish(RunObservationCommand(session, method, params));
        }

        if (IsTargetCommand(method)) {
            return finish(RunTargetCommand(session, method, params));
        }

        if (method == "get") {
            return finish(RunGetCommand(session, params));
        }

        if (method == "snapshot") {
            return finish(RunSnapshotCommand(session, params));
        }

        if (method == "click") return finish(RunClickCommand(session, params));
        if (method == "mouse_move") return finish(RunMouseMoveCommand(session, params, controlGate));
        if (method == "mouse_drag") return finish(RunMouseDragCommand(session, params, controlGate));
        if (method == "scroll") return finish(RunScrollCommand(session, params, controlGate));
        if (method == "type") return finish(RunTypeCommand(session, params));

        return finish(Error("unknown method: " + method, "unknown_method"));
    } catch (const std::exception& e) {
        return Error(e.what(), "exception");
    }
}

int RunDaemon(const DaemonOptions& options) {
    if (!IsSessionNameValid(options.session)) {
        std::cerr << "Invalid session name: " << options.session << std::endl;
        return 2;
    }

#if defined(__unix__) || defined(__APPLE__)
    auto socketPath = SocketPathForSession(options.session);
    auto pidPath = PidPathForSession(options.session);
    std::filesystem::remove(socketPath);
    std::ofstream(pidPath) << getpid();
    ::signal(SIGPIPE, SIG_IGN);

    int serverFd = ::socket(AF_UNIX, SOCK_STREAM
#if defined(SOCK_CLOEXEC)
        | SOCK_CLOEXEC
#endif
        , 0);
    if (serverFd < 0) {
        std::perror("socket");
        return 1;
    }
    SetCloseOnExec(serverFd);

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::string pathString = socketPath.string();
    if (pathString.size() >= sizeof(addr.sun_path)) {
        std::cerr << "Socket path is too long: " << pathString << std::endl;
        ::close(serverFd);
        return 1;
    }
    std::strncpy(addr.sun_path, pathString.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(serverFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        ::close(serverFd);
        return 1;
    }
    ::chmod(pathString.c_str(), 0600);

    if (::listen(serverFd, 32) < 0) {
        std::perror("listen");
        ::close(serverFd);
        return 1;
    }

    ResetDaemonStopState();
    std::thread idleThread = StartIdleBehaviorThreadIfEnabled();
    while (!DaemonShouldStop()) {
        int clientFd =
#if defined(__linux__) && defined(SOCK_CLOEXEC)
            ::accept4(serverFd, nullptr, nullptr, SOCK_CLOEXEC);
#else
            ::accept(serverFd, nullptr, nullptr);
#endif
        if (clientFd < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("accept");
            break;
        }
        SetCloseOnExec(clientFd);

        std::string line = ReadLineFromFd(clientFd);
        if (line.empty()) {
            ::close(clientFd);
            continue;
        }

        json request = json::parse(line, nullptr, false);
        json response;
        if (request.is_discarded()) {
            response = Error("invalid request: malformed JSON", "bad_request");
        } else {
            response = HandleDaemonRequest(options.session, request);
        }
        WriteJsonLineToFd(clientFd, response);
        ::close(clientFd);
    }

    RequestDaemonStop();
    if (idleThread.joinable()) {
        idleThread.join();
    }
    ::close(serverFd);
    std::filesystem::remove(socketPath);
    std::filesystem::remove(pidPath);
    return 0;
#elif defined(_WIN32)
    auto pidPath = PidPathForSession(options.session);
    std::ofstream(pidPath) << GetCurrentProcessId();
    std::wstring pipeName = ComputerCpp::Windows::Utf8ToWide(PipeNameForSession(options.session));

    ResetDaemonStopState();
    SetDaemonStopNotifier([pipeName]() {
        HANDLE pipe = CreateFileW(
            pipeName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe);
        }
    });
    std::thread idleThread = StartIdleBehaviorThreadIfEnabled();
    while (!DaemonShouldStop()) {
        HANDLE pipe = CreateNamedPipeW(
            pipeName.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            64 * 1024,
            64 * 1024,
            0,
            nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            std::cerr << "CreateNamedPipe failed: " << GetLastError() << std::endl;
            break;
        }

        BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected) {
            CloseHandle(pipe);
            continue;
        }

        std::string line = ReadLineFromPipe(pipe);
        if (!line.empty()) {
            json request = json::parse(line, nullptr, false);
            json response;
            if (request.is_discarded()) {
                response = Error("invalid request: malformed JSON", "bad_request");
            } else {
                response = HandleDaemonRequest(options.session, request);
            }
            WriteJsonLineToPipe(pipe, response);
        }
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }

    SetDaemonStopNotifier({});
    RequestDaemonStop();
    if (idleThread.joinable()) {
        idleThread.join();
    }
    std::filesystem::remove(pidPath);
    return 0;
#else
    std::cerr << "Daemon transport is not implemented on this platform yet." << std::endl;
    return 8;
#endif
}

}
