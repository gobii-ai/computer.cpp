#include "DaemonDesktop.h"

#include "computer_cpp/ControlSession.h"
#include "computer_cpp/Platform.h"
#include "computer_cpp/StringUtils.h"

#include "DaemonJson.h"
#include "DaemonMetadata.h"
#include "DaemonParsing.h"
#include "DaemonProtocol.h"

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

bool IsHttpUrl(const std::string& url) {
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
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
        {"frontmostApp", AppToJson(Platform::GetFrontmostApp())},
        {"focusedElement", FocusedToJson(Platform::GetFocusedElementInfo())},
        {"frontmostWindowBounds", BoundsToJson(Platform::GetFrontmostWindowBounds())},
        {"screen", {{"width", width}, {"height", height}}},
        {"cursor", {{"x", cursorX}, {"y", cursorY}}}
    });
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
        "url", "browser", "newWindow", "newInstance", "controlSession", "controlSessionToken", "controlScope"
    })) {
        return Error("unknown open_url parameter: " + *unknown, "invalid_url");
    }
    auto urlParam = StringParam(params, "url", "");
    auto browserParam = StringParam(params, "browser", "firefox");
    auto newWindow = BoolParam(params, "newWindow", true);
    auto newInstance = BoolParam(params, "newInstance", false);
    if (!urlParam || !browserParam || !newWindow || !newInstance) {
        return Error("open_url requires string url/browser and boolean newWindow/newInstance", "invalid_url");
    }
    std::string url = *urlParam;
    if (IsBlank(url)) {
        return Error("open_url requires url", "invalid_url");
    }
    if (!IsHttpUrl(url)) {
        return Error("open_url requires http or https URL", "invalid_url");
    }
    std::string browser = *browserParam;
    if (params.contains("browser") && IsBlank(browser)) {
        return Error("open_url browser must be non-empty when provided", "invalid_url");
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
    return Ok({
        {"url", url},
        {"browser", browser},
        {"newWindow", *newWindow},
        {"newInstance", *newInstance},
        {"window", WindowToJson(activeWindow)}
    });
}

} // namespace ComputerCpp
