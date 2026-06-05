#include "computer_cpp/Platform.h"
#include "computer_cpp/HumanInput.h"
#include "LinuxPng.h"

#include <nlohmann/json.hpp>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <charconv>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ComputerCpp::Platform {
using json = nlohmann::json;
namespace {

std::optional<double> gWaylandCursorX;
std::optional<double> gWaylandCursorY;

std::string Trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::vector<char*> ExecArgv(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    return argv;
}

void RedirectStandardStreamsToNull() {
    int fd = ::open("/dev/null", O_RDWR);
    if (fd < 0) {
        return;
    }
    ::dup2(fd, STDIN_FILENO);
    ::dup2(fd, STDOUT_FILENO);
    ::dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO) {
        ::close(fd);
    }
}

void CloseNonStandardFileDescriptors() {
    long maxFd = ::sysconf(_SC_OPEN_MAX);
    if (maxFd < 0) {
        maxFd = 4096;
    }
    maxFd = std::min<long>(maxFd, 65536);
    for (int fd = STDERR_FILENO + 1; fd < maxFd; ++fd) {
        ::close(fd);
    }
}

bool SpawnDetached(const std::vector<std::string>& args) {
    if (args.empty() || args.front().empty()) {
        return false;
    }
    pid_t pid = ::fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        ::setsid();
        RedirectStandardStreamsToNull();
        CloseNonStandardFileDescriptors();
        auto argv = ExecArgv(args);
        ::execvp(argv[0], argv.data());
        _exit(127);
    }
    return true;
}

int RunProcessWait(const std::vector<std::string>& args) {
    if (args.empty() || args.front().empty()) {
        return 127;
    }
    pid_t pid = ::fork();
    if (pid < 0) {
        return 127;
    }
    if (pid == 0) {
        RedirectStandardStreamsToNull();
        CloseNonStandardFileDescriptors();
        auto argv = ExecArgv(args);
        ::execvp(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return 127;
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 127;
}

std::optional<std::pair<double, double>> KWinCursorPosition() {
    return std::nullopt;
}

bool EnvEquals(const char* name, const std::string& expected) {
    const char* value = std::getenv(name);
    if (!value) {
        return false;
    }
    std::string actual = value;
    std::transform(actual.begin(), actual.end(), actual.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return actual == expected;
}

bool IsWaylandSession() {
    if (EnvEquals("XDG_SESSION_TYPE", "wayland") || std::getenv("WAYLAND_DISPLAY")) {
        return true;
    }
    return false;
}

double WaylandScrollScale() {
    const char* raw = std::getenv("COMPUTER_CPP_WAYLAND_SCROLL_SCALE");
    if (raw && *raw) {
        char* end = nullptr;
        double value = std::strtod(raw, &end);
        if (end && *end == '\0' && value > 0.0 && value <= 1.0) {
            return value;
        }
    }
    // KWin/libei scroll axis deltas are much stronger than the pixel-like
    // dy/dx contract exposed by computer.cpp. Normalize them so a reading
    // scroll remains a small human nudge instead of a page jump.
    return 0.14;
}

int ScaleWaylandScrollDelta(int delta) {
    if (delta == 0) return 0;
    double scaled = static_cast<double>(delta) * WaylandScrollScale();
    int rounded = static_cast<int>(std::lround(scaled));
    if (rounded == 0) return delta < 0 ? -1 : 1;
    return rounded;
}

std::optional<std::filesystem::path> CurrentExecutableDir() {
    std::array<char, 4096> buffer {};
    ssize_t len = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (len <= 0) {
        return std::nullopt;
    }
    buffer[static_cast<size_t>(len)] = '\0';
    return std::filesystem::path(buffer.data()).parent_path();
}

bool IsExecutableFile(const std::filesystem::path& path) {
    return ::access(path.c_str(), X_OK) == 0;
}

std::string FindWaylandInputHelper() {
    if (const char* configured = std::getenv("COMPUTER_CPP_WAYLAND_INPUT")) {
        if (IsExecutableFile(configured)) {
            return configured;
        }
    }
    if (auto dir = CurrentExecutableDir()) {
        auto sibling = *dir / "computer.cpp-wayland-input";
        if (IsExecutableFile(sibling)) {
            return sibling.string();
        }
    }
    return {};
}

bool RunWaylandInput(const std::vector<std::string>& args) {
    std::string helper = FindWaylandInputHelper();
    if (helper.empty()) {
        return false;
    }
    std::vector<std::string> command;
    command.reserve(args.size() + 1);
    command.push_back(helper);
    command.insert(command.end(), args.begin(), args.end());
    return RunProcessWait(command) == 0;
}

std::string TempPngPath(const std::string& label) {
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path() /
            ("computer.cpp-" + label + "-" + std::to_string(stamp) + ".png")).string();
}

bool CaptureWaylandFull(const std::string& filePath) {
    (void)filePath;
    return false;
}

bool TransformImage(const std::string& src, const std::string& dst, const Bounds& crop, int maxDimension) {
    if (!crop.available && maxDimension <= 0 && src != dst) {
        std::error_code ec;
        std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
        return !ec;
    }
    return src == dst;
}

bool SaveWaylandScreenshot(const std::string& filePath, Bounds crop = {}, int maxDimension = 0) {
    if (!crop.available && maxDimension <= 0) {
        return CaptureWaylandFull(filePath);
    }
    std::string full = TempPngPath("wayland-full");
    bool ok = CaptureWaylandFull(full) && TransformImage(full, filePath, crop, maxDimension);
    std::error_code ec;
    std::filesystem::remove(full, ec);
    return ok;
}

std::optional<Bounds> KScreenEnabledGeometry() {
    return std::nullopt;
}

std::optional<unsigned long> ParseWindowId(const std::string& value) {
    std::string trimmed = Trim(value);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    unsigned long parsed = 0;
    int base = 10;
    const char* begin = trimmed.data();
    const char* end = begin + trimmed.size();
    if (trimmed.size() > 2 && trimmed[0] == '0' && (trimmed[1] == 'x' || trimmed[1] == 'X')) {
        begin += 2;
        base = 16;
    }
    auto [ptr, ec] = std::from_chars(begin, end, parsed, base);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<unsigned long> WindowIdForAppQuery(const std::string& query);
std::string Lower(std::string value);

bool WithDisplay(const std::function<bool(Display*)>& fn) {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        return false;
    }
    bool ok = fn(display);
    XCloseDisplay(display);
    return ok;
}

std::optional<Window> WindowProperty(Display* display, Window window, const char* name) {
    Atom property = XInternAtom(display, name, True);
    if (property == None) {
        return std::nullopt;
    }
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char* data = nullptr;
    int status = XGetWindowProperty(
        display,
        window,
        property,
        0,
        1,
        False,
        XA_WINDOW,
        &actualType,
        &actualFormat,
        &itemCount,
        &bytesAfter,
        &data);
    std::optional<Window> result;
    if (status == Success && data && itemCount > 0 && actualFormat == 32) {
        result = *reinterpret_cast<unsigned long*>(data);
    }
    if (data) {
        XFree(data);
    }
    return result;
}

std::optional<unsigned long> CardinalProperty(Display* display, Window window, const char* name) {
    Atom property = XInternAtom(display, name, True);
    if (property == None) {
        return std::nullopt;
    }
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char* data = nullptr;
    int status = XGetWindowProperty(
        display,
        window,
        property,
        0,
        1,
        False,
        XA_CARDINAL,
        &actualType,
        &actualFormat,
        &itemCount,
        &bytesAfter,
        &data);
    std::optional<unsigned long> result;
    if (status == Success && data && itemCount > 0 && actualFormat == 32) {
        result = *reinterpret_cast<unsigned long*>(data);
    }
    if (data) {
        XFree(data);
    }
    return result;
}

std::string TextProperty(Display* display, Window window, const char* name) {
    Atom property = XInternAtom(display, name, True);
    if (property != None) {
        Atom utf8 = XInternAtom(display, "UTF8_STRING", True);
        Atom actualType = None;
        int actualFormat = 0;
        unsigned long itemCount = 0;
        unsigned long bytesAfter = 0;
        unsigned char* data = nullptr;
        int status = XGetWindowProperty(
            display,
            window,
            property,
            0,
            4096,
            False,
            utf8 == None ? AnyPropertyType : utf8,
            &actualType,
            &actualFormat,
            &itemCount,
            &bytesAfter,
            &data);
        std::string result;
        if (status == Success && data && itemCount > 0 && actualFormat == 8) {
            result.assign(reinterpret_cast<char*>(data), itemCount);
        }
        if (data) {
            XFree(data);
        }
        if (!result.empty()) {
            return result;
        }
    }
    char* legacy = nullptr;
    if (XFetchName(display, window, &legacy) && legacy) {
        std::string result = legacy;
        XFree(legacy);
        return result;
    }
    return {};
}

void CollectXWindows(Display* display, Window parent, std::vector<Window>& windows, bool visibleOnly) {
    Window root = None;
    Window returnedParent = None;
    Window* children = nullptr;
    unsigned int childCount = 0;
    if (!XQueryTree(display, parent, &root, &returnedParent, &children, &childCount)) {
        return;
    }
    for (unsigned int i = 0; i < childCount; ++i) {
        Window child = children[i];
        XWindowAttributes attrs {};
        bool include = XGetWindowAttributes(display, child, &attrs) != 0;
        if (include && (!visibleOnly || attrs.map_state == IsViewable) && !attrs.override_redirect) {
            windows.push_back(child);
        }
        CollectXWindows(display, child, windows, visibleOnly);
    }
    if (children) {
        XFree(children);
    }
}

std::vector<Window> QueryXWindows(Display* display, bool visibleOnly) {
    std::vector<Window> windows;
    CollectXWindows(display, DefaultRootWindow(display), windows, visibleOnly);
    return windows;
}

std::string WindowClass(Display* display, Window window) {
    XClassHint hint {};
    if (XGetClassHint(display, window, &hint)) {
        std::string result;
        if (hint.res_class) {
            result = hint.res_class;
        } else if (hint.res_name) {
            result = hint.res_name;
        }
        if (hint.res_name) {
            XFree(hint.res_name);
        }
        if (hint.res_class) {
            XFree(hint.res_class);
        }
        return result;
    }
    return {};
}

std::string WindowName(Display* display, Window window) {
    std::string name = TextProperty(display, window, "_NET_WM_NAME");
    if (!name.empty()) {
        return name;
    }
    return TextProperty(display, window, "WM_NAME");
}

int WindowPid(Display* display, Window window) {
    auto pid = CardinalProperty(display, window, "_NET_WM_PID");
    if (!pid) {
        return -1;
    }
    return static_cast<int>(*pid);
}

Bounds WindowBounds(Display* display, Window window) {
    XWindowAttributes attrs {};
    if (!XGetWindowAttributes(display, window, &attrs)) {
        return {};
    }
    Window root = DefaultRootWindow(display);
    int rootX = attrs.x;
    int rootY = attrs.y;
    Window child = None;
    XTranslateCoordinates(display, window, root, 0, 0, &rootX, &rootY, &child);
    return {
        true,
        static_cast<double>(rootX),
        static_cast<double>(rootY),
        static_cast<double>(attrs.width),
        static_cast<double>(attrs.height)
    };
}

bool WindowMatchesQuery(Display* display, Window window, const std::string& query) {
    std::string needle = Lower(query);
    std::string haystack = Lower(WindowClass(display, window) + " " + WindowName(display, window));
    return haystack.find(needle) != std::string::npos;
}

std::optional<unsigned long> ActiveWindowId() {
    std::optional<unsigned long> active;
    WithDisplay([&](Display* display) {
        Window root = DefaultRootWindow(display);
        if (auto netActive = WindowProperty(display, root, "_NET_ACTIVE_WINDOW")) {
            active = *netActive;
            return true;
        }
        Window focus = None;
        int revert = 0;
        XGetInputFocus(display, &focus, &revert);
        if (focus != None && focus != PointerRoot) {
            active = focus;
            return true;
        }
        return false;
    });
    if (active) {
        return active;
    }
    auto firefox = WindowIdForAppQuery("firefox");
    if (firefox) {
        return firefox;
    }
    return std::nullopt;
}

std::optional<unsigned long> WindowIdForPid(int pid) {
    if (pid <= 0) {
        return std::nullopt;
    }
    std::optional<unsigned long> found;
    WithDisplay([&](Display* display) {
        for (bool visibleOnly : {true, false}) {
            auto windows = QueryXWindows(display, visibleOnly);
            for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
                if (WindowPid(display, *it) == pid) {
                    found = *it;
                    return true;
                }
            }
        }
        return false;
    });
    return found;
}

std::optional<unsigned long> WindowIdForAppQuery(const std::string& query) {
    std::optional<unsigned long> found;
    WithDisplay([&](Display* display) {
        auto windows = QueryXWindows(display, true);
        for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
            if (WindowMatchesQuery(display, *it, query)) {
                found = *it;
                return true;
            }
        }
        return false;
    });
    return found;
}

std::string WindowClass(unsigned long window) {
    std::string result;
    WithDisplay([&](Display* display) {
        result = WindowClass(display, static_cast<Window>(window));
        return !result.empty();
    });
    return result;
}

int WindowPid(unsigned long window) {
    int pid = -1;
    WithDisplay([&](Display* display) {
        pid = WindowPid(display, static_cast<Window>(window));
        return pid > 0;
    });
    return pid;
}

std::string WindowName(unsigned long window) {
    std::string result;
    WithDisplay([&](Display* display) {
        result = WindowName(display, static_cast<Window>(window));
        return !result.empty();
    });
    return result;
}

Bounds WindowBounds(unsigned long window) {
    Bounds result;
    WithDisplay([&](Display* display) {
        result = WindowBounds(display, static_cast<Window>(window));
        return result.available;
    });
    return result;
}

WindowInfo WindowInfoForXWindow(unsigned long window) {
    WindowInfo info;
    info.available = true;
    info.id = std::to_string(window);
    info.title = WindowName(window);
    info.appClass = WindowClass(window);
    info.pid = WindowPid(window);
    info.active = ActiveWindowId() == window;
    info.bounds = WindowBounds(window);
    return info;
}

std::vector<WindowInfo> QueryKWinWindows() {
    return {};
}

bool KWinCloseWindow(const std::string& id) {
    (void)id;
    return false;
}

bool KWinWindowExists(const std::string& id) {
    if (id.empty()) {
        return false;
    }
    for (const auto& window : QueryKWinWindows()) {
        if (window.id == id) {
            return true;
        }
    }
    return false;
}

bool XWindowExists(const std::string& id) {
    if (id.empty()) {
        return false;
    }
    auto window = ParseWindowId(id);
    if (!window) {
        return false;
    }
    bool exists = false;
    WithDisplay([&](Display* display) {
        XWindowAttributes attrs {};
        exists = XGetWindowAttributes(display, static_cast<Window>(*window), &attrs) != 0;
        return exists;
    });
    return exists;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string NormalizeKey(const std::string& key) {
    std::string lower = Lower(key);
    if (lower == "command" || lower == "cmd" || lower == "meta" || lower == "control" || lower == "ctrl") return "ctrl";
    if (lower == "option" || lower == "alt") return "alt";
    if (lower == "shift") return "shift";
    if (lower == "return" || lower == "enter") return "Return";
    if (lower == "escape" || lower == "esc") return "Escape";
    if (lower == "tab") return "Tab";
    if (lower == "space") return "space";
    if (lower == "backspace") return "BackSpace";
    if (lower == "delete") return "Delete";
    if (lower == "left") return "Left";
    if (lower == "right") return "Right";
    if (lower == "up") return "Up";
    if (lower == "down") return "Down";
    return key;
}

unsigned int ButtonNumber(const std::string& button) {
    std::string lower = Lower(button);
    if (lower == "right" || lower == "secondary") return 3;
    if (lower == "middle") return 2;
    if (lower == "wheel-up") return 4;
    if (lower == "wheel-down") return 5;
    if (lower == "wheel-left") return 6;
    if (lower == "wheel-right") return 7;
    return 1;
}

bool XTestMoveMouse(double x, double y) {
    return WithDisplay([&](Display* display) {
        int screen = DefaultScreen(display);
        bool ok = XTestFakeMotionEvent(
            display,
            screen,
            static_cast<int>(std::round(x)),
            static_cast<int>(std::round(y)),
            CurrentTime) != 0;
        XFlush(display);
        return ok;
    });
}

bool XTestButton(const std::string& button, bool pressed) {
    return WithDisplay([&](Display* display) {
        bool ok = XTestFakeButtonEvent(display, ButtonNumber(button), pressed ? True : False, CurrentTime) != 0;
        XFlush(display);
        return ok;
    });
}

bool XTestClick(double x, double y, const std::string& button, int clickCount) {
    return WithDisplay([&](Display* display) {
        int screen = DefaultScreen(display);
        if (!XTestFakeMotionEvent(
                display,
                screen,
                static_cast<int>(std::round(x)),
                static_cast<int>(std::round(y)),
                CurrentTime)) {
            return false;
        }
        unsigned int buttonNumber = ButtonNumber(button);
        for (int i = 0; i < std::max(1, clickCount); ++i) {
            XTestFakeButtonEvent(display, buttonNumber, True, CurrentTime);
            XFlush(display);
            std::this_thread::sleep_for(std::chrono::milliseconds(35));
            XTestFakeButtonEvent(display, buttonNumber, False, CurrentTime);
            XFlush(display);
            if (i + 1 < std::max(1, clickCount)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(70));
            }
        }
        return true;
    });
}

bool XSendClientMessage(Display* display, Window window, const char* messageName, long data0, long data1 = 0, long data2 = 0, long data3 = 0, long data4 = 0) {
    Atom message = XInternAtom(display, messageName, False);
    if (message == None) {
        return false;
    }
    XEvent event {};
    event.xclient.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = message;
    event.xclient.format = 32;
    event.xclient.data.l[0] = data0;
    event.xclient.data.l[1] = data1;
    event.xclient.data.l[2] = data2;
    event.xclient.data.l[3] = data3;
    event.xclient.data.l[4] = data4;
    Window root = DefaultRootWindow(display);
    int sent = XSendEvent(display, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(display);
    return sent != 0;
}

bool XRequestCloseWindow(Window window) {
    return WithDisplay([&](Display* display) {
        return XSendClientMessage(display, window, "_NET_CLOSE_WINDOW", CurrentTime, 2);
    });
}

void XRemoveWindowStates(Display* display, Window window) {
    Atom maximizedVert = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", True);
    Atom maximizedHorz = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", True);
    Atom fullscreen = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", True);
    if (maximizedVert != None || maximizedHorz != None) {
        XSendClientMessage(display, window, "_NET_WM_STATE", 0, maximizedVert, maximizedHorz, 1);
    }
    if (fullscreen != None) {
        XSendClientMessage(display, window, "_NET_WM_STATE", 0, fullscreen, 0, 1);
    }
}

bool XActivateWindow(Window window) {
    return WithDisplay([&](Display* display) {
        XRaiseWindow(display, window);
        XSendClientMessage(display, window, "_NET_ACTIVE_WINDOW", 1, CurrentTime, 0);
        XSetInputFocus(display, window, RevertToParent, CurrentTime);
        XFlush(display);
        return true;
    });
}

bool XSetWindowBounds(Window window, const Bounds& bounds, bool activate) {
    if (!bounds.available) {
        return false;
    }
    return WithDisplay([&](Display* display) {
        if (activate) {
            XRaiseWindow(display, window);
            XSendClientMessage(display, window, "_NET_ACTIVE_WINDOW", 1, CurrentTime, 0);
            XSetInputFocus(display, window, RevertToParent, CurrentTime);
        }
        XRemoveWindowStates(display, window);
        XMoveResizeWindow(
            display,
            window,
            static_cast<int>(std::round(bounds.x)),
            static_cast<int>(std::round(bounds.y)),
            static_cast<unsigned int>(std::max(1, static_cast<int>(std::round(bounds.width)))),
            static_cast<unsigned int>(std::max(1, static_cast<int>(std::round(bounds.height)))));
        XFlush(display);
        return true;
    });
}

struct KeyStroke {
    KeySym symbol = NoSymbol;
    bool shift = false;
};

std::optional<KeyStroke> KeyStrokeForAscii(unsigned char ch) {
    if (ch >= 'a' && ch <= 'z') return KeyStroke{static_cast<KeySym>(XK_a + (ch - 'a')), false};
    if (ch >= 'A' && ch <= 'Z') return KeyStroke{static_cast<KeySym>(XK_a + (ch - 'A')), true};
    if (ch >= '0' && ch <= '9') return KeyStroke{static_cast<KeySym>(XK_0 + (ch - '0')), false};
    switch (ch) {
        case ' ': return KeyStroke{XK_space, false};
        case '\n': return KeyStroke{XK_Return, false};
        case '\t': return KeyStroke{XK_Tab, false};
        case '-': return KeyStroke{XK_minus, false};
        case '_': return KeyStroke{XK_minus, true};
        case '=': return KeyStroke{XK_equal, false};
        case '+': return KeyStroke{XK_equal, true};
        case '[': return KeyStroke{XK_bracketleft, false};
        case '{': return KeyStroke{XK_bracketleft, true};
        case ']': return KeyStroke{XK_bracketright, false};
        case '}': return KeyStroke{XK_bracketright, true};
        case '\\': return KeyStroke{XK_backslash, false};
        case '|': return KeyStroke{XK_backslash, true};
        case ';': return KeyStroke{XK_semicolon, false};
        case ':': return KeyStroke{XK_semicolon, true};
        case '\'': return KeyStroke{XK_apostrophe, false};
        case '"': return KeyStroke{XK_apostrophe, true};
        case ',': return KeyStroke{XK_comma, false};
        case '<': return KeyStroke{XK_comma, true};
        case '.': return KeyStroke{XK_period, false};
        case '>': return KeyStroke{XK_period, true};
        case '/': return KeyStroke{XK_slash, false};
        case '?': return KeyStroke{XK_slash, true};
        case '`': return KeyStroke{XK_grave, false};
        case '~': return KeyStroke{XK_grave, true};
        case '!': return KeyStroke{XK_1, true};
        case '@': return KeyStroke{XK_2, true};
        case '#': return KeyStroke{XK_3, true};
        case '$': return KeyStroke{XK_4, true};
        case '%': return KeyStroke{XK_5, true};
        case '^': return KeyStroke{XK_6, true};
        case '&': return KeyStroke{XK_7, true};
        case '*': return KeyStroke{XK_8, true};
        case '(': return KeyStroke{XK_9, true};
        case ')': return KeyStroke{XK_0, true};
        default: return std::nullopt;
    }
}

std::optional<KeySym> KeySymForToken(const std::string& token) {
    std::string lower = Lower(token);
    if (lower == "ctrl" || lower == "control") return XK_Control_L;
    if (lower == "alt" || lower == "option") return XK_Alt_L;
    if (lower == "shift") return XK_Shift_L;
    if (lower == "return" || lower == "enter") return XK_Return;
    if (lower == "escape" || lower == "esc") return XK_Escape;
    if (lower == "tab") return XK_Tab;
    if (lower == "space") return XK_space;
    if (lower == "backspace") return XK_BackSpace;
    if (lower == "delete") return XK_Delete;
    if (lower == "left") return XK_Left;
    if (lower == "right") return XK_Right;
    if (lower == "up") return XK_Up;
    if (lower == "down") return XK_Down;
    if (token.size() == 1) {
        if (auto stroke = KeyStrokeForAscii(static_cast<unsigned char>(token[0]))) {
            return stroke->symbol;
        }
    }
    KeySym symbol = XStringToKeysym(token.c_str());
    if (symbol == NoSymbol) {
        symbol = XStringToKeysym(lower.c_str());
    }
    if (symbol == NoSymbol) {
        return std::nullopt;
    }
    return symbol;
}

bool XKeyEvent(Display* display, KeySym symbol, bool pressed) {
    KeyCode code = XKeysymToKeycode(display, symbol);
    if (code == 0) {
        return false;
    }
    return XTestFakeKeyEvent(display, code, pressed ? True : False, CurrentTime) != 0;
}

bool XSendHotkey(const std::vector<std::string>& keys, int holdMs) {
    return WithDisplay([&](Display* display) {
        std::vector<KeySym> modifiers;
        std::optional<KeySym> mainKey;
        for (const auto& key : keys) {
            std::string normalized = NormalizeKey(key);
            std::string lower = Lower(normalized);
            if (lower == "ctrl" || lower == "control") {
                modifiers.push_back(XK_Control_L);
            } else if (lower == "alt" || lower == "option") {
                modifiers.push_back(XK_Alt_L);
            } else if (lower == "shift") {
                modifiers.push_back(XK_Shift_L);
            } else {
                mainKey = KeySymForToken(normalized);
            }
        }
        if (!mainKey) {
            return false;
        }
        for (auto modifier : modifiers) {
            if (!XKeyEvent(display, modifier, true)) return false;
        }
        bool ok = XKeyEvent(display, *mainKey, true);
        if (holdMs > 0) {
            XFlush(display);
            std::this_thread::sleep_for(std::chrono::milliseconds(holdMs));
        }
        ok = XKeyEvent(display, *mainKey, false) && ok;
        for (auto it = modifiers.rbegin(); it != modifiers.rend(); ++it) {
            ok = XKeyEvent(display, *it, false) && ok;
        }
        XFlush(display);
        return ok;
    });
}

bool XTypeText(const std::string& text, int holdMs) {
    return WithDisplay([&](Display* display) {
        for (unsigned char ch : text) {
            auto stroke = KeyStrokeForAscii(ch);
            if (!stroke) {
                return false;
            }
            bool ok = true;
            if (stroke->shift) {
                ok = XKeyEvent(display, XK_Shift_L, true);
            }
            ok = XKeyEvent(display, stroke->symbol, true) && ok;
            if (holdMs > 0) {
                XFlush(display);
                std::this_thread::sleep_for(std::chrono::milliseconds(holdMs));
            }
            ok = XKeyEvent(display, stroke->symbol, false) && ok;
            if (stroke->shift) {
                ok = XKeyEvent(display, XK_Shift_L, false) && ok;
            }
            XFlush(display);
            if (!ok) {
                return false;
            }
            if (holdMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(std::min(holdMs, 12)));
            }
        }
        return true;
    });
}

int MaskShift(unsigned long mask) {
    int shift = 0;
    while (mask && (mask & 1u) == 0u) {
        mask >>= 1u;
        ++shift;
    }
    return shift;
}

int MaskBits(unsigned long mask) {
    int bits = 0;
    while (mask) {
        bits += static_cast<int>(mask & 1u);
        mask >>= 1u;
    }
    return bits;
}

uint8_t ScaleChannel(unsigned long pixel, unsigned long mask) {
    if (mask == 0) {
        return 0;
    }
    int shift = MaskShift(mask);
    int bits = MaskBits(mask);
    unsigned long value = (pixel & mask) >> shift;
    unsigned long maxValue = (1ul << bits) - 1ul;
    return static_cast<uint8_t>((value * 255ul + maxValue / 2ul) / maxValue);
}

bool CaptureRootRgb(std::vector<uint8_t>& rgb, int& width, int& height, Bounds bounds = {}) {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        return false;
    }
    Window root = DefaultRootWindow(display);
    XWindowAttributes attrs {};
    if (!XGetWindowAttributes(display, root, &attrs)) {
        XCloseDisplay(display);
        return false;
    }
    int x = 0;
    int y = 0;
    width = attrs.width;
    height = attrs.height;
    if (bounds.available && bounds.width > 1.0 && bounds.height > 1.0) {
        x = std::clamp(static_cast<int>(bounds.x), 0, std::max(0, attrs.width - 1));
        y = std::clamp(static_cast<int>(bounds.y), 0, std::max(0, attrs.height - 1));
        width = std::clamp(static_cast<int>(bounds.width), 1, attrs.width - x);
        height = std::clamp(static_cast<int>(bounds.height), 1, attrs.height - y);
    }
    XImage* image = XGetImage(display, root, x, y, static_cast<unsigned int>(width), static_cast<unsigned int>(height), AllPlanes, ZPixmap);
    if (!image) {
        XCloseDisplay(display);
        return false;
    }

    rgb.resize(static_cast<size_t>(width * height * 3));
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            unsigned long pixel = XGetPixel(image, col, row);
            size_t out = static_cast<size_t>((row * width + col) * 3);
            rgb[out] = ScaleChannel(pixel, image->red_mask);
            rgb[out + 1] = ScaleChannel(pixel, image->green_mask);
            rgb[out + 2] = ScaleChannel(pixel, image->blue_mask);
        }
    }
    XDestroyImage(image);
    XCloseDisplay(display);
    return true;
}

} // namespace

PermissionStatus CheckPermissions(bool) { return {true, true}; }
bool OpenPermissionsSettings() { return false; }
bool OpenAccessibilitySettings() { return false; }
bool OpenScreenCaptureSettings() { return false; }
bool RequestAccessibilityPermission() { return true; }
bool RequestScreenCapturePermission() { return true; }
AppInfo GetFrontmostApp() {
    if (IsWaylandSession()) {
        return {};
    }
    auto window = ActiveWindowId();
    if (!window) {
        return {};
    }
    AppInfo app;
    app.available = true;
    app.pid = WindowPid(*window);
    app.name = WindowName(*window);
    app.bundleId = WindowClass(*window);
    if (app.name.empty()) {
        app.name = app.bundleId;
    }
    return app;
}
std::string GetFrontmostAppSummary() {
    auto app = GetFrontmostApp();
    if (!app.available) {
        return "unavailable";
    }
    return app.name + " [" + app.bundleId + "] pid=" + std::to_string(app.pid);
}
FocusedElementInfo GetFocusedElementInfo() { return {}; }
Bounds GetFrontmostWindowBounds() {
    if (IsWaylandSession()) {
        auto active = GetActiveWindow();
        return active.available ? active.bounds : Bounds{};
    }
    auto window = ActiveWindowId();
    return window ? WindowBounds(*window) : Bounds{};
}

WindowInfo GetActiveWindow() {
    for (auto& window : QueryKWinWindows()) {
        if (window.active) {
            return window;
        }
    }
    auto active = ActiveWindowId();
    return active ? WindowInfoForXWindow(*active) : WindowInfo{};
}

std::vector<WindowInfo> ListWindows(const std::string& appQuery) {
    auto windows = QueryKWinWindows();
    if (windows.empty()) {
        if (auto active = ActiveWindowId()) {
            windows.push_back(WindowInfoForXWindow(*active));
        }
    }
    if (appQuery.empty()) {
        return windows;
    }
    std::string query = Lower(appQuery);
    std::vector<WindowInfo> filtered;
    for (auto& window : windows) {
        std::string haystack = Lower(window.appClass + " " + window.title);
        if (haystack.find(query) != std::string::npos) {
            filtered.push_back(std::move(window));
        }
    }
    return filtered;
}

bool CloseWindow(const std::string& id) {
    if (id.empty()) {
        return false;
    }
    auto kwinWindows = QueryKWinWindows();
    bool useKWin = !kwinWindows.empty();
    bool foundInKWin = false;
    for (const auto& window : kwinWindows) {
        if (window.id == id) {
            foundInKWin = true;
            break;
        }
    }
    if (useKWin && !foundInKWin) {
        return false;
    }
    bool closeRequested = useKWin
        ? KWinCloseWindow(id)
        : (ParseWindowId(id) ? XRequestCloseWindow(static_cast<Window>(*ParseWindowId(id))) : false);
    if (!closeRequested) {
        return false;
    }
    for (int i = 0; i < 16; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(125));
        bool stillVisible = useKWin ? KWinWindowExists(id) : XWindowExists(id);
        if (!stillVisible) {
            return true;
        }
    }
    return false;
}

bool SetFrontmostWindowBounds(const Bounds& bounds) {
    if (IsWaylandSession()) {
        return false;
    }
    auto window = ActiveWindowId();
    if (!window || !bounds.available) {
        return false;
    }
    return XSetWindowBounds(static_cast<Window>(*window), bounds, false);
}
bool SetWindowBoundsForPid(int pid, const Bounds& bounds) {
    if (IsWaylandSession()) {
        return false;
    }
    auto window = WindowIdForPid(pid);
    if (!window) {
        window = WindowIdForAppQuery("firefox");
    }
    if (!window || !bounds.available) {
        return false;
    }
    return XSetWindowBounds(static_cast<Window>(*window), bounds, true);
}
int GetFrontmostAppPid() {
    auto app = GetFrontmostApp();
    return app.available ? app.pid : -1;
}
bool ActivateAppByPid(int pid) {
    if (IsWaylandSession()) {
        return false;
    }
    auto window = WindowIdForPid(pid);
    return window ? XActivateWindow(static_cast<Window>(*window)) : false;
}
bool LaunchOrActivateApp(const std::string& query, AppInfo& appInfo) {
    if (IsWaylandSession()) {
        std::string lower = Lower(query);
        if (lower.find("chrome") != std::string::npos) {
            SpawnDetached({"google-chrome"});
            SpawnDetached({"chromium"});
        } else if (lower.find("firefox") != std::string::npos) {
            SpawnDetached({"firefox"});
            SpawnDetached({"firefox-esr"});
        } else {
            SpawnDetached({query});
        }
        appInfo = {};
        return true;
    }
    if (auto window = WindowIdForAppQuery(query)) {
        if (XActivateWindow(static_cast<Window>(*window))) {
            appInfo = GetFrontmostApp();
            return true;
        }
    }
    std::string lower = Lower(query);
    std::string command = query;
    if (lower.find("firefox") != std::string::npos) {
        command = "firefox-esr";
    }
    if (!SpawnDetached({command})) {
        return false;
    }
    if (command == "firefox-esr") {
        SpawnDetached({"firefox"});
    }
    for (int i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (auto window = WindowIdForAppQuery(query)) {
            XActivateWindow(static_cast<Window>(*window));
            appInfo = GetFrontmostApp();
            return appInfo.available;
        }
    }
    appInfo = GetFrontmostApp();
    return appInfo.available;
}
bool LooksLikeFirefoxBrowser(const std::string& browser) {
    std::string value = browser;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    auto slash = value.find_last_of('/');
    if (slash != std::string::npos) {
        value = value.substr(slash + 1);
    }
    return value.find("firefox") != std::string::npos;
}

bool OpenUrl(const std::string& url, const std::string& browser, bool newWindow, bool newInstance) {
    if (url.empty()) {
        return false;
    }
    std::string chosen = browser.empty() ? "firefox" : browser;
    if (chosen == "firefox") {
        chosen = "firefox-esr";
    }
    std::vector<std::string> args{chosen};
    if (newInstance && LooksLikeFirefoxBrowser(chosen)) {
        args.push_back("--new-instance");
    }
    if (newWindow) {
        args.push_back("--new-window");
    }
    args.push_back(url);
    if (SpawnDetached(args)) {
        return true;
    }
    if (chosen == "firefox-esr") {
        args[0] = "firefox";
        return SpawnDetached(args);
    }
    return false;
}
void ActivateAgentApp() {}
void DeactivateAgentApp() {}
void GetScreenSize(int& width, int& height) {
    width = 0;
    height = 0;
    if (IsWaylandSession()) {
        if (auto geometry = KScreenEnabledGeometry()) {
            width = static_cast<int>(geometry->width);
            height = static_cast<int>(geometry->height);
            return;
        }
        std::string path = TempPngPath("screen-size");
        if (CaptureWaylandFull(path)) {
            LinuxPng::ReadPngSize(path, width, height);
            std::error_code ec;
            std::filesystem::remove(path, ec);
            return;
        }
    }
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        return;
    }
    int screen = DefaultScreen(display);
    width = DisplayWidth(display, screen);
    height = DisplayHeight(display, screen);
    XCloseDisplay(display);
}
void GetCursorPosition(double& x, double& y) {
    if (IsWaylandSession()) {
        if (!gWaylandCursorX || !gWaylandCursorY) {
            if (auto cursor = KWinCursorPosition()) {
                gWaylandCursorX = cursor->first;
                gWaylandCursorY = cursor->second;
            }
        }
        x = gWaylandCursorX.value_or(0.0);
        y = gWaylandCursorY.value_or(0.0);
        return;
    }
    WithDisplay([&](Display* display) {
        Window root = DefaultRootWindow(display);
        Window returnedRoot = None;
        Window returnedChild = None;
        int rootX = 0;
        int rootY = 0;
        int winX = 0;
        int winY = 0;
        unsigned int mask = 0;
        bool ok = XQueryPointer(display, root, &returnedRoot, &returnedChild, &rootX, &rootY, &winX, &winY, &mask) != 0;
        if (ok) {
            x = rootX;
            y = rootY;
        }
        return ok;
    });
}
void MoveMouse(double x, double y) {
    if (IsWaylandSession()) {
        if (RunWaylandInput({"move", std::to_string(x), std::to_string(y)})) {
            gWaylandCursorX = x;
            gWaylandCursorY = y;
        }
        return;
    }
    XTestMoveMouse(x, y);
}
void MoveMouseSmooth(double x, double y, int durationMs, int steps) {
    double startX = 0.0;
    double startY = 0.0;
    GetCursorPosition(startX, startY);
    double distance = std::hypot(x - startX, y - startY);
    auto plan = HumanInput::PlanPointerMove(distance, durationMs, steps);
    if (IsWaylandSession()) {
        std::vector<std::string> args = {
            "move",
            std::to_string(x),
            std::to_string(y),
            std::to_string(std::clamp(plan.durationMs, 45, 1600)),
            std::to_string(std::clamp(plan.steps, 2, 80)),
        };
        if (gWaylandCursorX && gWaylandCursorY) {
            args.push_back(std::to_string(*gWaylandCursorX));
            args.push_back(std::to_string(*gWaylandCursorY));
        }
        if (RunWaylandInput(args)) {
            gWaylandCursorX = x;
            gWaylandCursorY = y;
        }
        return;
    }
    int safeSteps = std::clamp(plan.steps, 2, 80);
    int effectiveDuration = std::clamp(plan.durationMs, 45, 1600);
    int sleepMs = safeSteps > 0 ? std::max(1, effectiveDuration / safeSteps) : 1;
    auto path = HumanInput::CurvedPath({startX, startY}, {x, y}, safeSteps);
    for (const auto& point : path) {
        MoveMouse(point.x, point.y);
        std::this_thread::sleep_for(std::chrono::milliseconds(HumanInput::ScaledDelayMs(sleepMs)));
    }
}
void DragMouseSmooth(double fromX, double fromY, double toX, double toY, const std::string& button, int durationMs, int steps) {
    if (IsWaylandSession()) {
        bool ok = RunWaylandInput({
            "drag",
            std::to_string(fromX),
            std::to_string(fromY),
            std::to_string(toX),
            std::to_string(toY),
            button,
            std::to_string(std::clamp(durationMs, 80, 2500)),
            std::to_string(std::clamp(steps, 2, 80)),
        });
        if (ok) {
            gWaylandCursorX = toX;
            gWaylandCursorY = toY;
        }
        return;
    }
    MoveMouseSmooth(fromX, fromY, 220, 12);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    MouseDown(button, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    MoveMouseSmooth(toX, toY, durationMs, steps);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    MouseUp(button, 1);
}
bool ClickSmooth(double x, double y, const std::string& button, int clickCount, int durationMs, int steps) {
    double startX = 0.0;
    double startY = 0.0;
    GetCursorPosition(startX, startY);
    auto plan = HumanInput::PlanPointerMove(std::hypot(x - startX, y - startY), durationMs, steps);
    if (IsWaylandSession()) {
        std::vector<std::string> args = {
            "click",
            std::to_string(x),
            std::to_string(y),
            button,
            std::to_string(std::max(1, clickCount)),
            std::to_string(std::clamp(plan.durationMs, 45, 1600)),
            std::to_string(std::clamp(plan.steps, 2, 80)),
        };
        if (gWaylandCursorX && gWaylandCursorY) {
            args.push_back(std::to_string(*gWaylandCursorX));
            args.push_back(std::to_string(*gWaylandCursorY));
        }
        bool ok = RunWaylandInput(args);
        if (ok) {
            gWaylandCursorX = x;
            gWaylandCursorY = y;
        }
        return ok;
    }
    MoveMouseSmooth(x, y, plan.durationMs, plan.steps);
    std::this_thread::sleep_for(std::chrono::milliseconds(HumanInput::PreClickSettleMs()));
    MouseDown(button, clickCount);
    std::this_thread::sleep_for(std::chrono::milliseconds(HumanInput::ClickHoldMs()));
    MouseUp(button, clickCount);
    return true;
}
void MouseDown(const std::string& button, int clickCount) {
    if (IsWaylandSession()) {
        for (int i = 0; i < std::max(1, clickCount); ++i) {
            RunWaylandInput({"button", button, "down"});
        }
        return;
    }
    for (int i = 0; i < std::max(1, clickCount); ++i) {
        XTestButton(button, true);
    }
}
void MouseUp(const std::string& button, int clickCount) {
    if (IsWaylandSession()) {
        for (int i = 0; i < std::max(1, clickCount); ++i) {
            RunWaylandInput({"button", button, "up"});
        }
        return;
    }
    for (int i = 0; i < std::max(1, clickCount); ++i) {
        XTestButton(button, false);
    }
}
void Scroll(int deltaY, int deltaX) {
    if (IsWaylandSession()) {
        RunWaylandInput({
            "scroll",
            std::to_string(ScaleWaylandScrollDelta(deltaY)),
            std::to_string(ScaleWaylandScrollDelta(deltaX)),
        });
        return;
    }
    int verticalButton = deltaY < 0 ? 5 : 4;
    int horizontalButton = deltaX < 0 ? 7 : 6;
    int verticalClicks = std::clamp(std::abs(deltaY) / 80, 1, 12);
    int horizontalClicks = std::clamp(std::abs(deltaX) / 80, 0, 12);
    for (int i = 0; i < verticalClicks && deltaY != 0; ++i) {
        std::string button = verticalButton == 4 ? "wheel-up" : "wheel-down";
        XTestButton(button, true);
        XTestButton(button, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(22));
    }
    for (int i = 0; i < horizontalClicks && deltaX != 0; ++i) {
        std::string button = horizontalButton == 6 ? "wheel-left" : "wheel-right";
        XTestButton(button, true);
        XTestButton(button, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(22));
    }
}
void ScrollGesture(int deltaY, int deltaX, int durationMs, int steps, double) {
    if (IsWaylandSession()) {
        RunWaylandInput({
            "scroll",
            std::to_string(ScaleWaylandScrollDelta(deltaY)),
            std::to_string(ScaleWaylandScrollDelta(deltaX)),
            std::to_string(std::clamp(durationMs, 50, 2500)),
            std::to_string(std::clamp(steps, 1, 80)),
        });
        return;
    }
    int safeSteps = std::clamp(steps, 1, 80);
    int effectiveDuration = std::clamp(durationMs, 50, 2500);
    int sleepMs = std::max(1, effectiveDuration / safeSteps);
    double accumulatedY = 0.0;
    double accumulatedX = 0.0;
    int emittedY = 0;
    int emittedX = 0;
    auto flushAccumulated = [&](bool force) {
        auto emit = [&](int y, int x) {
            Scroll(y, x);
            if (y != 0) ++emittedY;
            if (x != 0) ++emittedX;
        };
        while (std::abs(accumulatedY) >= 80.0) {
            int emitY = accumulatedY < 0 ? -80 : 80;
            accumulatedY -= emitY;
            emit(emitY, 0);
        }
        while (std::abs(accumulatedX) >= 80.0) {
            int emitX = accumulatedX < 0 ? -80 : 80;
            accumulatedX -= emitX;
            emit(0, emitX);
        }
        if (force && emittedY == 0 && std::abs(accumulatedY) > 0.0) {
            emit(accumulatedY < 0 ? -80 : 80, 0);
        }
        if (force && emittedX == 0 && std::abs(accumulatedX) > 0.0) {
            emit(0, accumulatedX < 0 ? -80 : 80);
        }
    };
    for (int i = 0; i < safeSteps; ++i) {
        accumulatedY += static_cast<double>(deltaY) / static_cast<double>(safeSteps);
        accumulatedX += static_cast<double>(deltaX) / static_cast<double>(safeSteps);
        flushAccumulated(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
    flushAccumulated(true);
}
bool Click(double x, double y, const std::string& button, int clickCount) {
    if (IsWaylandSession()) {
        bool ok = RunWaylandInput({
            "click",
            std::to_string(x),
            std::to_string(y),
            button,
            std::to_string(std::max(1, clickCount)),
        });
        if (ok) {
            gWaylandCursorX = x;
            gWaylandCursorY = y;
        }
        return ok;
    }
    return XTestClick(x, y, button, clickCount);
}
int ResolveKeycode(const std::string&) { return -1; }
bool SendHotkey(const std::vector<std::string>& keys, int holdMs) {
    if (keys.empty()) {
        return false;
    }
    std::string chord;
    for (const auto& key : keys) {
        if (!chord.empty()) {
            chord += "+";
        }
        chord += NormalizeKey(key);
    }
    if (IsWaylandSession()) {
        bool ok = RunWaylandInput({"key", chord});
        if (holdMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(holdMs));
        }
        return ok;
    }
    bool ok = XSendHotkey(keys, holdMs);
    if (holdMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(holdMs));
    }
    return ok;
}
bool TypeCharacter(const std::string& character, int holdMs) { return TypeText(character, holdMs); }
bool TypeText(const std::string& text, int holdMs) {
    if (IsWaylandSession()) {
        return RunWaylandInput({"type", text, std::to_string(std::max(1, holdMs))});
    }
    return XTypeText(text, std::max(0, holdMs));
}
bool PasteText(const std::string& text) {
    if (IsWaylandSession()) {
        return false;
    }
    return TypeText(text, 1);
}
std::vector<std::string> GetSelectAllHotkey() { return {"control", "a"}; }
bool ReadClipboardText(std::string& text) {
    (void)text;
    if (IsWaylandSession()) {
        return false;
    }
    return false;
}
bool WriteClipboardText(const std::string& text) {
    (void)text;
    if (IsWaylandSession()) {
        return false;
    }
    return false;
}
bool SaveScreenshot(const std::string& filePath) {
    if (IsWaylandSession()) {
        return SaveWaylandScreenshot(filePath);
    }
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb;
    return CaptureRootRgb(rgb, width, height) && LinuxPng::WritePngRgb(filePath, width, height, rgb);
}
bool SaveScreenshotRegion(const std::string& filePath, const Bounds& bounds) {
    if (IsWaylandSession()) {
        return SaveWaylandScreenshot(filePath, bounds);
    }
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb;
    return CaptureRootRgb(rgb, width, height, bounds) && LinuxPng::WritePngRgb(filePath, width, height, rgb);
}
bool SaveScreenshotScaled(const std::string& filePath, int maxDimension) {
    if (IsWaylandSession()) {
        return SaveWaylandScreenshot(filePath, {}, maxDimension);
    }
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb;
    return CaptureRootRgb(rgb, width, height) && LinuxPng::WritePngRgbScaled(filePath, width, height, rgb, maxDimension);
}
bool SaveScreenshotRegionScaled(const std::string& filePath, const Bounds& bounds, int maxDimension) {
    if (IsWaylandSession()) {
        return SaveWaylandScreenshot(filePath, bounds, maxDimension);
    }
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb;
    return CaptureRootRgb(rgb, width, height, bounds) && LinuxPng::WritePngRgbScaled(filePath, width, height, rgb, maxDimension);
}
std::vector<uint8_t> CaptureScreenRaw(int& width, int& height) {
    if (IsWaylandSession()) {
        width = 0;
        height = 0;
        return {};
    }
    std::vector<uint8_t> rgb;
    if (!CaptureRootRgb(rgb, width, height)) {
        width = 0;
        height = 0;
        return {};
    }
    return rgb;
}
SnapshotResult TakeSnapshot(const SnapshotOptions&) {
    SnapshotResult result;
    result.frontmostApp = GetFrontmostApp();
    result.frontmostWindowBounds = GetFrontmostWindowBounds();
    std::ostringstream out;
    out << "Computer: linux\n";
    out << "Frontmost app: " << GetFrontmostAppSummary() << "\n";
    if (result.frontmostWindowBounds.available) {
        out << "Frontmost window bounds: x=" << result.frontmostWindowBounds.x
            << " y=" << result.frontmostWindowBounds.y
            << " width=" << result.frontmostWindowBounds.width
            << " height=" << result.frontmostWindowBounds.height << "\n";
    }
    result.text = out.str();
    return result;
}

}
