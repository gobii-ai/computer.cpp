#include "computer_cpp/HumanInput.h"

#include <dbus/dbus.h>
#include <libei.h>

#include <linux/input-event-codes.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int Pressed = 1;
constexpr int Released = 0;

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::vector<std::string> Split(const std::string& value, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream stream(value);
    std::string part;
    while (std::getline(stream, part, delimiter)) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    return parts;
}

int ParseIntArg(const char* value, const char* name) {
    int parsed = 0;
    std::string text = value ? value : "";
    auto* begin = text.data();
    auto* end = begin + text.size();
    auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end) {
        throw std::invalid_argument(std::string("invalid integer for ") + name + ": " + text);
    }
    return parsed;
}

double ParseDoubleArg(const char* value, const char* name) {
    double parsed = 0.0;
    std::string text = value ? value : "";
    auto* begin = text.data();
    auto* end = begin + text.size();
    auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end || !std::isfinite(parsed)) {
        throw std::invalid_argument(std::string("invalid number for ") + name + ": " + text);
    }
    return parsed;
}

uint64_t NowUsec() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

int ButtonCode(const std::string& button) {
    const std::string lower = Lower(button);
    if (lower == "right" || lower == "secondary" || lower == "3") return BTN_RIGHT;
    if (lower == "middle" || lower == "2") return BTN_MIDDLE;
    return BTN_LEFT;
}

std::optional<int> KeyCode(const std::string& key) {
    static const std::map<std::string, int> keys = {
        {"esc", KEY_ESC}, {"escape", KEY_ESC},
        {"tab", KEY_TAB},
        {"enter", KEY_ENTER}, {"return", KEY_ENTER},
        {"space", KEY_SPACE},
        {"backspace", KEY_BACKSPACE},
        {"delete", KEY_DELETE}, {"del", KEY_DELETE},
        {"left", KEY_LEFT}, {"right", KEY_RIGHT}, {"up", KEY_UP}, {"down", KEY_DOWN},
        {"home", KEY_HOME}, {"end", KEY_END},
        {"pageup", KEY_PAGEUP}, {"pagedown", KEY_PAGEDOWN},
        {"ctrl", KEY_LEFTCTRL}, {"control", KEY_LEFTCTRL},
        {"alt", KEY_LEFTALT}, {"option", KEY_LEFTALT},
        {"shift", KEY_LEFTSHIFT},
        {"cmd", KEY_LEFTCTRL}, {"command", KEY_LEFTCTRL}, {"meta", KEY_LEFTMETA}, {"super", KEY_LEFTMETA},
        {"a", KEY_A}, {"b", KEY_B}, {"c", KEY_C}, {"d", KEY_D}, {"e", KEY_E},
        {"f", KEY_F}, {"g", KEY_G}, {"h", KEY_H}, {"i", KEY_I}, {"j", KEY_J},
        {"k", KEY_K}, {"l", KEY_L}, {"m", KEY_M}, {"n", KEY_N}, {"o", KEY_O},
        {"p", KEY_P}, {"q", KEY_Q}, {"r", KEY_R}, {"s", KEY_S}, {"t", KEY_T},
        {"u", KEY_U}, {"v", KEY_V}, {"w", KEY_W}, {"x", KEY_X}, {"y", KEY_Y},
        {"z", KEY_Z},
        {"0", KEY_0}, {"1", KEY_1}, {"2", KEY_2}, {"3", KEY_3}, {"4", KEY_4},
        {"5", KEY_5}, {"6", KEY_6}, {"7", KEY_7}, {"8", KEY_8}, {"9", KEY_9},
        {"minus", KEY_MINUS}, {"equal", KEY_EQUAL}, {"equals", KEY_EQUAL},
        {"leftbrace", KEY_LEFTBRACE}, {"rightbrace", KEY_RIGHTBRACE},
        {"backslash", KEY_BACKSLASH}, {"semicolon", KEY_SEMICOLON},
        {"apostrophe", KEY_APOSTROPHE}, {"grave", KEY_GRAVE},
        {"comma", KEY_COMMA}, {"dot", KEY_DOT}, {"period", KEY_DOT},
        {"slash", KEY_SLASH},
    };
    auto it = keys.find(Lower(key));
    if (it == keys.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<std::pair<int, bool>> CharacterKey(char ch) {
    static const std::map<char, std::pair<int, bool>> chars = {
        {'a', {KEY_A, false}}, {'b', {KEY_B, false}}, {'c', {KEY_C, false}}, {'d', {KEY_D, false}},
        {'e', {KEY_E, false}}, {'f', {KEY_F, false}}, {'g', {KEY_G, false}}, {'h', {KEY_H, false}},
        {'i', {KEY_I, false}}, {'j', {KEY_J, false}}, {'k', {KEY_K, false}}, {'l', {KEY_L, false}},
        {'m', {KEY_M, false}}, {'n', {KEY_N, false}}, {'o', {KEY_O, false}}, {'p', {KEY_P, false}},
        {'q', {KEY_Q, false}}, {'r', {KEY_R, false}}, {'s', {KEY_S, false}}, {'t', {KEY_T, false}},
        {'u', {KEY_U, false}}, {'v', {KEY_V, false}}, {'w', {KEY_W, false}}, {'x', {KEY_X, false}},
        {'y', {KEY_Y, false}}, {'z', {KEY_Z, false}},
        {'A', {KEY_A, true}}, {'B', {KEY_B, true}}, {'C', {KEY_C, true}}, {'D', {KEY_D, true}},
        {'E', {KEY_E, true}}, {'F', {KEY_F, true}}, {'G', {KEY_G, true}}, {'H', {KEY_H, true}},
        {'I', {KEY_I, true}}, {'J', {KEY_J, true}}, {'K', {KEY_K, true}}, {'L', {KEY_L, true}},
        {'M', {KEY_M, true}}, {'N', {KEY_N, true}}, {'O', {KEY_O, true}}, {'P', {KEY_P, true}},
        {'Q', {KEY_Q, true}}, {'R', {KEY_R, true}}, {'S', {KEY_S, true}}, {'T', {KEY_T, true}},
        {'U', {KEY_U, true}}, {'V', {KEY_V, true}}, {'W', {KEY_W, true}}, {'X', {KEY_X, true}},
        {'Y', {KEY_Y, true}}, {'Z', {KEY_Z, true}},
        {'1', {KEY_1, false}}, {'2', {KEY_2, false}}, {'3', {KEY_3, false}}, {'4', {KEY_4, false}},
        {'5', {KEY_5, false}}, {'6', {KEY_6, false}}, {'7', {KEY_7, false}}, {'8', {KEY_8, false}},
        {'9', {KEY_9, false}}, {'0', {KEY_0, false}},
        {'!', {KEY_1, true}}, {'@', {KEY_2, true}}, {'#', {KEY_3, true}}, {'$', {KEY_4, true}},
        {'%', {KEY_5, true}}, {'^', {KEY_6, true}}, {'&', {KEY_7, true}}, {'*', {KEY_8, true}},
        {'(', {KEY_9, true}}, {')', {KEY_0, true}},
        {' ', {KEY_SPACE, false}}, {'\t', {KEY_TAB, false}}, {'\n', {KEY_ENTER, false}},
        {'-', {KEY_MINUS, false}}, {'_', {KEY_MINUS, true}},
        {'=', {KEY_EQUAL, false}}, {'+', {KEY_EQUAL, true}},
        {'[', {KEY_LEFTBRACE, false}}, {'{', {KEY_LEFTBRACE, true}},
        {']', {KEY_RIGHTBRACE, false}}, {'}', {KEY_RIGHTBRACE, true}},
        {'\\', {KEY_BACKSLASH, false}}, {'|', {KEY_BACKSLASH, true}},
        {';', {KEY_SEMICOLON, false}}, {':', {KEY_SEMICOLON, true}},
        {'\'', {KEY_APOSTROPHE, false}}, {'"', {KEY_APOSTROPHE, true}},
        {'`', {KEY_GRAVE, false}}, {'~', {KEY_GRAVE, true}},
        {',', {KEY_COMMA, false}}, {'<', {KEY_COMMA, true}},
        {'.', {KEY_DOT, false}}, {'>', {KEY_DOT, true}},
        {'/', {KEY_SLASH, false}}, {'?', {KEY_SLASH, true}},
    };
    auto it = chars.find(ch);
    if (it == chars.end()) {
        return std::nullopt;
    }
    return it->second;
}

class KWinEisClient {
public:
    ~KWinEisClient() {
        Close();
    }

    bool Connect(bool needPointer, bool needKeyboard) {
        DBusError error;
        dbus_error_init(&error);
        connection_ = dbus_bus_get(DBUS_BUS_SESSION, &error);
        if (!connection_) {
            std::cerr << "dbus session connection failed: " << (error.message ? error.message : "unknown") << "\n";
            dbus_error_free(&error);
            return false;
        }
        dbus_connection_set_exit_on_disconnect(connection_, false);
        if (!dbus_connection_can_send_type(connection_, DBUS_TYPE_UNIX_FD)) {
            std::cerr << "dbus connection cannot pass unix fds\n";
            return false;
        }

        int fd = ConnectToKWin();
        if (fd < 0) {
            return false;
        }

        ei_ = ei_new_sender(nullptr);
        if (!ei_) {
            std::cerr << "ei_new_sender failed\n";
            close(fd);
            return false;
        }
        ei_configure_name(ei_, "computer.cpp");
        int rc = ei_setup_backend_fd(ei_, fd);
        if (rc != 0) {
            std::cerr << "ei_setup_backend_fd failed: " << rc << "\n";
            ei_ = ei_unref(ei_);
            return false;
        }

        return Negotiate(needPointer, needKeyboard);
    }

    bool Move(double x, double y) {
        if (!pointer_) return false;
        ei_device_pointer_motion_absolute(pointer_, x, y);
        Frame(pointer_);
        return Dispatch();
    }

    bool Button(const std::string& button, bool pressed) {
        if (!button_) return false;
        ei_device_button_button(button_, static_cast<uint32_t>(ButtonCode(button)), pressed);
        Frame(button_);
        return Dispatch();
    }

    bool Scroll(double dx, double dy) {
        if (!scroll_) return false;
        ei_device_scroll_delta(scroll_, dx, dy);
        Frame(scroll_);
        ei_device_scroll_stop(scroll_, true, true);
        Frame(scroll_);
        return Dispatch();
    }

    bool Key(int keycode, bool pressed) {
        if (!keyboard_) return false;
        ei_device_keyboard_key(keyboard_, static_cast<uint32_t>(keycode), pressed);
        Frame(keyboard_);
        return Dispatch();
    }

private:
    int ConnectToKWin() {
        DBusMessage* message = dbus_message_new_method_call(
            "org.kde.KWin",
            "/org/kde/KWin/EIS/RemoteDesktop",
            "org.kde.KWin.EIS.RemoteDesktop",
            "connectToEIS");
        if (!message) {
            std::cerr << "could not allocate dbus message\n";
            return -1;
        }

        dbus_int32_t caps = EI_DEVICE_CAP_POINTER |
                            EI_DEVICE_CAP_POINTER_ABSOLUTE |
                            EI_DEVICE_CAP_KEYBOARD |
                            EI_DEVICE_CAP_TOUCH |
                            EI_DEVICE_CAP_SCROLL |
                            EI_DEVICE_CAP_BUTTON;
        dbus_message_append_args(message, DBUS_TYPE_INT32, &caps, DBUS_TYPE_INVALID);

        DBusError error;
        dbus_error_init(&error);
        DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection_, message, 5000, &error);
        dbus_message_unref(message);
        if (!reply) {
            std::cerr << "kwin connectToEIS failed: " << (error.message ? error.message : "unknown") << "\n";
            dbus_error_free(&error);
            return -1;
        }

        DBusMessageIter iter;
        if (!dbus_message_iter_init(reply, &iter) || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UNIX_FD) {
            std::cerr << "kwin connectToEIS returned no fd\n";
            dbus_message_unref(reply);
            return -1;
        }
        int fd = -1;
        dbus_message_iter_get_basic(&iter, &fd);
        if (!dbus_message_iter_next(&iter) || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INT32) {
            std::cerr << "kwin connectToEIS returned no cookie\n";
            dbus_message_unref(reply);
            if (fd >= 0) close(fd);
            return -1;
        }
        dbus_int32_t cookie = 0;
        dbus_message_iter_get_basic(&iter, &cookie);
        cookie_ = cookie;
        dbus_message_unref(reply);
        return fd;
    }

    bool Negotiate(bool needPointer, bool needKeyboard) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            pollfd pfd{ei_get_fd(ei_), POLLIN, 0};
            int pollResult = poll(&pfd, 1, 300);
            if (pollResult > 0 && (pfd.revents & POLLIN)) {
                ei_dispatch(ei_);
            }

            while (ei_event* event = ei_get_event(ei_)) {
                auto type = ei_event_get_type(event);
                if (type == EI_EVENT_DISCONNECT) {
                    std::cerr << "EIS disconnected during negotiation\n";
                    ei_event_unref(event);
                    return false;
                }
                if (type == EI_EVENT_SEAT_ADDED) {
                    BindSeat(event);
                } else if (type == EI_EVENT_DEVICE_ADDED) {
                    RegisterDevice(event);
                }
                ei_event_unref(event);
            }

            bool pointerReady = !needPointer || (pointer_ && button_ && scroll_);
            bool keyboardReady = !needKeyboard || keyboard_;
            if (pointerReady && keyboardReady) {
                StartDevices();
                return true;
            }
        }

        std::cerr << "timed out waiting for EIS devices";
        if (needPointer) std::cerr << " pointer=" << (pointer_ != nullptr) << " button=" << (button_ != nullptr) << " scroll=" << (scroll_ != nullptr);
        if (needKeyboard) std::cerr << " keyboard=" << (keyboard_ != nullptr);
        std::cerr << "\n";
        return false;
    }

    void BindSeat(ei_event* event) {
        ei_seat* seat = ei_event_get_seat(event);
        ei_seat_bind_capabilities(
            seat,
            EI_DEVICE_CAP_POINTER,
            EI_DEVICE_CAP_POINTER_ABSOLUTE,
            EI_DEVICE_CAP_KEYBOARD,
            EI_DEVICE_CAP_SCROLL,
            EI_DEVICE_CAP_BUTTON,
            EI_DEVICE_CAP_TOUCH,
            nullptr);
    }

    void RegisterDevice(ei_event* event) {
        ei_device* device = ei_event_get_device(event);
        const bool hasPointer = ei_device_has_capability(device, EI_DEVICE_CAP_POINTER_ABSOLUTE);
        const bool hasButton = ei_device_has_capability(device, EI_DEVICE_CAP_BUTTON);
        const bool hasScroll = ei_device_has_capability(device, EI_DEVICE_CAP_SCROLL);
        const bool hasKeyboard = ei_device_has_capability(device, EI_DEVICE_CAP_KEYBOARD);

        if (hasPointer && hasButton) {
            if (pointer_ != device) {
                pointer_ = ei_device_ref(device);
            }
            if (button_ != device) {
                button_ = ei_device_ref(device);
            }
            if (hasScroll && scroll_ != device) {
                scroll_ = ei_device_ref(device);
            }
            if (!keyboard_ && hasKeyboard) {
                keyboard_ = ei_device_ref(device);
            }
            return;
        }

        if (!pointer_ && hasPointer) {
            pointer_ = ei_device_ref(device);
        }
        if (!button_ && hasButton) {
            button_ = ei_device_ref(device);
        }
        if (!scroll_ && hasScroll) {
            scroll_ = ei_device_ref(device);
        }
        if (!keyboard_ && hasKeyboard) {
            keyboard_ = ei_device_ref(device);
        }
    }

    void StartDevices() {
        std::set<ei_device*> devices;
        for (auto* device : {pointer_, button_, scroll_, keyboard_}) {
            if (device && devices.insert(device).second) {
                ei_device_start_emulating(device, ++sequence_);
            }
        }
        Dispatch();
    }

    void Frame(ei_device* device) {
        ei_device_frame(device, NowUsec());
    }

    bool Dispatch() {
        if (ei_) {
            ei_dispatch(ei_);
        }
        return true;
    }

    void DisconnectKWin() {
        if (!connection_ || cookie_ < 0) {
            return;
        }
        DBusMessage* message = dbus_message_new_method_call(
            "org.kde.KWin",
            "/org/kde/KWin/EIS/RemoteDesktop",
            "org.kde.KWin.EIS.RemoteDesktop",
            "disconnect");
        if (!message) {
            return;
        }
        dbus_int32_t cookie = cookie_;
        dbus_message_append_args(message, DBUS_TYPE_INT32, &cookie, DBUS_TYPE_INVALID);
        dbus_connection_send(connection_, message, nullptr);
        dbus_connection_flush(connection_);
        dbus_message_unref(message);
        cookie_ = -1;
    }

    void Close() {
        std::set<ei_device*> devices;
        for (auto* device : {pointer_, button_, scroll_, keyboard_}) {
            if (device && devices.insert(device).second) {
                ei_device_stop_emulating(device);
            }
        }
        Dispatch();
        if (pointer_) pointer_ = ei_device_unref(pointer_);
        if (button_) button_ = ei_device_unref(button_);
        if (scroll_) scroll_ = ei_device_unref(scroll_);
        if (keyboard_) keyboard_ = ei_device_unref(keyboard_);
        if (ei_) ei_ = ei_unref(ei_);
        DisconnectKWin();
        if (connection_) {
            dbus_connection_unref(connection_);
            connection_ = nullptr;
        }
    }

    DBusConnection* connection_ = nullptr;
    ei* ei_ = nullptr;
    ei_device* pointer_ = nullptr;
    ei_device* button_ = nullptr;
    ei_device* scroll_ = nullptr;
    ei_device* keyboard_ = nullptr;
    int cookie_ = -1;
    uint32_t sequence_ = 0;
};

bool SmoothMove(KWinEisClient& client, double x, double y, int durationMs, int steps, std::optional<double> startX, std::optional<double> startY) {
    if (!startX || !startY || steps <= 1 || durationMs <= 0) {
        return client.Move(x, y);
    }
    int safeSteps = std::clamp(steps, 1, 80);
    int sleepMs = std::max(1, std::clamp(durationMs, 0, 3000) / safeSteps);
    auto path = ComputerCpp::HumanInput::CurvedPath({*startX, *startY}, {x, y}, safeSteps);
    for (const auto& point : path) {
        if (!client.Move(point.x, point.y)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(ComputerCpp::HumanInput::ScaledDelayMs(sleepMs)));
    }
    if (ComputerCpp::HumanInput::ShouldMicroPause(std::hypot(x - *startX, y - *startY))) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ComputerCpp::HumanInput::MicroPauseMs()));
    }
    return true;
}

bool SendChord(KWinEisClient& client, const std::string& chord) {
    std::vector<int> codes;
    for (const auto& part : Split(chord, '+')) {
        auto code = KeyCode(part);
        if (!code) {
            std::cerr << "unknown key: " << part << "\n";
            return false;
        }
        codes.push_back(*code);
    }
    if (codes.empty()) {
        return false;
    }
    for (int code : codes) {
        if (!client.Key(code, true)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }
    for (auto it = codes.rbegin(); it != codes.rend(); ++it) {
        if (!client.Key(*it, false)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }
    return true;
}

bool TypeText(KWinEisClient& client, const std::string& text, int delayMs) {
    for (char ch : text) {
        auto entry = CharacterKey(ch);
        if (!entry) {
            std::cerr << "unsupported character for Wayland direct typing\n";
            return false;
        }
        auto [code, shift] = *entry;
        if (shift && !client.Key(KEY_LEFTSHIFT, true)) return false;
        if (!client.Key(code, true)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, delayMs)));
        if (!client.Key(code, false)) return false;
        if (shift && !client.Key(KEY_LEFTSHIFT, false)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, delayMs)));
    }
    return true;
}

void Usage() {
    std::cerr << "usage: computer.cpp-wayland-input probe|move|click|drag|button|scroll|key|type ...\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        Usage();
        return 2;
    }
    std::string command = argv[1];
    bool needPointer = command == "probe" || command == "move" || command == "click" || command == "drag" || command == "button" || command == "scroll";
    bool needKeyboard = command == "probe" || command == "key" || command == "type";
    KWinEisClient client;
    if (!client.Connect(needPointer, needKeyboard)) {
        return 1;
    }

    try {
        if (command == "probe") {
            std::cout << "ok\n";
            return 0;
        }
        if (command == "move") {
            if (argc < 4) {
                Usage();
                return 2;
            }
            double x = ParseDoubleArg(argv[2], "x");
            double y = ParseDoubleArg(argv[3], "y");
            int durationMs = argc > 4 ? ParseIntArg(argv[4], "duration-ms") : 0;
            int steps = argc > 5 ? ParseIntArg(argv[5], "steps") : 1;
            std::optional<double> startX;
            std::optional<double> startY;
            if (argc > 7) {
                startX = ParseDoubleArg(argv[6], "start-x");
                startY = ParseDoubleArg(argv[7], "start-y");
            }
            return SmoothMove(client, x, y, durationMs, steps, startX, startY) ? 0 : 1;
        }
        if (command == "click") {
            if (argc < 4) {
                Usage();
                return 2;
            }
            double x = ParseDoubleArg(argv[2], "x");
            double y = ParseDoubleArg(argv[3], "y");
            std::string button = argc > 4 ? argv[4] : "left";
            int count = argc > 5 ? std::max(1, ParseIntArg(argv[5], "count")) : 1;
            int durationMs = argc > 6 ? ParseIntArg(argv[6], "duration-ms") : 0;
            int steps = argc > 7 ? ParseIntArg(argv[7], "steps") : 1;
            std::optional<double> startX;
            std::optional<double> startY;
            if (argc > 9) {
                startX = ParseDoubleArg(argv[8], "start-x");
                startY = ParseDoubleArg(argv[9], "start-y");
            }
            if (!SmoothMove(client, x, y, durationMs, steps, startX, startY)) return 1;
            std::this_thread::sleep_for(std::chrono::milliseconds(ComputerCpp::HumanInput::PreClickSettleMs()));
            for (int i = 0; i < count; ++i) {
                if (!client.Button(button, true)) return 1;
                std::this_thread::sleep_for(std::chrono::milliseconds(ComputerCpp::HumanInput::ClickHoldMs()));
                if (!client.Button(button, false)) return 1;
                if (i + 1 < count) std::this_thread::sleep_for(std::chrono::milliseconds(ComputerCpp::HumanInput::MultiClickGapMs()));
            }
            return 0;
        }
        if (command == "drag") {
            if (argc < 6) {
                Usage();
                return 2;
            }
            double fromX = ParseDoubleArg(argv[2], "from-x");
            double fromY = ParseDoubleArg(argv[3], "from-y");
            double toX = ParseDoubleArg(argv[4], "to-x");
            double toY = ParseDoubleArg(argv[5], "to-y");
            std::string button = argc > 6 ? argv[6] : "left";
            int durationMs = argc > 7 ? ParseIntArg(argv[7], "duration-ms") : 700;
            int steps = argc > 8 ? ParseIntArg(argv[8], "steps") : 32;
            if (!SmoothMove(client, fromX, fromY, 220, 12, std::nullopt, std::nullopt)) return 1;
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            bool down = false;
            if (!client.Button(button, true)) return 1;
            down = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            bool moved = SmoothMove(client, toX, toY, durationMs, steps, fromX, fromY);
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            bool released = client.Button(button, false);
            return moved && down && released ? 0 : 1;
        }
        if (command == "button") {
            if (argc < 4) {
                Usage();
                return 2;
            }
            std::string state = Lower(argv[3]);
            return client.Button(argv[2], state == "down" || state == "press" || state == "pressed") ? 0 : 1;
        }
        if (command == "scroll") {
            if (argc < 4) {
                Usage();
                return 2;
            }
            double dy = ParseDoubleArg(argv[2], "dy");
            double dx = ParseDoubleArg(argv[3], "dx");
            int durationMs = argc > 4 ? ParseIntArg(argv[4], "duration-ms") : 0;
            int steps = argc > 5 ? std::clamp(ParseIntArg(argv[5], "steps"), 1, 80) : 1;
            int sleepMs = std::max(1, std::clamp(durationMs, 0, 3000) / steps);
            for (int i = 0; i < steps; ++i) {
                if (!client.Scroll(-dx / steps, -dy / steps)) return 1;
                std::this_thread::sleep_for(std::chrono::milliseconds(ComputerCpp::HumanInput::ScaledDelayMs(sleepMs, 0.72, 1.35)));
            }
            return 0;
        }
        if (command == "key") {
            if (argc < 3) {
                Usage();
                return 2;
            }
            return SendChord(client, argv[2]) ? 0 : 1;
        }
        if (command == "type") {
            if (argc < 3) {
                Usage();
                return 2;
            }
            int delayMs = argc > 3 ? ParseIntArg(argv[3], "delay-ms") : 1;
            return TypeText(client, argv[2], delayMs) ? 0 : 1;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 2;
    }

    Usage();
    return 2;
}
