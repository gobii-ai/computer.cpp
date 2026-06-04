#include "computer_cpp/Platform.h"
#include "computer_cpp/HumanInput.h"
#include "LinuxPng.h"

#include <nlohmann/json.hpp>

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace ComputerCpp::Platform {
using json = nlohmann::json;
namespace {

std::optional<double> gWaylandCursorX;
std::optional<double> gWaylandCursorY;

struct CommandResult {
    int status = -1;
    std::string output;
};

std::string Trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string ShellQuote(const std::string& value) {
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out += ch;
        }
    }
    out += "'";
    return out;
}

std::string JsonStringLiteral(const std::string& value) {
    return json(value).dump();
}

CommandResult RunCommand(const std::string& command) {
    CommandResult result;
    std::array<char, 4096> buffer {};
    FILE* pipe = popen((command + " 2>/dev/null").c_str(), "r");
    if (!pipe) {
        return result;
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        result.output += buffer.data();
    }
    result.status = pclose(pipe);
    return result;
}

bool RunOk(const std::string& command) {
    return RunCommand(command + " >/dev/null").status == 0;
}

std::string GuiSessionEnvPrefix() {
    return "if command -v systemctl >/dev/null 2>&1; then "
           "__ac_env=$(systemctl --user show-environment 2>/dev/null | "
           "grep -E '^(DISPLAY|WAYLAND_DISPLAY|XDG_CURRENT_DESKTOP|XDG_SESSION_TYPE|DBUS_SESSION_BUS_ADDRESS|XAUTHORITY)=' | "
           "sed 's/^/export /'); eval \"$__ac_env\"; fi; ";
}

CommandResult RunGuiCommand(const std::string& command) {
    return RunCommand(GuiSessionEnvPrefix() + command);
}

bool RunGuiOk(const std::string& command) {
    return RunOk(GuiSessionEnvPrefix() + command);
}

std::string RunKWinScript(const std::string& scriptBody) {
    auto qdbus = RunGuiCommand("command -v qdbus6");
    if (qdbus.status != 0 || Trim(qdbus.output).empty()) {
        return "";
    }
    auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    std::string marker = "COMPUTER_CPP_KWIN_" + std::to_string(::getpid()) + "_" + std::to_string(stamp);
    std::string plugin = "computer.cpp-kwin-" + std::to_string(::getpid()) + "-" + std::to_string(stamp);
    std::filesystem::path scriptPath = std::filesystem::temp_directory_path() /
        ("computer.cpp-kwin-" + std::to_string(::getpid()) + "-" + std::to_string(stamp) + ".js");
    {
        std::ofstream script(scriptPath);
        if (!script) {
            return "";
        }
        script << "const __agentComputerMarker = " << JsonStringLiteral(marker) << ";\n";
        script << scriptBody << "\n";
    }
    RunGuiOk("qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript " + ShellQuote(plugin));
    bool loaded = RunGuiOk("qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript " +
        ShellQuote(scriptPath.string()) + " " + ShellQuote(plugin));
    if (!loaded) {
        std::filesystem::remove(scriptPath);
        return "";
    }
    RunGuiOk("qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.start");
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    std::string sedExpr = "s/^.*" + marker + " //p";
    auto result = RunGuiCommand("journalctl --user -n 500 --no-pager | sed -n " +
        ShellQuote(sedExpr) + " | tail -1");
    RunGuiOk("qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript " + ShellQuote(plugin));
    std::filesystem::remove(scriptPath);
    return Trim(result.output);
}

std::optional<std::pair<double, double>> KWinCursorPosition() {
    std::string output = RunKWinScript(
        "const p = workspace.cursorPos;\n"
        "print(__agentComputerMarker + ' ' + p.x + ' ' + p.y);");
    if (output.empty()) {
        return std::nullopt;
    }
    std::istringstream in(output);
    double x = 0.0;
    double y = 0.0;
    if (!(in >> x >> y)) {
        return std::nullopt;
    }
    return std::pair<double, double>{x, y};
}

std::string StripAnsi(std::string value) {
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\x1b' && i + 1 < value.size() && value[i + 1] == '[') {
            i += 2;
            while (i < value.size() && !std::isalpha(static_cast<unsigned char>(value[i]))) {
                ++i;
            }
            continue;
        }
        out.push_back(value[i]);
    }
    return out;
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
    auto result = RunCommand("systemctl --user show-environment 2>/dev/null | grep -E '^(XDG_SESSION_TYPE=wayland|WAYLAND_DISPLAY=)'");
    return result.status == 0 && !Trim(result.output).empty();
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
    auto result = RunGuiCommand("command -v computer.cpp-wayland-input");
    std::string path = Trim(result.output);
    if (result.status == 0 && !path.empty()) {
        return path;
    }
    return {};
}

bool RunWaylandInput(const std::vector<std::string>& args) {
    std::string helper = FindWaylandInputHelper();
    if (helper.empty()) {
        return false;
    }
    std::string command = ShellQuote(helper);
    for (const auto& arg : args) {
        command += " " + ShellQuote(arg);
    }
    return RunGuiOk(command);
}

std::string TempPngPath(const std::string& label) {
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path() /
            ("computer.cpp-" + label + "-" + std::to_string(stamp) + ".png")).string();
}

std::string TempRawPath(const std::string& label) {
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path() /
            ("computer.cpp-" + label + "-" + std::to_string(stamp) + ".rgb")).string();
}

std::string ImageMagickCommand() {
    auto magick = RunGuiCommand("command -v magick");
    if (magick.status == 0 && !Trim(magick.output).empty()) {
        return Trim(magick.output);
    }
    auto convert = RunGuiCommand("command -v convert");
    if (convert.status == 0 && !Trim(convert.output).empty()) {
        return Trim(convert.output);
    }
    return {};
}

bool WithSpectacleLock(const std::function<bool()>& fn) {
    int fd = ::open("/tmp/computer.cpp-spectacle.lock", O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        return fn();
    }
    if (::flock(fd, LOCK_EX) != 0) {
        ::close(fd);
        return fn();
    }
    bool ok = fn();
    ::flock(fd, LOCK_UN);
    ::close(fd);
    return ok;
}

bool CaptureWaylandFull(const std::string& filePath) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        bool ok = WithSpectacleLock([&]() {
            std::error_code removeEc;
            std::filesystem::remove(filePath, removeEc);
            if (!RunGuiOk("spectacle -b -n -f -o " + ShellQuote(filePath))) {
                return false;
            }
            std::error_code ec;
            return std::filesystem::exists(filePath, ec) && std::filesystem::file_size(filePath, ec) > 0;
        });
        if (ok) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(120 + attempt * 80));
    }
    return false;
}

bool TransformImage(const std::string& src, const std::string& dst, const Bounds& crop, int maxDimension) {
    std::string tool = ImageMagickCommand();
    if (tool.empty()) {
        if (!crop.available && maxDimension <= 0 && src != dst) {
            std::error_code ec;
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
            return !ec;
        }
        return src == dst;
    }
    std::string command = ShellQuote(tool) + " " + ShellQuote(src);
    if (crop.available && crop.width > 1.0 && crop.height > 1.0) {
        int x = std::max(0, static_cast<int>(std::round(crop.x)));
        int y = std::max(0, static_cast<int>(std::round(crop.y)));
        int width = std::max(1, static_cast<int>(std::round(crop.width)));
        int height = std::max(1, static_cast<int>(std::round(crop.height)));
        command += " -crop " + ShellQuote(std::to_string(width) + "x" + std::to_string(height) + "+" + std::to_string(x) + "+" + std::to_string(y));
        command += " +repage";
    }
    if (maxDimension > 0) {
        command += " -resize " + ShellQuote(std::to_string(maxDimension) + "x" + std::to_string(maxDimension) + ">");
    }
    command += " " + ShellQuote(dst);
    return RunGuiOk(command);
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
    auto result = RunGuiCommand("kscreen-doctor -o");
    if (result.status != 0 || result.output.empty()) {
        return std::nullopt;
    }
    std::istringstream in(StripAnsi(result.output));
    std::string line;
    bool enabled = false;
    bool any = false;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    while (std::getline(in, line)) {
        if (line.find("Output:") != std::string::npos) {
            enabled = false;
            continue;
        }
        if (line.find("enabled") != std::string::npos) {
            enabled = true;
            continue;
        }
        if (line.find("disabled") != std::string::npos) {
            enabled = false;
            continue;
        }
        auto pos = line.find("Geometry:");
        if (!enabled || pos == std::string::npos) {
            continue;
        }
        std::string rest = line.substr(pos + 9);
        for (char& ch : rest) {
            if (ch == ',' || ch == 'x') ch = ' ';
        }
        std::istringstream values(rest);
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        if (values >> x >> y >> width >> height) {
            if (!any) {
                left = x;
                top = y;
                right = x + width;
                bottom = y + height;
                any = true;
            } else {
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x + width);
                bottom = std::max(bottom, y + height);
            }
        }
    }
    if (!any || right <= left || bottom <= top) {
        return std::nullopt;
    }
    return Bounds{true, static_cast<double>(left), static_cast<double>(top), static_cast<double>(right - left), static_cast<double>(bottom - top)};
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

std::optional<unsigned long> ActiveWindowId() {
    auto result = RunCommand("xdotool getactivewindow");
    if (result.status == 0) {
        if (auto id = ParseWindowId(result.output)) {
            return id;
        }
    }
    result = RunCommand("xdotool getwindowfocus");
    if (result.status == 0) {
        if (auto id = ParseWindowId(result.output)) {
            return id;
        }
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
    auto result = RunCommand("xdotool search --onlyvisible --pid " + std::to_string(pid) + " | tail -n 1");
    if (result.status != 0) {
        result = RunCommand("xdotool search --pid " + std::to_string(pid) + " | tail -n 1");
    }
    if (result.status != 0) {
        return std::nullopt;
    }
    return ParseWindowId(result.output);
}

std::optional<unsigned long> WindowIdForAppQuery(const std::string& query) {
    auto classResult = RunCommand("xdotool search --onlyvisible --class " + ShellQuote(query) + " | tail -n 1");
    if (classResult.status == 0) {
        if (auto id = ParseWindowId(classResult.output)) {
            return id;
        }
    }
    auto nameResult = RunCommand("xdotool search --onlyvisible --name " + ShellQuote(query) + " | tail -n 1");
    if (nameResult.status == 0) {
        return ParseWindowId(nameResult.output);
    }
    return std::nullopt;
}

std::map<std::string, std::string> ParseShellAssignments(const std::string& output) {
    std::map<std::string, std::string> values;
    std::istringstream in(output);
    std::string line;
    while (std::getline(in, line)) {
        auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        values[line.substr(0, pos)] = line.substr(pos + 1);
    }
    return values;
}

std::string WindowClass(unsigned long window) {
    auto result = RunCommand("xprop -id " + std::to_string(window) + " WM_CLASS");
    auto text = result.output;
    auto first = text.find('"');
    auto second = first == std::string::npos ? std::string::npos : text.find('"', first + 1);
    auto third = second == std::string::npos ? std::string::npos : text.find('"', second + 1);
    auto fourth = third == std::string::npos ? std::string::npos : text.find('"', third + 1);
    if (third != std::string::npos && fourth != std::string::npos) {
        return text.substr(third + 1, fourth - third - 1);
    }
    if (first != std::string::npos && second != std::string::npos) {
        return text.substr(first + 1, second - first - 1);
    }
    return Trim(text);
}

std::optional<int> ParseIntStrict(const std::string& value) {
    std::string trimmed = Trim(value);
    int parsed = 0;
    auto* begin = trimmed.data();
    auto* end = begin + trimmed.size();
    auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<double> ParseDoubleStrict(const std::string& value) {
    std::string trimmed = Trim(value);
    double parsed = 0.0;
    auto* begin = trimmed.data();
    auto* end = begin + trimmed.size();
    auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end || !std::isfinite(parsed)) {
        return std::nullopt;
    }
    return parsed;
}

int ToInt(const std::string& value, int fallback = 0) {
    return ParseIntStrict(value).value_or(fallback);
}

double ToDouble(const std::string& value, double fallback = 0.0) {
    return ParseDoubleStrict(value).value_or(fallback);
}

int WindowPid(unsigned long window) {
    auto result = RunCommand("xdotool getwindowpid " + std::to_string(window));
    if (result.status != 0) {
        return -1;
    }
    return ToInt(result.output, -1);
}

std::string WindowName(unsigned long window) {
    auto result = RunCommand("xdotool getwindowname " + std::to_string(window));
    return Trim(result.output);
}

Bounds WindowBounds(unsigned long window) {
    auto result = RunCommand("xdotool getwindowgeometry --shell " + std::to_string(window));
    if (result.status != 0) {
        return {};
    }
    auto values = ParseShellAssignments(result.output);
    auto x = ParseIntStrict(values["X"]);
    auto y = ParseIntStrict(values["Y"]);
    auto width = ParseIntStrict(values["WIDTH"]);
    auto height = ParseIntStrict(values["HEIGHT"]);
    if (!x || !y || !width || !height) {
        return {};
    }
    return {
        true,
        static_cast<double>(*x),
        static_cast<double>(*y),
        static_cast<double>(*width),
        static_cast<double>(*height)
    };
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

WindowInfo WindowInfoFromKWinJson(const json& item) {
    WindowInfo info;
    info.available = true;
    info.id = item.value("id", item.value("internal_id", ""));
    info.title = item.value("title", item.value("caption", ""));
    info.appClass = item.value("appClass", item.value("resourceClass", ""));
    info.pid = item.value("pid", -1);
    info.active = item.value("active", false);
    info.bounds = {
        true,
        item.value("x", 0.0),
        item.value("y", 0.0),
        item.value("width", 0.0),
        item.value("height", 0.0)
    };
    return info;
}

std::vector<WindowInfo> QueryKWinWindows() {
    const std::string script = R"JS(
try {
  const wins = workspace.windowList ? workspace.windowList() : workspace.clientList();
  const data = wins.map(w => ({
    id: String(w.internalId || ""),
    title: String(w.caption || ""),
    appClass: String(w.resourceClass || ""),
    pid: w.pid || -1,
    active: workspace.activeWindow === w,
    x: w.x || 0,
    y: w.y || 0,
    width: w.width || 0,
    height: w.height || 0
  }));
  print(__agentComputerMarker + " " + JSON.stringify(data));
} catch (e) {
  print(__agentComputerMarker + " " + JSON.stringify({ok:false,error:String(e)}));
}
)JS";
    std::string output = RunKWinScript(script);
    if (output.empty()) {
        return {};
    }
    auto parsed = json::parse(output, nullptr, false);
    if (!parsed.is_array()) {
        return {};
    }
    std::vector<WindowInfo> windows;
    for (const auto& item : parsed) {
        auto info = WindowInfoFromKWinJson(item);
        if (!info.id.empty()) {
            windows.push_back(std::move(info));
        }
    }
    return windows;
}

bool KWinCloseWindow(const std::string& id) {
    std::string script = R"JS(
try {
  const target = )JS" + JsonStringLiteral(id) + R"JS(;
  const wins = workspace.windowList ? workspace.windowList() : workspace.clientList();
  let found = false;
  for (const w of wins) {
    if (String(w.internalId || "") === target) {
      found = true;
      w.closeWindow();
      break;
    }
  }
  print(__agentComputerMarker + " " + JSON.stringify({ok:found, id:target}));
} catch (e) {
  print(__agentComputerMarker + " " + JSON.stringify({ok:false,error:String(e)}));
}
)JS";
    std::string output = RunKWinScript(script);
    if (output.empty()) {
        return false;
    }
    auto parsed = json::parse(output, nullptr, false);
    if (!parsed.is_object()) {
        return false;
    }
    return parsed.value("ok", false);
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
    auto result = RunCommand("xdotool getwindowname " + ShellQuote(id));
    return result.status == 0;
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

std::string ButtonName(const std::string& button) {
    std::string lower = Lower(button);
    if (lower == "right") return "3";
    if (lower == "middle") return "2";
    return "1";
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

bool WithDisplay(const std::function<bool(Display*)>& fn) {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        return false;
    }
    bool ok = fn(display);
    XCloseDisplay(display);
    return ok;
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
        : RunGuiOk("xdotool windowclose " + ShellQuote(id));
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
    RunOk("wmctrl -ir " + std::to_string(*window) + " -b remove,maximized_vert,maximized_horz,fullscreen");
    return RunOk("wmctrl -ir " + std::to_string(*window) + " -e 0," +
                 std::to_string(static_cast<int>(bounds.x)) + "," +
                 std::to_string(static_cast<int>(bounds.y)) + "," +
                 std::to_string(static_cast<int>(bounds.width)) + "," +
                 std::to_string(static_cast<int>(bounds.height)));
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
    RunOk("xdotool windowactivate --sync " + std::to_string(*window));
    RunOk("wmctrl -ir " + std::to_string(*window) + " -b remove,maximized_vert,maximized_horz,fullscreen");
    return RunOk("wmctrl -ir " + std::to_string(*window) + " -e 0," +
                 std::to_string(static_cast<int>(bounds.x)) + "," +
                 std::to_string(static_cast<int>(bounds.y)) + "," +
                 std::to_string(static_cast<int>(bounds.width)) + "," +
                 std::to_string(static_cast<int>(bounds.height)));
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
    return window ? RunOk("xdotool windowactivate --sync " + std::to_string(*window)) : false;
}
bool LaunchOrActivateApp(const std::string& query, AppInfo& appInfo) {
    if (IsWaylandSession()) {
        std::string lower = Lower(query);
        if (lower.find("chrome") != std::string::npos) {
            if (!RunGuiOk("nohup google-chrome >/dev/null 2>&1 &")) {
                RunGuiOk("nohup chromium >/dev/null 2>&1 &");
            }
        } else if (lower.find("firefox") != std::string::npos) {
            RunGuiOk("nohup firefox >/dev/null 2>&1 &");
        } else {
            RunGuiOk("nohup " + ShellQuote(query) + " >/dev/null 2>&1 &");
        }
        appInfo = {};
        return true;
    }
    if (auto window = WindowIdForAppQuery(query)) {
        if (RunOk("xdotool windowactivate --sync " + std::to_string(*window))) {
            appInfo = GetFrontmostApp();
            return true;
        }
    }
    std::string lower = Lower(query);
    std::string command = query;
    if (lower.find("firefox") != std::string::npos) {
        command = "firefox-esr";
    }
    if (!RunOk("nohup " + command + " >/dev/null 2>&1 &")) {
        return false;
    }
    for (int i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (auto window = WindowIdForAppQuery(query)) {
            RunOk("xdotool windowactivate --sync " + std::to_string(*window));
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
    std::string command;
    std::string chosen = browser.empty() ? "firefox" : browser;
    if (chosen.find('/') != std::string::npos) {
        command = ShellQuote(chosen);
    } else {
        command = chosen;
    }
    if (newInstance && LooksLikeFirefoxBrowser(chosen)) {
        command += " --new-instance";
    }
    if (newWindow) {
        command += " --new-window";
    }
    command += " " + ShellQuote(url) + " >/tmp/computer.cpp-firefox.log 2>&1 &";
    if (IsWaylandSession()) {
        return RunGuiOk("nohup " + command);
    }
    return RunOk("nohup " + command);
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
    auto result = RunCommand("xdotool getmouselocation --shell");
    auto values = ParseShellAssignments(result.output);
    x = ToDouble(values["X"]);
    y = ToDouble(values["Y"]);
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
    bool ok = RunOk("xdotool key --clearmodifiers " + ShellQuote(chord));
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
    return RunOk("xdotool type --clearmodifiers --delay " + std::to_string(std::max(0, holdMs)) + " -- " + ShellQuote(text));
}
bool PasteText(const std::string& text) {
    if (IsWaylandSession()) {
        return RunGuiOk("qdbus6 org.kde.klipper /klipper org.kde.klipper.klipper.setClipboardContents " + ShellQuote(text)) &&
               RunWaylandInput({"key", "ctrl+v"});
    }
    return TypeText(text, 1);
}
std::vector<std::string> GetSelectAllHotkey() { return {"control", "a"}; }
bool ReadClipboardText(std::string& text) {
    if (IsWaylandSession()) {
        auto result = RunGuiCommand("qdbus6 org.kde.klipper /klipper org.kde.klipper.klipper.getClipboardContents");
        if (result.status != 0) {
            return false;
        }
        text = result.output;
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
            text.pop_back();
        }
        return true;
    }
    return false;
}
bool WriteClipboardText(const std::string& text) {
    if (IsWaylandSession()) {
        return RunGuiOk("qdbus6 org.kde.klipper /klipper org.kde.klipper.klipper.setClipboardContents " + ShellQuote(text));
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
        std::string png = TempPngPath("raw");
        std::string raw = TempRawPath("raw");
        std::vector<uint8_t> bytes;
        std::string tool = ImageMagickCommand();
        if (!tool.empty() && CaptureWaylandFull(png) && LinuxPng::ReadPngSize(png, width, height)) {
            if (RunGuiOk(ShellQuote(tool) + " " + ShellQuote(png) + " -depth 8 rgb:" + ShellQuote(raw))) {
                std::ifstream in(raw, std::ios::binary);
                bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                if (bytes.size() != static_cast<size_t>(width * height * 3)) {
                    bytes.clear();
                    width = 0;
                    height = 0;
                }
            }
        }
        std::error_code ec;
        std::filesystem::remove(png, ec);
        std::filesystem::remove(raw, ec);
        return bytes;
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
