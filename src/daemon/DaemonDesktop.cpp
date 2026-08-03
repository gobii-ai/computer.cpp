#include "DaemonDesktop.h"

#include "DaemonBrowser.h"

#include "computer_cpp/AppConfig.h"
#include "computer_cpp/Browser.h"
#include "computer_cpp/ControlSession.h"
#include "computer_cpp/Platform.h"
#include "computer_cpp/StringUtils.h"

#include "DaemonJson.h"
#include "DaemonMetadata.h"
#include "DaemonParsing.h"
#include "DaemonProtocol.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <set>
#include <thread>

namespace ComputerCpp {

namespace {

using json = nlohmann::json;

bool WindowIdVisible(const std::string& id) {
    if (id.empty()) {
        return false;
    }
    for (const auto& window : Platform::ListWindows("")) {
        if (window.id == id) {
            return true;
        }
    }
    return false;
}

Platform::WindowInfo WaitForOpenedWindow(const std::string& appQuery, const std::set<std::string>& beforeIds) {
    Platform::WindowInfo fallback;
    for (int attempt = 0; attempt < 40; ++attempt) {
        auto windows = Platform::ListWindows(appQuery);
        for (const auto& window : windows) {
            if (window.available && window.active && !window.id.empty() && beforeIds.count(window.id) == 0) {
                return window;
            }
        }
        for (const auto& window : windows) {
            if (window.available && !window.id.empty() && beforeIds.count(window.id) == 0) {
                return window;
            }
        }
        for (const auto& window : windows) {
            if (window.available && window.active && fallback.id.empty()) {
                fallback = window;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(125));
    }
    if (fallback.available) {
        return fallback;
    }
    if (!appQuery.empty()) {
        return {};
    }
    return Platform::GetActiveWindow();
}

std::vector<Platform::WindowInfo> BrowserWindowsForPid(int pid) {
    std::vector<Platform::WindowInfo> matches;
    for (auto& window : Platform::ListWindows("")) {
        if (window.pid == pid) matches.push_back(std::move(window));
    }
    return matches;
}

Platform::WindowInfo WaitForBrowserWindow(
    int pid,
    const std::set<std::string>& beforeIds,
    bool requireNew
) {
    Platform::WindowInfo fallback;
    for (int attempt = 0; attempt < 40; ++attempt) {
        for (const auto& window : BrowserWindowsForPid(pid)) {
            if (!window.available || window.id.empty()) continue;
            if (beforeIds.count(window.id) == 0) {
                if (window.active) return window;
                if (fallback.id.empty()) fallback = window;
            } else if (!requireNew && window.active) {
                return window;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(125));
    }
    return requireNew ? Platform::WindowInfo{} : fallback;
}

bool WaitForActivePid(int pid) {
    if (pid <= 0) return false;
    for (int attempt = 0; attempt < 70; ++attempt) {
        if (Platform::GetFrontmostAppPid() == pid) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

bool IsHttpUrl(const std::string& url) {
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

json DesktopSessionToJson(const Platform::DesktopSessionState& state) {
    const bool ready = IsDesktopSessionReady(state);
    std::string status = "ready";
    if (!state.detectionSupported) {
        status = "unsupported";
    } else if (!state.available || !state.onConsole || !state.loginDone) {
        status = "unavailable";
    } else if (state.screenLocked) {
        status = "locked";
    } else if (state.displayAsleep) {
        status = "display_asleep";
    } else if (state.screenSaverActive) {
        status = "screensaver";
    }
    return {
        {"detectionSupported", state.detectionSupported},
        {"available", state.available},
        {"onConsole", state.onConsole},
        {"loginDone", state.loginDone},
        {"screenLocked", state.screenLocked},
        {"screenSaverActive", state.screenSaverActive},
        {"displayAsleep", state.displayAsleep},
        {"ready", ready},
        {"status", status}
    };
}

} // namespace

std::set<std::string> VisibleWindowIds(const std::vector<Platform::WindowInfo>& windows) {
    std::set<std::string> ids;
    for (const auto& window : windows) {
        if (window.available && !window.id.empty()) {
            ids.insert(window.id);
        }
    }
    return ids;
}

bool IsDesktopSessionReady(const Platform::DesktopSessionState& state) {
    return state.detectionSupported &&
        state.available &&
        state.onConsole &&
        state.loginDone &&
        !state.screenLocked &&
        !state.screenSaverActive &&
        !state.displayAsleep;
}

bool CanAttemptDesktopWake(const Platform::DesktopSessionState& state, bool force) {
    return state.detectionSupported &&
        state.available &&
        state.onConsole &&
        state.loginDone &&
        (!state.screenLocked || state.screenSaverActive || force);
}

bool IsPermissionsPane(const std::string& pane) {
    return pane == "accessibility" ||
        pane == "screen" ||
        pane == "screen-capture" ||
        pane == "screen-recording";
}

json RunPermissionsCommand(const json& params) {
    if (auto unknown = UnknownParam(params, {"request", "controlSession", "controlSessionToken", "controlScope"})) {
        return Error("unknown permissions parameter: " + *unknown, "invalid_permissions");
    }
    auto requestMissing = BoolParam(params, "request", false);
    if (!requestMissing) {
        return Error("permissions request must be boolean", "invalid_permissions");
    }
    return Ok(PermissionToJson(Platform::CheckPermissions(*requestMissing)));
}

json RunOpenPermissionsCommand(const json& params) {
    if (auto unknown = UnknownParam(params, {"pane", "controlSession", "controlSessionToken", "controlScope"})) {
        return Error("unknown open_permissions parameter: " + *unknown, "invalid_permissions");
    }
    auto paneParam = StringParam(params, "pane", "accessibility");
    if (!paneParam) {
        return Error("open_permissions pane must be a string", "invalid_permissions");
    }
    std::string pane = *paneParam;
    if (params.contains("pane") && IsBlank(pane)) {
        return Error("open_permissions pane must be non-empty", "invalid_permissions");
    }
    if (!IsPermissionsPane(pane)) {
        return Error("open_permissions pane must be accessibility, screen, screen-capture, or screen-recording", "invalid_permissions");
    }
    bool opened = false;
    if (pane == "screen" || pane == "screen-capture" || pane == "screen-recording") {
        opened = Platform::OpenScreenCaptureSettings();
        pane = "screen";
    } else {
        opened = Platform::OpenAccessibilitySettings();
        pane = "accessibility";
    }
    return Ok({{"opened", opened}, {"pane", pane}});
}

json RunStateCommand(const std::string& session) {
    int width = 0;
    int height = 0;
    double cursorX = 0.0;
    double cursorY = 0.0;
    Platform::GetScreenSize(width, height);
    Platform::GetCursorPosition(cursorX, cursorY);
    return Ok({
        {"session", session},
        {"permissions", PermissionToJson(Platform::CheckPermissions(false))},
        {"desktopSession", DesktopSessionToJson(Platform::GetDesktopSessionState())},
        {"frontmostApp", AppToJson(Platform::GetFrontmostApp())},
        {"focusedElement", FocusedToJson(Platform::GetFocusedElementInfo())},
        {"frontmostWindowBounds", BoundsToJson(Platform::GetFrontmostWindowBounds())},
        {"screen", {{"width", width}, {"height", height}}},
        {"cursor", {{"x", cursorX}, {"y", cursorY}}}
    });
}

json RunDesktopSessionStateCommand() {
    return Ok({
        {"session", DesktopSessionToJson(Platform::GetDesktopSessionState())},
        {"frontmostApp", AppToJson(Platform::GetFrontmostApp())}
    });
}

json RunDesktopWakeCommand(const json& params) {
    if (auto unknown = UnknownParam(params, {
        "force", "controlSession", "controlSessionToken", "controlScope"
    })) {
        return Error("unknown desktop_wake parameter: " + *unknown, "invalid_desktop");
    }
    auto forceParam = BoolParam(params, "force", false);
    if (!forceParam) {
        return Error("desktop_wake force must be boolean", "invalid_desktop");
    }
    const bool force = *forceParam;
    const Platform::DesktopSessionState before = Platform::GetDesktopSessionState();
    if (!before.detectionSupported) {
        return Error("desktop session detection and wake are not supported on this platform",
            "desktop_session_unsupported");
    }
    if (!before.available || !before.onConsole || !before.loginDone) {
        return Error("desktop GUI session is unavailable", "desktop_session_unavailable");
    }
    if (!CanAttemptDesktopWake(before, force)) {
        return Error("desktop session is locked and requires manual unlock", "desktop_locked");
    }

    const bool alreadyReady = !force && IsDesktopSessionReady(before);
    const bool wakeSignalSent = alreadyReady ? false : Platform::WakeDesktopSession(force);
    Platform::DesktopSessionState after = before;
    if (!alreadyReady && wakeSignalSent) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            after = Platform::GetDesktopSessionState();
            if (IsDesktopSessionReady(after)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } while (std::chrono::steady_clock::now() < deadline);
    }
    if (after.screenLocked) {
        return Error("desktop woke to a lock screen and requires manual unlock", "desktop_locked");
    }
    return Ok({
        {"wakeRequested", !alreadyReady},
        {"forced", force},
        {"wakeSignalSent", wakeSignalSent},
        {"ready", IsDesktopSessionReady(after)},
        {"before", DesktopSessionToJson(before)},
        {"after", DesktopSessionToJson(after)},
        {"frontmostApp", AppToJson(Platform::GetFrontmostApp())}
    });
}

json EnsureDesktopSessionReadyForNativeControl() {
    const Platform::DesktopSessionState state =
        Platform::GetDesktopSessionState();
    if (!state.detectionSupported) {
        return Ok({
            {"ready", true},
            {"wakeRequested", false},
            {"session", DesktopSessionToJson(state)}
        });
    }
    if (IsDesktopSessionReady(state)) {
        return Ok({
            {"ready", true},
            {"wakeRequested", false},
            {"session", DesktopSessionToJson(state)}
        });
    }

    json wake = RunDesktopWakeCommand(json::object());
    if (!wake.value("ok", false)) return wake;
    if (!wake.value("data", json::object()).value("ready", false)) {
        return Error(
            "native user activity did not make the desktop session ready",
            "desktop_wake_failed");
    }
    return wake;
}

json RunWindowBoundsCommand(const json& params) {
    if (auto unknown = UnknownParam(params, {
        "x", "y", "width", "height", "pid", "controlSession", "controlSessionToken", "controlScope"
    })) {
        return Error("unknown window_bounds parameter: " + *unknown, "invalid_window");
    }
    Platform::Bounds bounds;
    bounds.available = true;
    auto x = NumberParam(params, "x", 0.0);
    auto y = NumberParam(params, "y", 0.0);
    auto width = NumberParam(params, "width", 0.0);
    auto height = NumberParam(params, "height", 0.0);
    auto pidParam = IntParam(params, "pid", 0);
    if (!x || !y || !width || !height || !pidParam) {
        return Error("window_bounds requires numeric x/y/width/height and integer pid", "invalid_window");
    }
    if (*width <= 0.0 || *height <= 0.0) {
        return Error("window_bounds requires positive width and height", "invalid_window");
    }
    if (*pidParam < 0) {
        return Error("window_bounds pid must be non-negative", "invalid_window");
    }
    bounds.x = *x;
    bounds.y = *y;
    bounds.width = *width;
    bounds.height = *height;
    int pid = *pidParam;
    bool applied = pid > 0
        ? Platform::SetWindowBoundsForPid(pid, bounds)
        : Platform::SetFrontmostWindowBounds(bounds);
    return Ok({
        {"applied", applied},
        {"pid", pid},
        {"requested", BoundsToJson(bounds)},
        {"actual", BoundsToJson(Platform::GetFrontmostWindowBounds())}
    });
}

json RunWindowActiveCommand() {
    return Ok({{"window", WindowToJson(Platform::GetActiveWindow())}});
}

json RunWindowListCommand(const json& params) {
    if (auto unknown = UnknownParam(params, {"app", "controlSession", "controlSessionToken", "controlScope"})) {
        return Error("unknown window_list parameter: " + *unknown, "invalid_window");
    }
    auto app = StringParam(params, "app", "");
    if (!app) {
        return Error("window_list app must be a string", "invalid_window");
    }
    if (params.contains("app") && IsBlank(*app)) {
        return Error("window_list app must be non-empty when provided", "invalid_window");
    }
    json windows = json::array();
    for (const auto& window : Platform::ListWindows(*app)) {
        windows.push_back(WindowToJson(window));
    }
    return Ok({{"windows", windows}});
}

json RunWindowActivateCommand(const json& params) {
    if (auto unknown = UnknownParam(params, {
            "id", "controlSession", "controlSessionToken", "controlScope"
        })) {
        return Error("unknown window_activate parameter: " + *unknown, "invalid_window");
    }
    auto idParam = StringParam(params, "id", "");
    if (!idParam) {
        return Error("window_activate id must be a string", "invalid_window");
    }
    const std::string id = *idParam;
    if (IsBlank(id)) {
        return Error("window_activate id must be non-empty", "invalid_window");
    }
    Platform::WindowInfo target;
    for (const auto& window : Platform::ListWindows("")) {
        if (window.id == id) {
            target = window;
            break;
        }
    }
    if (!target.available) {
        return Ok({{"found", false}, {"activated", false}, {"id", id}});
    }
    json desktopReady = EnsureDesktopSessionReadyForNativeControl();
    if (!desktopReady.value("ok", false)) return desktopReady;
    if (!Platform::ActivateWindow(id)) {
        return Error("could not activate window", "window_activate_failed");
    }
    return Ok({
        {"found", true},
        {"activated", true},
        {"window", WindowToJson(Platform::GetActiveWindow())}
    });
}

json RunWindowCloseCommand(const json& params, const std::string& activeControlToken) {
    if (auto unknown = UnknownParam(params, {"id", "frontmost", "controlSession", "controlSessionToken", "controlScope"})) {
        return Error("unknown window_close parameter: " + *unknown, "invalid_window");
    }
    auto idParam = StringParam(params, "id", "");
    auto frontmost = BoolParam(params, "frontmost", true);
    if (!idParam || !frontmost) {
        return Error("window_close requires string id and boolean frontmost", "invalid_window");
    }
    std::string id = *idParam;
    if (params.contains("id") && IsBlank(id)) {
        return Error("window_close id must be non-empty", "invalid_window");
    }
    Platform::WindowInfo target;
    if (id.empty() && *frontmost) {
        target = Platform::GetActiveWindow();
        id = target.id;
    } else if (!id.empty()) {
        for (const auto& window : Platform::ListWindows("")) {
            if (window.id == id) {
                target = window;
                break;
            }
        }
    }
    if (id.empty() || !target.available) {
        return Ok({{"found", false}, {"closed", false}, {"id", id}});
    }
    bool closed = Platform::CloseWindow(id);
    if (!closed) {
        if (!WindowIdVisible(id)) {
            ReleaseControlSessionResource(activeControlToken, "window", id);
            return Ok({{"found", true}, {"closed", true}, {"window", WindowToJson(target)}, {"verifiedAbsentAfterClose", true}});
        }
        return Error("could not close window", "window_close_failed");
    }
    ReleaseControlSessionResource(activeControlToken, "window", id);
    return Ok({{"found", true}, {"closed", true}, {"window", WindowToJson(target)}});
}

json RunAppLaunchCommand(const json& params, const std::string& activeControlToken) {
    if (auto unknown = UnknownParam(params, {"query", "controlSession", "controlSessionToken", "controlScope"})) {
        return Error("unknown app_launch parameter: " + *unknown, "invalid_app");
    }
    Platform::AppInfo app;
    auto queryParam = StringParam(params, "query", "");
    if (!queryParam) {
        return Error("app_launch query must be a string", "invalid_app");
    }
    std::string query = *queryParam;
    if (IsBlank(query)) {
        return Error("app_launch query must be non-empty", "invalid_app");
    }
    auto beforeIds = VisibleWindowIds(Platform::ListWindows(query));
    bool launched = Platform::LaunchOrActivateApp(query, app);
    if (!launched) {
        return Error("could not launch or activate app", "app_launch_failed");
    }
    auto activeWindow = WaitForOpenedWindow(query, beforeIds);
    if (activeWindow.available && !activeWindow.id.empty()) {
        RegisterControlSessionResource(activeControlToken, "window", activeWindow.id, app.name, WindowToJson(activeWindow));
    }
    return Ok({{"launched", launched}, {"app", AppToJson(app)}, {"window", WindowToJson(activeWindow)}});
}

json RunAppActivatePidCommand(const json& params) {
    if (auto unknown = UnknownParam(params, {"pid", "controlSession", "controlSessionToken", "controlScope"})) {
        return Error("unknown app_activate_pid parameter: " + *unknown, "invalid_app");
    }
    auto pidParam = IntParam(params, "pid", 0);
    if (!pidParam) {
        return Error("app_activate_pid requires integer pid", "invalid_app");
    }
    int pid = *pidParam;
    if (pid <= 0) {
        return Error("app_activate_pid requires positive pid", "invalid_app");
    }
    json desktopReady = EnsureDesktopSessionReadyForNativeControl();
    if (!desktopReady.value("ok", false)) return desktopReady;
    bool activated = Platform::ActivateAppByPid(pid);
    if (!activated) {
        return Error("could not activate app by pid", "app_activate_pid_failed");
    }
    return Ok({{"activated", activated}, {"pid", pid}, {"app", AppToJson(Platform::GetFrontmostApp())}});
}

json RunAppActiveCommand() {
    return Ok({{"app", AppToJson(Platform::GetFrontmostApp())}});
}

json RunOpenUrlCommand(const json& params, const std::string& activeControlToken) {
    if (auto unknown = UnknownParam(params, {
        "url", "browser", "profile", "newWindow", "newInstance", "controlSession", "controlSessionToken", "controlScope"
    })) {
        return Error("unknown open_url parameter: " + *unknown, "invalid_url");
    }
    auto urlParam = StringParam(params, "url", "");
    auto browserParam = StringParam(params, "browser", "");
    auto profileParam = StringParam(params, "profile", "");
    auto newWindow = BoolParam(params, "newWindow", true);
    auto newInstance = BoolParam(params, "newInstance", false);
    if (!urlParam || !browserParam || !profileParam || !newWindow || !newInstance) {
        return Error("open_url requires string url/browser/profile and boolean newWindow/newInstance", "invalid_url");
    }
    std::string url = *urlParam;
    if (IsBlank(url)) {
        return Error("open_url requires url", "invalid_url");
    }
    if (!IsHttpUrl(url)) {
        return Error("open_url requires http or https URL", "invalid_url");
    }
    if (std::any_of(url.begin(), url.end(), [](unsigned char ch) {
            return std::iscntrl(ch) != 0;
        })) {
        return Error("open_url url must not contain control characters", "invalid_url");
    }
    std::string browser = *browserParam;
    if (params.contains("browser") && IsBlank(browser)) {
        return Error("open_url browser must be non-empty when provided", "invalid_url");
    }
    if (params.contains("profile") && !IsValidBrowserProfileName(*profileParam)) {
        return Error("open_url profile must match [A-Za-z0-9][A-Za-z0-9._-]*", "invalid_url");
    }

    const std::string normalizedBrowser = NormalizeBrowserId(browser);
    const bool explicitUnsupported = params.contains("browser") &&
        !IsSupportedBrowserId(normalizedBrowser);
    std::string fallbackWarning;
    if (!*newInstance && !explicitUnsupported) {
        const ManagedBrowserSession session = ResolveManagedBrowserSession(params, true, true);
        if (session.ok) {
            if (session.pid <= 0) {
                return Error("managed browser did not report its process id", "browser_pid_unavailable");
            }
            auto beforeIds = VisibleWindowIds(BrowserWindowsForPid(session.pid));
            if (!Platform::ActivateAppByPid(session.pid) ||
                !WaitForActivePid(session.pid)) {
                return Error("could not activate managed browser", "browser_focus_failed");
            }
            Platform::WindowInfo activeWindow;
            const bool createWindow = *newWindow && !session.launched;
            if (createWindow) {
                if (!Platform::SendHotkey({"primary", "n"}, 40)) {
                    return Error("could not create managed browser window", "browser_window_create_failed");
                }
                activeWindow = WaitForBrowserWindow(session.pid, beforeIds, true);
                if (!activeWindow.available) {
                    return Error("managed browser did not create a new window", "browser_window_create_failed");
                }
            } else {
                activeWindow = WaitForBrowserWindow(session.pid, {}, false);
                if (!activeWindow.available) {
                    return Error("managed browser did not expose a window", "browser_window_unavailable");
                }
            }
            if (!Platform::SendHotkey({"primary", "l"}, 40) ||
                !Platform::TypeText(url, 1) ||
                !Platform::SendHotkey({"enter"}, 40)) {
                return Error("could not navigate managed browser", "browser_navigation_failed");
            }
            if (activeWindow.available && !activeWindow.id.empty()) {
                RegisterControlSessionResource(activeControlToken, "window",
                    activeWindow.id, session.windowQuery,
                    WindowToJson(activeWindow));
            }
            return Ok({
                {"url", url},
                {"browser", session.browser},
                {"profile", session.profile},
                {"managed", true},
                {"newWindow", *newWindow},
                {"newInstance", false},
                {"window", WindowToJson(activeWindow)}
            });
        }
        if (session.code == "invalid_browser_profile") {
            return Error(session.error, session.code);
        }
        fallbackWarning = session.error;
        browser.clear();
    }

    if (!explicitUnsupported) {
        std::string configError;
        const AppConfig config = LoadDaemonAppConfig(&configError);
        if (!configError.empty()) {
            if (fallbackWarning.empty()) fallbackWarning = configError;
            browser.clear();
        } else {
            const std::string browserId = params.contains("browser")
                ? normalizedBrowser
                : config.browser.defaultBrowser;
            const BrowserDescriptor descriptor = DescribeBrowser(browserId);
            if (descriptor.installed) {
#if defined(__APPLE__)
                browser = descriptor.applicationName;
#else
                browser = descriptor.executable;
#endif
            } else {
                browser.clear();
            }
        }
    }
    auto beforeIds = VisibleWindowIds(Platform::ListWindows(browser));
    bool opened = Platform::OpenUrl(url, browser, *newWindow, *newInstance);
    if (!opened) {
        return Error("could not open URL", "open_url_failed");
    }
    auto activeWindow = WaitForOpenedWindow(browser, beforeIds);
    if (activeWindow.available && !activeWindow.id.empty()) {
        RegisterControlSessionResource(activeControlToken, "window", activeWindow.id, browser, WindowToJson(activeWindow));
    }
    json response = Ok({
        {"url", url},
        {"browser", browser},
        {"profile", ""},
        {"managed", false},
        {"newWindow", *newWindow},
        {"newInstance", *newInstance},
        {"window", WindowToJson(activeWindow)}
    });
    if (!fallbackWarning.empty()) response["data"]["warning"] = fallbackWarning;
    return response;
}

} // namespace ComputerCpp
