#include "computer_cpp/Platform.h"

#include "computer_cpp/HumanInput.h"
#include "computer_cpp/Image.h"
#include "computer_cpp/StringUtils.h"
#include "computer_cpp/WindowsUtil.h"

#include "WindowsAppResolver.h"
#include "WindowsNativeInput.h"

#define NOMINMAX
#include <windows.h>
#include <oleacc.h>
#include <propkey.h>
#include <propsys.h>
#include <psapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <uiautomation.h>
#include <wtsapi32.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>

namespace ComputerCpp::Platform {
namespace {

struct ComScope {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~ComScope() {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }
};

template <typename T>
class ComPtr {
public:
    ~ComPtr() {
        reset();
    }
    T** put() {
        reset();
        return &ptr_;
    }
    T* get() const {
        return ptr_;
    }
    T* operator->() const {
        return ptr_;
    }
    explicit operator bool() const {
        return ptr_ != nullptr;
    }
    void reset() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

private:
    T* ptr_ = nullptr;
};

std::wstring Utf8ToWide(const std::string& value) {
    return Windows::Utf8ToWide(value);
}

std::string WindowTitle(HWND hwnd) {
    int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) {
        return {};
    }
    std::wstring title(static_cast<size_t>(length + 1), L'\0');
    GetWindowTextW(hwnd, title.data(), length + 1);
    title.resize(static_cast<size_t>(length));
    return Windows::WideToUtf8(title);
}

std::string ProcessName(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return {};
    }
    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    std::string out;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        out = Windows::WideToUtf8(std::filesystem::path(path).filename().wstring());
    } else if (GetModuleBaseNameW(process, nullptr, path, MAX_PATH)) {
        out = Windows::WideToUtf8(path);
    }
    CloseHandle(process);
    return out;
}

Bounds RectToBounds(const RECT& rect) {
    return {
        true,
        static_cast<double>(rect.left),
        static_cast<double>(rect.top),
        static_cast<double>(rect.right - rect.left),
        static_cast<double>(rect.bottom - rect.top),
    };
}

std::optional<HWND> HwndFromId(const std::string& id) {
    if (id.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    unsigned long long raw = std::strtoull(id.c_str(), &end, 16);
    if (end == id.c_str() || *end != '\0') {
        return std::nullopt;
    }
    HWND hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(raw));
    return IsWindow(hwnd) ? std::optional<HWND>(hwnd) : std::nullopt;
}

std::string IdFromHwnd(HWND hwnd) {
    std::ostringstream out;
    out << std::hex << reinterpret_cast<uintptr_t>(hwnd);
    return out.str();
}

bool IsSafeActivationHit(LRESULT hit) {
    switch (hit) {
        case HTCAPTION:
        case HTLEFT:
        case HTRIGHT:
        case HTTOP:
        case HTBOTTOM:
        case HTTOPLEFT:
        case HTTOPRIGHT:
        case HTBOTTOMLEFT:
        case HTBOTTOMRIGHT:
            return true;
        default:
            return false;
    }
}

std::optional<POINT> SafeActivationClickPoint(HWND hwnd, const RECT& rect) {
    const int virtualLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int virtualTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int virtualRight = virtualLeft + GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1;
    const int virtualBottom = virtualTop + GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1;
    const int centerX = rect.left + (rect.right - rect.left) / 2;
    const int centerY = rect.top + (rect.bottom - rect.top) / 2;
    POINT candidates[] = {
        {rect.left + 1, centerY},
        {rect.right - 2, centerY},
        {centerX, rect.top + 1},
        {centerX, rect.bottom - 2},
        {centerX, rect.top + 8},
        {rect.left + (rect.right - rect.left) / 4, rect.top + 8},
        {rect.left + ((rect.right - rect.left) * 3) / 4, rect.top + 8},
    };
    for (auto point : candidates) {
        point.x = std::clamp<LONG>(point.x, virtualLeft, virtualRight);
        point.y = std::clamp<LONG>(point.y, virtualTop, virtualBottom);
        DWORD_PTR hit = HTNOWHERE;
        if (SendMessageTimeoutW(
                hwnd,
                WM_NCHITTEST,
                0,
                MAKELPARAM(point.x, point.y),
                SMTO_ABORTIFHUNG | SMTO_BLOCK,
                100,
                &hit) != 0 &&
            IsSafeActivationHit(static_cast<LRESULT>(hit))) {
            return point;
        }
    }
    return std::nullopt;
}

bool ActivateWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }
    ShowWindowAsync(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);

    HWND foreground = GetForegroundWindow();
    DWORD currentThread = GetCurrentThreadId();
    DWORD targetThread = GetWindowThreadProcessId(hwnd, nullptr);
    DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    bool attachedTarget = targetThread != 0 && targetThread != currentThread &&
        AttachThreadInput(currentThread, targetThread, TRUE) != FALSE;
    bool attachedForeground = foregroundThread != 0 && foregroundThread != currentThread && foregroundThread != targetThread &&
        AttachThreadInput(currentThread, foregroundThread, TRUE) != FALSE;

    BringWindowToTop(hwnd);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
    SwitchToThisWindow(hwnd, TRUE);

    if (GetForegroundWindow() != hwnd) {
        RECT rect{};
        POINT originalCursor{};
        if (GetWindowRect(hwnd, &rect) && GetCursorPos(&originalCursor)) {
            // Only click a point the target itself identifies as caption or
            // resize chrome. Borderless/maximized windows may have no such
            // point; in that case fail activation instead of touching content.
            auto clickPoint = SafeActivationClickPoint(hwnd, rect);
            if (clickPoint && SetCursorPos(clickPoint->x, clickPoint->y)) {
                INPUT click[2]{};
                click[0].type = INPUT_MOUSE;
                click[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                click[1].type = INPUT_MOUSE;
                click[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
                const UINT sent = SendInput(2, click, sizeof(INPUT));
                if (sent == 1) {
                    INPUT release{};
                    release.type = INPUT_MOUSE;
                    release.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                    SendInput(1, &release, sizeof(INPUT));
                }
                if (sent == 2) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }
            SetCursorPos(originalCursor.x, originalCursor.y);
        }
    }

    if (attachedForeground) {
        AttachThreadInput(currentThread, foregroundThread, FALSE);
    }
    if (attachedTarget) {
        AttachThreadInput(currentThread, targetThread, FALSE);
    }

    for (int attempt = 0; attempt < 20; ++attempt) {
        if (GetForegroundWindow() == hwnd) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return false;
}

bool IsRealWindow(HWND hwnd) {
    return IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr && GetWindowTextLengthW(hwnd) > 0;
}

WindowInfo WindowInfoFor(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    WindowInfo info;
    info.available = true;
    info.id = IdFromHwnd(hwnd);
    info.title = WindowTitle(hwnd);
    info.appClass = ProcessName(pid);
    info.pid = static_cast<int>(pid);
    info.active = hwnd == GetForegroundWindow();
    info.bounds = RectToBounds(rect);
    return info;
}

WORD MouseButtonFlag(const std::string& button, bool down) {
    std::string lower = Lowercase(button);
    if (lower == "right") {
        return down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    }
    if (lower == "middle") {
        return down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
    }
    return down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
}

void SendMouseButton(const std::string& button, bool down) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MouseButtonFlag(button, down);
    SendInput(1, &input, sizeof(INPUT));
}

std::optional<WORD> KeyNameToVirtualKey(const std::string& keyName) {
    std::string key = Lowercase(keyName);
    if (key == "control" || key == "ctrl" || key == "primary") return VK_CONTROL;
    if (key == "shift") return VK_SHIFT;
    if (key == "alt" || key == "option") return VK_MENU;
    if (key == "win" || key == "windows" || key == "super" || key == "cmd" || key == "command") return VK_LWIN;
    if (key == "enter" || key == "return") return VK_RETURN;
    if (key == "escape" || key == "esc") return VK_ESCAPE;
    if (key == "tab") return VK_TAB;
    if (key == "space") return VK_SPACE;
    if (key == "backspace") return VK_BACK;
    if (key == "delete") return VK_DELETE;
    if (key == "up") return VK_UP;
    if (key == "down") return VK_DOWN;
    if (key == "left") return VK_LEFT;
    if (key == "right") return VK_RIGHT;
    if (key.size() == 1) {
        SHORT vk = VkKeyScanW(static_cast<wchar_t>(std::toupper(static_cast<unsigned char>(key[0]))));
        if (vk != -1) return static_cast<WORD>(vk & 0xff);
    }
    if (key.size() >= 2 && key[0] == 'f') {
        char* end = nullptr;
        long n = std::strtol(key.c_str() + 1, &end, 10);
        if (end != key.c_str() + 1 && *end == '\0' && n >= 1 && n <= 24) {
            return static_cast<WORD>(VK_F1 + n - 1);
        }
    }
    return std::nullopt;
}

WindowsInput::SendInputFunction& NativeInputSender() {
    static WindowsInput::SendInputFunction sender =
        [](UINT count, LPINPUT inputs, int inputSize) {
            return ::SendInput(count, inputs, inputSize);
        };
    return sender;
}

bool SendVirtualKey(WORD vk, bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    if (!down) {
        input.ki.dwFlags = KEYEVENTF_KEYUP;
    }
    return NativeInputSender()(1, &input, sizeof(INPUT)) == 1;
}

bool SendUnicodeChar(wchar_t ch, bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = ch;
    input.ki.dwFlags = KEYEVENTF_UNICODE | (down ? 0 : KEYEVENTF_KEYUP);
    return NativeInputSender()(1, &input, sizeof(INPUT)) == 1;
}

Image::RgbImage CaptureRegion(int left, int top, int width, int height) {
    if (width <= 0 || height <= 0) {
        return {};
    }
    HDC screen = GetDC(nullptr);
    if (!screen) {
        return {};
    }
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    if (!memory || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        return {};
    }
    HGDIOBJ old = SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, width, height, screen, left, top, SRCCOPY | CAPTUREBLT);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    std::vector<uint8_t> bgra(static_cast<size_t>(width * height * 4));
    bool ok = GetDIBits(memory, bitmap, 0, static_cast<UINT>(height), bgra.data(), &info, DIB_RGB_COLORS) != 0;

    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    if (!ok) {
        return {};
    }
    std::vector<uint8_t> rgb(static_cast<size_t>(width * height * 3));
    for (int i = 0; i < width * height; ++i) {
        rgb[static_cast<size_t>(i * 3)] = bgra[static_cast<size_t>(i * 4 + 2)];
        rgb[static_cast<size_t>(i * 3 + 1)] = bgra[static_cast<size_t>(i * 4 + 1)];
        rgb[static_cast<size_t>(i * 3 + 2)] = bgra[static_cast<size_t>(i * 4)];
    }
    return Image::MakeRgbImage(width, height, std::move(rgb));
}

Image::RgbImage ResizeRgb(const Image::RgbImage& src, int maxDimension) {
    if (!src.valid() || maxDimension <= 0 || std::max(src.width, src.height) <= maxDimension) {
        return src;
    }
    double scale = static_cast<double>(maxDimension) / static_cast<double>(std::max(src.width, src.height));
    int dstWidth = std::max(1, static_cast<int>(std::round(src.width * scale)));
    int dstHeight = std::max(1, static_cast<int>(std::round(src.height * scale)));
    std::vector<uint8_t> rgb(static_cast<size_t>(dstWidth * dstHeight * 3));
    for (int y = 0; y < dstHeight; ++y) {
        int sy = std::min(src.height - 1, static_cast<int>((static_cast<int64_t>(y) * src.height) / dstHeight));
        for (int x = 0; x < dstWidth; ++x) {
            int sx = std::min(src.width - 1, static_cast<int>((static_cast<int64_t>(x) * src.width) / dstWidth));
            size_t from = static_cast<size_t>((sy * src.width + sx) * 3);
            size_t to = static_cast<size_t>((y * dstWidth + x) * 3);
            rgb[to] = src.rgb[from];
            rgb[to + 1] = src.rgb[from + 1];
            rgb[to + 2] = src.rgb[from + 2];
        }
    }
    return Image::MakeRgbImage(dstWidth, dstHeight, std::move(rgb));
}

std::optional<std::wstring> ClipboardText() {
    if (!OpenClipboard(nullptr)) {
        return std::nullopt;
    }
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (!data) {
        CloseClipboard();
        return std::nullopt;
    }
    auto* text = static_cast<const wchar_t*>(GlobalLock(data));
    if (!text) {
        CloseClipboard();
        return std::nullopt;
    }
    std::wstring value(text);
    GlobalUnlock(data);
    CloseClipboard();
    return value;
}

std::string BstrToUtf8(BSTR value) {
    if (!value) {
        return {};
    }
    std::string out = Windows::WideToUtf8(std::wstring_view(value, SysStringLen(value)));
    SysFreeString(value);
    return out;
}

bool OpenSettingsUri(const wchar_t* uri) {
    return reinterpret_cast<intptr_t>(ShellExecuteW(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

Bounds UiaBounds(IUIAutomationElement* element) {
    RECT rect{};
    if (!element || FAILED(element->get_CurrentBoundingRectangle(&rect))) {
        return {};
    }
    return RectToBounds(rect);
}

std::string UiaName(IUIAutomationElement* element) {
    BSTR value = nullptr;
    return element && SUCCEEDED(element->get_CurrentName(&value)) ? BstrToUtf8(value) : std::string();
}

std::string UiaRole(IUIAutomationElement* element) {
    CONTROLTYPEID id = 0;
    if (!element || FAILED(element->get_CurrentControlType(&id))) {
        return {};
    }
    return "UIA:" + std::to_string(id);
}

void AppendElementLines(IUIAutomation* automation, IUIAutomationElement* element, SnapshotResult& result, int depth, int maxDepth, int& count, int maxNodes) {
    if (!automation || !element || depth > maxDepth || count >= maxNodes) {
        return;
    }
    ++count;
    std::string name = UiaName(element);
    std::string role = UiaRole(element);
    Bounds bounds = UiaBounds(element);
    int pid = -1;
    element->get_CurrentProcessId(&pid);
    std::string ref = "w" + std::to_string(count);

    std::ostringstream line;
    line << std::string(static_cast<size_t>(depth * 2), ' ') << "@" << ref << " " << role;
    if (!name.empty()) {
        line << " \"" << name << "\"";
    }
    if (bounds.available) {
        line << " [" << bounds.x << "," << bounds.y << " " << bounds.width << "x" << bounds.height << "]";
    }
    result.text += line.str() + "\n";

    RefRecord record;
    record.ref = ref;
    record.kind = "element";
    record.source = "uia";
    record.role = role;
    record.name = name;
    record.pid = pid;
    record.bounds = bounds;
    result.refs.push_back(record);

    ComPtr<IUIAutomationCondition> condition;
    if (FAILED(automation->CreateTrueCondition(condition.put()))) {
        return;
    }
    ComPtr<IUIAutomationElementArray> children;
    if (FAILED(element->FindAll(TreeScope_Children, condition.get(), children.put())) || !children) {
        return;
    }
    int length = 0;
    children->get_Length(&length);
    for (int i = 0; i < length && count < maxNodes; ++i) {
        ComPtr<IUIAutomationElement> child;
        if (SUCCEEDED(children->GetElement(i, child.put()))) {
            AppendElementLines(automation, child.get(), result, depth + 1, maxDepth, count, maxNodes);
        }
    }
}

}

void WindowsInput::SetSendInputFunctionForTesting(SendInputFunction sender) {
    NativeInputSender() = sender
        ? std::move(sender)
        : SendInputFunction([](UINT count, LPINPUT inputs, int inputSize) {
            return ::SendInput(count, inputs, inputSize);
        });
}

void WindowsInput::ResetSendInputFunctionForTesting() {
    SetSendInputFunctionForTesting({});
}

PermissionStatus CheckPermissions(bool) { return {true, true}; }
DesktopSessionState GetDesktopSessionState() {
    DesktopSessionState state;
    state.detectionSupported = true;

    DWORD processSessionId = 0;
    const bool hasProcessSession =
        ProcessIdToSessionId(GetCurrentProcessId(), &processSessionId) != FALSE;
    const DWORD consoleSessionId = WTSGetActiveConsoleSessionId();
    // Session zero is non-interactive. Any other session attached to this
    // process may be usable, including RDP even though it is not the console.
    state.available = hasProcessSession && processSessionId != 0;
    state.onConsole = state.available && consoleSessionId != 0xFFFFFFFF &&
        processSessionId == consoleSessionId;
    state.interactive = state.available;
    // A WTS query failure is unknown rather than proof that login is absent.
    state.loginDone = state.available;

    if (state.available) {
        LPWSTR buffer = nullptr;
        DWORD bytes = 0;
        if (WTSQuerySessionInformationW(
                WTS_CURRENT_SERVER_HANDLE,
                processSessionId,
                WTSSessionInfoEx,
                &buffer,
                &bytes) != FALSE &&
            buffer != nullptr &&
            bytes >= sizeof(WTSINFOEXW)) {
            const auto* info = reinterpret_cast<const WTSINFOEXW*>(buffer);
            if (info->Level == 1) {
                const auto& level = info->Data.WTSInfoExLevel1;
                state.loginDone = level.SessionState == WTSActive;
                state.interactive = state.loginDone;
                state.screenLocked =
                    level.SessionFlags == WTS_SESSIONSTATE_LOCK;
            }
        }
        if (buffer != nullptr) {
            WTSFreeMemory(buffer);
        }
    }

    BOOL screenSaverRunning = FALSE;
    if (SystemParametersInfoW(
            SPI_GETSCREENSAVERRUNNING,
            0,
            &screenSaverRunning,
            0) != FALSE) {
        state.screenSaverActive = screenSaverRunning != FALSE;
    }
    return state;
}

bool ComUsable(const ComScope& com) {
    return SUCCEEDED(com.hr) || com.hr == RPC_E_CHANGED_MODE;
}

std::string ShellItemDisplayName(IShellItem* item, SIGDN kind) {
    if (!item) {
        return {};
    }
    PWSTR raw = nullptr;
    std::string result;
    if (SUCCEEDED(item->GetDisplayName(kind, &raw)) && raw) {
        result = Windows::WideToUtf8(raw);
    }
    CoTaskMemFree(raw);
    return result;
}

std::string ShellItemProperty(IShellItem2* item, REFPROPERTYKEY key) {
    if (!item) {
        return {};
    }
    PWSTR raw = nullptr;
    std::string result;
    if (SUCCEEDED(item->GetString(key, &raw)) && raw) {
        result = Windows::WideToUtf8(raw);
    }
    CoTaskMemFree(raw);
    return result;
}

std::vector<WindowsApps::CatalogEntry> EnumerateInstalledApps() {
    std::vector<WindowsApps::CatalogEntry> entries;
    ComScope com;
    if (!ComUsable(com)) {
        return entries;
    }

    ComPtr<IShellItem> appsFolderItem;
    if (FAILED(SHGetKnownFolderItem(
            FOLDERID_AppsFolder,
            KF_FLAG_DEFAULT,
            nullptr,
            __uuidof(IShellItem),
            reinterpret_cast<void**>(appsFolderItem.put()))) ||
        !appsFolderItem) {
        return entries;
    }

    ComPtr<IShellFolder> appsFolder;
    if (FAILED(appsFolderItem->BindToHandler(
            nullptr,
            BHID_SFObject,
            __uuidof(IShellFolder),
            reinterpret_cast<void**>(appsFolder.put()))) ||
        !appsFolder) {
        return entries;
    }

    ComPtr<IEnumIDList> children;
    if (FAILED(appsFolder->EnumObjects(
            nullptr,
            SHCONTF_NONFOLDERS,
            children.put())) ||
        !children) {
        return entries;
    }

    PITEMID_CHILD child = nullptr;
    ULONG fetched = 0;
    while (children->Next(1, &child, &fetched) == S_OK) {
        ComPtr<IShellItem2> item;
        if (SUCCEEDED(SHCreateItemWithParent(
                nullptr,
                appsFolder.get(),
                child,
                __uuidof(IShellItem2),
                reinterpret_cast<void**>(item.put()))) &&
            item) {
            WindowsApps::CatalogEntry entry;
            entry.displayName = ShellItemDisplayName(
                item.get(), SIGDN_NORMALDISPLAY);
            entry.appUserModelId = ShellItemProperty(
                item.get(), PKEY_AppUserModel_ID);
            entry.executablePath = ShellItemProperty(
                item.get(), PKEY_Link_TargetParsingPath);
            entry.parsingName = ShellItemDisplayName(
                item.get(), SIGDN_DESKTOPABSOLUTEPARSING);
            if (!entry.displayName.empty()) {
                entries.push_back(std::move(entry));
            }
        }
        CoTaskMemFree(child);
        child = nullptr;
        fetched = 0;
    }

    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return Lowercase(left.displayName) < Lowercase(right.displayName);
    });
    return entries;
}

std::vector<WindowsApps::CatalogEntry> InstalledApps(bool forceRefresh = false) {
    struct Cache {
        std::mutex mutex;
        std::vector<WindowsApps::CatalogEntry> entries;
        std::chrono::steady_clock::time_point refreshedAt{};
        bool loaded = false;
    };
    static Cache cache;
    std::lock_guard lock(cache.mutex);
    const bool expired = !cache.loaded ||
        std::chrono::steady_clock::now() - cache.refreshedAt >
            std::chrono::minutes(5);
    if (forceRefresh || expired) {
        cache.entries = EnumerateInstalledApps();
        cache.refreshedAt = std::chrono::steady_clock::now();
        cache.loaded = true;
    }
    return cache.entries;
}

WindowsApps::CatalogMatch ResolveInstalledApp(const std::string& query) {
    auto match = WindowsApps::MatchCatalog(InstalledApps(), query);
    if (!match.entry && !match.ambiguous) {
        match = WindowsApps::MatchCatalog(InstalledApps(true), query);
    }
    return match;
}

std::string AppUserModelIdForWindow(HWND hwnd) {
    if (!hwnd) {
        return {};
    }
    ComPtr<IPropertyStore> properties;
    if (FAILED(SHGetPropertyStoreForWindow(
            hwnd,
            __uuidof(IPropertyStore),
            reinterpret_cast<void**>(properties.put()))) ||
        !properties) {
        return {};
    }

    PROPVARIANT value;
    PropVariantInit(&value);
    std::string appUserModelId;
    if (SUCCEEDED(properties->GetValue(PKEY_AppUserModel_ID, &value))) {
        if (value.vt == VT_LPWSTR && value.pwszVal) {
            appUserModelId = Windows::WideToUtf8(value.pwszVal);
        } else if (value.vt == VT_BSTR && value.bstrVal) {
            appUserModelId = Windows::WideToUtf8(value.bstrVal);
        }
    }
    PropVariantClear(&value);
    return appUserModelId;
}

AppInfo AppInfoForWindow(HWND hwnd) {
    if (!hwnd) {
        return {};
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    AppInfo info;
    info.available = true;
    info.pid = static_cast<int>(pid);
    info.executable = ProcessName(pid);

    ComScope com;
    std::string appUserModelId;
    if (ComUsable(com)) {
        appUserModelId = AppUserModelIdForWindow(hwnd);
    }
    if (!appUserModelId.empty()) {
        info.bundleId = appUserModelId;
        auto match = WindowsApps::MatchCatalog(
            InstalledApps(), appUserModelId);
        info.name = match.entry
            ? match.entry->displayName
            : appUserModelId;
        return info;
    }

    info.name = info.executable;
    info.bundleId = info.name;
    return info;
}

bool ShellExecuteTarget(const std::string& target, int* launchedPid = nullptr) {
    if (launchedPid) {
        *launchedPid = -1;
    }
    if (target.empty()) {
        return false;
    }
    const std::wstring wide = Utf8ToWide(target);
    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    execute.lpVerb = L"open";
    execute.lpFile = wide.c_str();
    execute.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&execute) == FALSE) {
        return false;
    }
    if (execute.hProcess) {
        if (launchedPid) {
            const DWORD processId = GetProcessId(execute.hProcess);
            if (processId > 0) {
                *launchedPid = static_cast<int>(processId);
            }
        }
        CloseHandle(execute.hProcess);
    }
    return true;
}

bool ActivateCatalogEntry(
    const WindowsApps::CatalogEntry& entry,
    AppInfo& appInfo) {
    if (!entry.appUserModelId.empty() &&
        entry.appUserModelId.find('!') != std::string::npos) {
        ComScope com;
        if (!ComUsable(com)) {
            return false;
        }
        ComPtr<IApplicationActivationManager> manager;
        if (FAILED(CoCreateInstance(
                CLSID_ApplicationActivationManager,
                nullptr,
                CLSCTX_LOCAL_SERVER,
                __uuidof(IApplicationActivationManager),
                reinterpret_cast<void**>(manager.put()))) ||
            !manager) {
            return false;
        }
        DWORD pid = 0;
        const std::wstring appUserModelId =
            Utf8ToWide(entry.appUserModelId);
        if (FAILED(manager->ActivateApplication(
                appUserModelId.c_str(),
                nullptr,
                AO_NONE,
                &pid))) {
            return false;
        }
        appInfo.available = true;
        appInfo.pid = static_cast<int>(pid);
        appInfo.name = entry.displayName;
        appInfo.bundleId = entry.appUserModelId;
        appInfo.executable = !entry.executablePath.empty()
            ? std::filesystem::path(entry.executablePath).filename().string()
            : std::string{};
        return true;
    }

    const std::string target = !entry.executablePath.empty()
        ? entry.executablePath
        : entry.parsingName;
    int launchedPid = -1;
    if (!ShellExecuteTarget(target, &launchedPid)) {
        return false;
    }
    appInfo.available = true;
    appInfo.pid = launchedPid;
    appInfo.name = entry.displayName;
    appInfo.bundleId = !entry.appUserModelId.empty()
        ? entry.appUserModelId
        : target;
    appInfo.executable = !entry.executablePath.empty()
        ? std::filesystem::path(entry.executablePath).filename().string()
        : std::string{};
    return true;
}

bool WakeDesktopSession(bool force) {
    const DesktopSessionState state = GetDesktopSessionState();
    if (!state.available || (!state.onConsole && !state.interactive) || !state.loginDone ||
        (state.screenLocked && !state.screenSaverActive && !force)) {
        return false;
    }

    // ES_DISPLAY_REQUIRED resets the display idle timer and brings a powered-
    // down monitor back without changing authentication state.
    const bool displayRequested = SetThreadExecutionState(
        ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED) != 0;

    bool activitySent = false;
    if (state.screenSaverActive) {
        // Native user activity dismisses a screen saver. A paired modifier
        // press has no text effect and cannot authenticate a locked desktop.
        INPUT activity[2]{};
        activity[0].type = INPUT_KEYBOARD;
        activity[0].ki.wVk = VK_SHIFT;
        activity[1].type = INPUT_KEYBOARD;
        activity[1].ki.wVk = VK_SHIFT;
        activity[1].ki.dwFlags = KEYEVENTF_KEYUP;
        activitySent = SendInput(2, activity, sizeof(INPUT)) == 2;
    }
    return displayRequested || activitySent;
}
bool OpenPermissionsSettings() { return OpenSettingsUri(L"ms-settings:easeofaccess"); }
bool OpenAccessibilitySettings() { return OpenPermissionsSettings(); }
bool OpenScreenCaptureSettings() { return true; }
bool RequestAccessibilityPermission() { return true; }
bool RequestScreenCapturePermission() { return true; }

AppInfo GetFrontmostApp() {
    HWND hwnd = GetForegroundWindow();
    return AppInfoForWindow(hwnd);
}

std::string GetFrontmostAppSummary() {
    auto app = GetFrontmostApp();
    return app.available ? app.name + " [" + app.bundleId + "] pid=" + std::to_string(app.pid) : "unavailable";
}

FocusedElementInfo GetFocusedElementInfo() {
    ComScope com;
    if (FAILED(com.hr)) {
        return {};
    }
    ComPtr<IUIAutomation> automation;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), reinterpret_cast<void**>(automation.put())))) {
        return {};
    }
    ComPtr<IUIAutomationElement> element;
    if (FAILED(automation->GetFocusedElement(element.put())) || !element) {
        return {};
    }
    FocusedElementInfo info;
    info.available = true;
    info.bounds = UiaBounds(element.get());
    info.role = UiaRole(element.get());
    info.title = UiaName(element.get());
    info.description = info.title;
    ComPtr<IUIAutomationValuePattern> value;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId, __uuidof(IUIAutomationValuePattern), reinterpret_cast<void**>(value.put()))) && value) {
        info.valueSettable = true;
        info.acceptsTextInput = true;
        BSTR raw = nullptr;
        if (SUCCEEDED(value->get_CurrentValue(&raw))) {
            info.value = BstrToUtf8(raw);
        }
    }
    return info;
}

Bounds GetFrontmostWindowBounds() {
    HWND hwnd = GetForegroundWindow();
    RECT rect{};
    return hwnd && GetWindowRect(hwnd, &rect) ? RectToBounds(rect) : Bounds{};
}

WindowInfo GetActiveWindow() {
    HWND hwnd = GetForegroundWindow();
    return hwnd ? WindowInfoFor(hwnd) : WindowInfo{};
}

std::vector<WindowInfo> ListWindows(const std::string& appQuery) {
    std::vector<WindowInfo> windows;
    std::string query = Lowercase(appQuery);
    std::pair<std::vector<WindowInfo>*, std::string> data{&windows, query};
    EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
        auto* data = reinterpret_cast<std::pair<std::vector<WindowInfo>*, std::string>*>(param);
        if (!IsRealWindow(hwnd)) {
            return TRUE;
        }
        WindowInfo info = WindowInfoFor(hwnd);
        std::string haystack = Lowercase(info.title + " " + info.appClass);
        if (data->second.empty() || haystack.find(data->second) != std::string::npos) {
            data->first->push_back(std::move(info));
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));
    return windows;
}

bool ActivateWindow(const std::string& id) {
    auto hwnd = HwndFromId(id);
    return hwnd && ActivateWindow(*hwnd);
}

bool CloseWindow(const std::string& id) {
    auto hwnd = HwndFromId(id);
    return hwnd && PostMessageW(*hwnd, WM_CLOSE, 0, 0);
}

bool SetFrontmostWindowBounds(const Bounds& bounds) {
    HWND hwnd = GetForegroundWindow();
    return hwnd && SetWindowPos(hwnd, nullptr, static_cast<int>(bounds.x), static_cast<int>(bounds.y), static_cast<int>(bounds.width), static_cast<int>(bounds.height), SWP_NOZORDER | SWP_NOACTIVATE);
}

bool SetWindowBoundsForPid(int pid, const Bounds& bounds) {
    for (const auto& window : ListWindows("")) {
        if (window.pid == pid) {
            auto hwnd = HwndFromId(window.id);
            return hwnd && SetWindowPos(*hwnd, nullptr, static_cast<int>(bounds.x), static_cast<int>(bounds.y), static_cast<int>(bounds.width), static_cast<int>(bounds.height), SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    return false;
}

int GetFrontmostAppPid() { return GetFrontmostApp().pid; }

bool ActivateAppByPid(int pid) {
    for (const auto& window : ListWindows("")) {
        if (window.pid == pid) {
            auto hwnd = HwndFromId(window.id);
            return hwnd && ActivateWindow(*hwnd);
        }
    }
    return false;
}

bool LaunchOrActivateApp(const std::string& query, AppInfo& appInfo) {
    const auto directWindows = ListWindows(query);
    for (const auto& window : directWindows) {
        if (auto hwnd = HwndFromId(window.id)) {
            if (!ActivateWindow(*hwnd)) {
                continue;
            }
            appInfo = AppInfoForWindow(*hwnd);
            return true;
        }
    }
    if (!directWindows.empty()) {
        return false;
    }

    int launchedPid = -1;
    if (ShellExecuteTarget(query, &launchedPid)) {
        appInfo.available = true;
        appInfo.pid = launchedPid;
        appInfo.name = query;
        appInfo.bundleId = query;
        appInfo.executable = std::filesystem::path(query).filename().string();
        return true;
    }

    const auto resolved = ResolveInstalledApp(query);
    if (!resolved.entry) {
        return false;
    }

    const auto catalogWindows = ListWindows(resolved.entry->displayName);
    for (const auto& window : catalogWindows) {
        if (auto hwnd = HwndFromId(window.id)) {
            if (!ActivateWindow(*hwnd)) {
                continue;
            }
            appInfo = AppInfoForWindow(*hwnd);
            return true;
        }
    }
    if (!catalogWindows.empty()) {
        return false;
    }
    return ActivateCatalogEntry(*resolved.entry, appInfo);
}

bool OpenUrl(const std::string& url, const std::string&, bool, bool) {
    std::wstring wide = Utf8ToWide(url);
    return reinterpret_cast<intptr_t>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

void ActivateAgentApp() {}
void DeactivateAgentApp() {}

void GetScreenSize(int& width, int& height) {
    width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
}

void GetCursorPosition(double& x, double& y) {
    POINT point{};
    GetCursorPos(&point);
    x = point.x;
    y = point.y;
}

void MoveMouse(double x, double y) { SetCursorPos(static_cast<int>(std::round(x)), static_cast<int>(std::round(y))); }

void MoveMouseSmooth(double x, double y, int durationMs, int steps) {
    double startX = 0.0;
    double startY = 0.0;
    GetCursorPosition(startX, startY);
    int safeSteps = std::max(1, steps);
    int sleepMs = durationMs > 0 ? std::max(1, durationMs / safeSteps) : 1;
    for (int i = 1; i <= safeSteps; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(safeSteps);
        MoveMouse(startX + (x - startX) * t, startY + (y - startY) * t);
        std::this_thread::sleep_for(std::chrono::milliseconds(HumanInput::ScaledDelayMs(sleepMs)));
    }
}

void DragMouseSmooth(double fromX, double fromY, double toX, double toY, const std::string& button, int durationMs, int steps) {
    MoveMouse(fromX, fromY);
    MouseDown(button);
    MoveMouseSmooth(toX, toY, durationMs, steps);
    MouseUp(button);
}

bool ClickSmooth(double x, double y, const std::string& button, int clickCount, int durationMs, int steps) {
    MoveMouseSmooth(x, y, durationMs, steps);
    return Click(x, y, button, clickCount);
}

void MouseDown(const std::string& button, int) { SendMouseButton(button, true); }
void MouseUp(const std::string& button, int) { SendMouseButton(button, false); }

void Scroll(int deltaY, int deltaX) {
    if (deltaY != 0) {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = static_cast<DWORD>(deltaY);
        SendInput(1, &input, sizeof(INPUT));
    }
    if (deltaX != 0) {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        input.mi.mouseData = static_cast<DWORD>(deltaX);
        SendInput(1, &input, sizeof(INPUT));
    }
}

void ScrollGesture(int deltaY, int deltaX, int durationMs, int steps, double) {
    int safeSteps = std::max(1, steps);
    int sleepMs = durationMs > 0 ? std::max(1, durationMs / safeSteps) : 1;
    for (int i = 0; i < safeSteps; ++i) {
        Scroll(deltaY / safeSteps, deltaX / safeSteps);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
}

bool Click(double x, double y, const std::string& button, int clickCount) {
    MoveMouse(x, y);
    for (int i = 0; i < std::max(1, clickCount); ++i) {
        MouseDown(button);
        std::this_thread::sleep_for(std::chrono::milliseconds(HumanInput::ClickHoldMs()));
        MouseUp(button);
        if (i + 1 < clickCount) {
            std::this_thread::sleep_for(std::chrono::milliseconds(HumanInput::MultiClickGapMs()));
        }
    }
    return true;
}

int ResolveKeycode(const std::string& keyName) {
    auto vk = KeyNameToVirtualKey(keyName);
    return vk ? static_cast<int>(*vk) : -1;
}

bool SendHotkey(const std::vector<std::string>& keys, int holdMs) {
    std::vector<WORD> vks;
    for (const auto& key : keys) {
        auto vk = KeyNameToVirtualKey(key);
        if (!vk) return false;
        vks.push_back(*vk);
    }
    if (vks.empty()) return false;
    std::vector<WORD> pressed;
    for (WORD vk : vks) {
        if (!SendVirtualKey(vk, true)) {
            for (auto it = pressed.rbegin(); it != pressed.rend(); ++it) {
                SendVirtualKey(*it, false);
            }
            return false;
        }
        pressed.push_back(vk);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, holdMs)));
    bool released = true;
    for (auto it = pressed.rbegin(); it != pressed.rend(); ++it) {
        released = SendVirtualKey(*it, false) && released;
    }
    return released;
}

bool TypeCharacter(const std::string& character, int holdMs) {
    std::wstring wide = Utf8ToWide(character);
    for (wchar_t ch : wide) {
        if (!SendUnicodeChar(ch, true)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, holdMs)));
        if (!SendUnicodeChar(ch, false)) return false;
    }
    return !wide.empty();
}

bool TypeText(const std::string& text, int holdMs) {
    std::wstring wide = Utf8ToWide(text);
    for (wchar_t ch : wide) {
        if (!SendUnicodeChar(ch, true)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, holdMs)));
        if (!SendUnicodeChar(ch, false)) return false;
    }
    return true;
}

bool PasteText(const std::string& text) {
    if (!WriteClipboardText(text)) return false;
    return SendHotkey({"control", "v"}, HumanInput::ClickHoldMs());
}

std::vector<std::string> GetSelectAllHotkey() { return {"control", "a"}; }

bool ReadClipboardText(std::string& text) {
    auto wide = ClipboardText();
    if (!wide) return false;
    text = Windows::WideToUtf8(*wide);
    return true;
}

bool WriteClipboardText(const std::string& text) {
    std::wstring wide = Utf8ToWide(text);
    if (!OpenClipboard(nullptr)) {
        return false;
    }
    EmptyClipboard();
    size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!global) {
        CloseClipboard();
        return false;
    }
    void* memory = GlobalLock(global);
    if (!memory) {
        GlobalFree(global);
        CloseClipboard();
        return false;
    }
    std::memcpy(memory, wide.c_str(), bytes);
    GlobalUnlock(global);
    if (!SetClipboardData(CF_UNICODETEXT, global)) {
        GlobalFree(global);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

bool SaveScreenshot(const std::string& filePath) {
    int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return Image::WritePngRgb(filePath, CaptureRegion(left, top, width, height));
}

bool SaveScreenshotRegion(const std::string& filePath, const Bounds& bounds) {
    return bounds.available && Image::WritePngRgb(filePath, CaptureRegion(static_cast<int>(bounds.x), static_cast<int>(bounds.y), static_cast<int>(bounds.width), static_cast<int>(bounds.height)));
}

bool SaveScreenshotScaled(const std::string& filePath, int maxDimension) {
    int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return Image::WritePngRgb(filePath, ResizeRgb(CaptureRegion(left, top, width, height), maxDimension));
}

bool SaveScreenshotRegionScaled(const std::string& filePath, const Bounds& bounds, int maxDimension) {
    return bounds.available && Image::WritePngRgb(filePath, ResizeRgb(CaptureRegion(static_cast<int>(bounds.x), static_cast<int>(bounds.y), static_cast<int>(bounds.width), static_cast<int>(bounds.height)), maxDimension));
}

std::vector<uint8_t> CaptureScreenRaw(int& width, int& height) {
    int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    Image::RgbImage image = CaptureRegion(left, top, GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
    width = image.width;
    height = image.height;
    return image.rgb;
}

SnapshotResult TakeSnapshot(const SnapshotOptions& options) {
    SnapshotResult result;
    result.frontmostApp = GetFrontmostApp();
    result.frontmostWindowBounds = GetFrontmostWindowBounds();
    result.text = "Computer: local\n";
    ComScope com;
    if (FAILED(com.hr)) {
        result.warning = "UI Automation initialization failed.";
        return result;
    }
    ComPtr<IUIAutomation> automation;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), reinterpret_cast<void**>(automation.put())))) {
        result.warning = "UI Automation is unavailable.";
        return result;
    }
    ComPtr<IUIAutomationElement> root;
    HWND hwnd = GetForegroundWindow();
    if (hwnd) {
        automation->ElementFromHandle(hwnd, root.put());
    }
    if (!root) {
        automation->GetRootElement(root.put());
    }
    int count = 0;
    AppendElementLines(automation.get(), root.get(), result, 0, std::max(0, options.maxDepth), count, std::max(1, options.maxNodes));
    return result;
}

}
