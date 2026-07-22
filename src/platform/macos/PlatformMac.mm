#include "computer_cpp/Platform.h"

#include "computer_cpp/AppPaths.h"
#include "computer_cpp/HumanInput.h"
#include "computer_cpp/StringUtils.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <dlfcn.h>
#include <filesystem>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <thread>
#include <utility>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <dispatch/dispatch.h>

namespace ComputerCpp::Platform {

namespace {

std::map<std::string, int> kKeycodes = {
    {"a", 0}, {"s", 1}, {"d", 2}, {"f", 3}, {"h", 4}, {"g", 5}, {"z", 6}, {"x", 7},
    {"c", 8}, {"v", 9}, {"b", 11}, {"q", 12}, {"w", 13}, {"e", 14}, {"r", 15}, {"y", 16},
    {"t", 17}, {"1", 18}, {"2", 19}, {"3", 20}, {"4", 21}, {"6", 22}, {"5", 23}, {"=", 24},
    {"9", 25}, {"7", 26}, {"-", 27}, {"8", 28}, {"0", 29}, {"]", 30}, {"o", 31}, {"u", 32},
    {"[", 33}, {"i", 34}, {"p", 35}, {"enter", 36}, {"return", 36}, {"l", 37}, {"j", 38},
    {"'", 39}, {"k", 40}, {";", 41}, {"\\", 42}, {",", 43}, {"/", 44}, {"n", 45}, {"m", 46},
    {".", 47}, {"tab", 48}, {"space", 49}, {"`", 50}, {"delete", 51}, {"backspace", 51},
    {"escape", 53}, {"esc", 53}, {"command", 55}, {"cmd", 55}, {"shift", 56},
    {"caps_lock", 57}, {"option", 58}, {"alt", 58}, {"control", 59}, {"ctrl", 59},
    {"right_shift", 60}, {"right_option", 61}, {"right_control", 62}, {"function", 63},
    {"help", 114}, {"home", 115}, {"page_up", 116}, {"pageup", 116}, {"pgup", 116},
    {"forward_delete", 117}, {"del", 117}, {"end", 119}, {"page_down", 121},
    {"pagedown", 121}, {"pgdn", 121}, {"left", 123}, {"right", 124}, {"down", 125}, {"up", 126}
};

bool gLeftDown = false;
bool gRightDown = false;
bool gMiddleDown = false;

struct HotkeyKey {
    CGKeyCode code;
    CGEventFlags flag;
    bool isModifier;
};

struct CharacterKeyStroke {
    CGKeyCode keycode;
    CGEventFlags flags;
};

struct CFReleaser {
    template <typename T>
    void operator()(T ref) const {
        if (ref) {
            CFRelease(ref);
        }
    }
};

struct CGImageReleaser {
    void operator()(CGImageRef ref) const {
        if (ref) {
            CGImageRelease(ref);
        }
    }
};

struct CGContextReleaser {
    void operator()(CGContextRef ref) const {
        if (ref) {
            CGContextRelease(ref);
        }
    }
};

template <typename T, typename Releaser>
class ScopedRef {
public:
    ScopedRef() = default;
    explicit ScopedRef(T ref) : ref_(ref) {}
    ~ScopedRef() {
        reset();
    }

    ScopedRef(const ScopedRef&) = delete;
    ScopedRef& operator=(const ScopedRef&) = delete;

    ScopedRef(ScopedRef&& other) noexcept : ref_(std::exchange(other.ref_, nullptr)) {}

    ScopedRef& operator=(ScopedRef&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.ref_, nullptr));
        }
        return *this;
    }

    T get() const {
        return ref_;
    }

    explicit operator bool() const {
        return ref_ != nullptr;
    }

    T release() {
        return std::exchange(ref_, nullptr);
    }

    void reset(T ref = nullptr) {
        if (ref_) {
            Releaser{}(ref_);
        }
        ref_ = ref;
    }

private:
    T ref_ = nullptr;
};

template <typename T>
using ScopedCFRef = ScopedRef<T, CFReleaser>;

using ScopedCGImageRef = ScopedRef<CGImageRef, CGImageReleaser>;
using ScopedCGContextRef = ScopedRef<CGContextRef, CGContextReleaser>;

std::string NSStringToString(NSString* value) {
    if (!value) {
        return "";
    }
    const char* utf8 = [value UTF8String];
    return utf8 ? std::string(utf8) : "";
}

std::string NSErrorToString(NSError* error) {
    if (!error) {
        return "";
    }
    NSString* description = error.localizedDescription ? error.localizedDescription : @"";
    NSString* value = [NSString stringWithFormat:@"%@ code=%ld: %@",
                                                 error.domain,
                                                 static_cast<long>(error.code),
                                                 description];
    return NSStringToString(value);
}

std::string PermissionBoolString(bool value) {
    return value ? "yes" : "no";
}

std::string PermissionTraceTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&time, &local);

    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

std::string PermissionRuntimeSummary() {
    NSBundle* bundle = [NSBundle mainBundle];
    NSRunningApplication* current = [NSRunningApplication currentApplication];
    NSRunningApplication* frontmost = [[NSWorkspace sharedWorkspace] frontmostApplication];

    std::ostringstream out;
    out << "pid=" << [[NSProcessInfo processInfo] processIdentifier]
        << " main_thread=" << PermissionBoolString([NSThread isMainThread])
        << " nsapp_running=" << PermissionBoolString(NSApp && [NSApp isRunning])
        << " nsapp_active=" << PermissionBoolString(NSApp && [NSApp isActive])
        << " current_active=" << PermissionBoolString(current && [current isActive])
        << " bundle_id=" << NSStringToString([bundle bundleIdentifier])
        << " bundle_path=" << NSStringToString([bundle bundlePath])
        << " executable_path=" << NSStringToString([bundle executablePath]);

    if (frontmost) {
        out << " frontmost_name=" << NSStringToString([frontmost localizedName])
            << " frontmost_bundle=" << NSStringToString([frontmost bundleIdentifier])
            << " frontmost_pid=" << [frontmost processIdentifier];
    }

    return out.str();
}

void AppendPermissionTrace(const std::string& event) {
    try {
        std::filesystem::path logPath = ComputerCpp::AppLogPath();
        std::filesystem::create_directories(logPath.parent_path());
        std::ofstream log(logPath, std::ios::app);
        log << PermissionTraceTimestamp() << " computer.cpp permissions platform event=" << event
            << " runtime={" << PermissionRuntimeSummary() << "}\n";
    } catch (...) {
    }
}

NSURL* ResolveApplicationUrl(const std::string& query) {
    if (query.empty()) {
        return nil;
    }

    NSString* nsQuery = [NSString stringWithUTF8String:query.c_str()];
    if (!nsQuery) {
        return nil;
    }

    NSURL* explicitUrl = [NSURL fileURLWithPath:[nsQuery stringByExpandingTildeInPath]];
    if (explicitUrl && [[NSFileManager defaultManager] fileExistsAtPath:explicitUrl.path]) {
        return explicitUrl;
    }

    if (query.find('.') != std::string::npos) {
        NSArray<NSURL*>* urls = [[NSWorkspace sharedWorkspace] URLsForApplicationsWithBundleIdentifier:nsQuery];
        if (urls.firstObject) {
            return urls.firstObject;
        }
    }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    NSString* path = [[NSWorkspace sharedWorkspace] fullPathForApplication:nsQuery];
#pragma clang diagnostic pop
    return path ? [NSURL fileURLWithPath:path] : nil;
}

std::string NormalizeKeyName(std::string keyName) {
    std::string normalized = ComputerCpp::Trim(keyName);
    normalized = ComputerCpp::Lowercase(normalized);
    if (normalized == "spacebar") {
        return "space";
    }
    if (normalized == "cmd") {
        return "command";
    }
    if (normalized == "primary") {
        return "command";
    }
    if (normalized == "ctrl") {
        return "control";
    }
    return normalized;
}

CGEventFlags ModifierFlagForKeycode(int keycode) {
    switch (keycode) {
        case 55: return kCGEventFlagMaskCommand;
        case 56:
        case 60: return kCGEventFlagMaskShift;
        case 57: return kCGEventFlagMaskAlphaShift;
        case 58:
        case 61: return kCGEventFlagMaskAlternate;
        case 59:
        case 62: return kCGEventFlagMaskControl;
#ifdef kCGEventFlagMaskSecondaryFn
        case 63: return kCGEventFlagMaskSecondaryFn;
#endif
        default: return 0;
    }
}

void PostKeyboardEvent(CGKeyCode keycode, bool down, CGEventFlags flags) {
    ScopedCFRef<CGEventRef> event(CGEventCreateKeyboardEvent(nullptr, keycode, down));
    if (!event) {
        return;
    }
    CGEventSetFlags(event.get(), flags);
    CGEventPost(kCGHIDEventTap, event.get());
}

UInt32 CarbonModifierStateForFlags(CGEventFlags flags) {
    UInt32 state = 0;
    if (flags & kCGEventFlagMaskShift) state |= shiftKey;
    if (flags & kCGEventFlagMaskAlternate) state |= optionKey;
    if (flags & kCGEventFlagMaskControl) state |= controlKey;
    if (flags & kCGEventFlagMaskCommand) state |= cmdKey;
    if (flags & kCGEventFlagMaskAlphaShift) state |= alphaLock;
    return state;
}

std::vector<HotkeyKey> BuildModifierSequence(CGEventFlags flags) {
    std::vector<HotkeyKey> modifiers;
    if (flags & kCGEventFlagMaskControl) modifiers.push_back({59, kCGEventFlagMaskControl, true});
    if (flags & kCGEventFlagMaskAlternate) modifiers.push_back({58, kCGEventFlagMaskAlternate, true});
    if (flags & kCGEventFlagMaskShift) modifiers.push_back({56, kCGEventFlagMaskShift, true});
    if (flags & kCGEventFlagMaskCommand) modifiers.push_back({55, kCGEventFlagMaskCommand, true});
    return modifiers;
}

void PressModifiers(const std::vector<HotkeyKey>& modifiers, CGEventFlags& activeFlags) {
    activeFlags = 0;
    for (const auto& modifier : modifiers) {
        activeFlags |= modifier.flag;
        PostKeyboardEvent(modifier.code, true, activeFlags);
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }
}

void ReleaseModifiers(const std::vector<HotkeyKey>& modifiers, CGEventFlags& activeFlags) {
    for (auto it = modifiers.rbegin(); it != modifiers.rend(); ++it) {
        PostKeyboardEvent(it->code, false, activeFlags);
        activeFlags &= ~it->flag;
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
}

std::optional<CharacterKeyStroke> ResolveCharacterKeyStrokeOnMainThread(const std::string& character) {
    NSString* nsStr = [NSString stringWithUTF8String:character.c_str()];
    if (!nsStr || [nsStr length] != 1) {
        return std::nullopt;
    }

    UniChar target = [nsStr characterAtIndex:0];
    ScopedCFRef<TISInputSourceRef> inputSource(TISCopyCurrentKeyboardLayoutInputSource());
    if (!inputSource) {
        return std::nullopt;
    }

    CFDataRef layoutData = static_cast<CFDataRef>(TISGetInputSourceProperty(inputSource.get(), kTISPropertyUnicodeKeyLayoutData));
    if (!layoutData) {
        return std::nullopt;
    }

    const auto* keyboardLayout = reinterpret_cast<const UCKeyboardLayout*>(CFDataGetBytePtr(layoutData));
    const std::vector<CGEventFlags> candidates = {
        0,
        kCGEventFlagMaskShift,
        kCGEventFlagMaskAlternate,
        kCGEventFlagMaskShift | kCGEventFlagMaskAlternate
    };

    for (CGEventFlags flags : candidates) {
        UInt32 modifierState = CarbonModifierStateForFlags(flags);
        for (UInt16 keycode = 0; keycode < 128; ++keycode) {
            UInt32 deadKeyState = 0;
            UniChar chars[4] = {};
            UniCharCount length = 0;
            OSStatus status = UCKeyTranslate(
                keyboardLayout,
                keycode,
                kUCKeyActionDown,
                (modifierState >> 8) & 0xFF,
                LMGetKbdType(),
                kUCKeyTranslateNoDeadKeysMask,
                &deadKeyState,
                4,
                &length,
                chars);

            if (status == noErr && length == 1 && chars[0] == target) {
                return CharacterKeyStroke{static_cast<CGKeyCode>(keycode), flags};
            }
        }
    }

    return std::nullopt;
}

std::optional<CharacterKeyStroke> ResolveCharacterKeyStroke(const std::string& character) {
    if ([NSThread isMainThread]) {
        return ResolveCharacterKeyStrokeOnMainThread(character);
    }

    __block std::optional<CharacterKeyStroke> resolved = std::nullopt;
    std::string characterCopy = character;
    dispatch_sync(dispatch_get_main_queue(), ^{
        resolved = ResolveCharacterKeyStrokeOnMainThread(characterCopy);
    });
    return resolved;
}

void PostMouseEvent(CGEventType type, CGMouseButton button, double x, double y, int clickCount) {
    ScopedCFRef<CGEventRef> event(CGEventCreateMouseEvent(nullptr, type, CGPointMake(x, y), button));
    if (!event) {
        return;
    }
    CGEventSetIntegerValueField(event.get(), kCGMouseEventClickState, clickCount);
    CGEventPost(kCGHIDEventTap, event.get());
}

std::string CopyStringAttribute(AXUIElementRef element, CFStringRef attribute) {
    if (!element) {
        return "";
    }

    CFTypeRef rawValue = nullptr;
    AXError error = AXUIElementCopyAttributeValue(element, attribute, &rawValue);
    ScopedCFRef<CFTypeRef> value(rawValue);
    if (error != kAXErrorSuccess || !value) {
        return "";
    }

    std::string result;
    if (CFGetTypeID(value.get()) == CFStringGetTypeID()) {
        result = NSStringToString((__bridge NSString*)value.get());
    } else if (CFGetTypeID(value.get()) == CFNumberGetTypeID()) {
        double numeric = 0.0;
        CFNumberGetValue(static_cast<CFNumberRef>(value.get()), kCFNumberDoubleType, &numeric);
        std::ostringstream out;
        out << numeric;
        result = out.str();
    } else if (CFGetTypeID(value.get()) == CFBooleanGetTypeID()) {
        result = CFBooleanGetValue(static_cast<CFBooleanRef>(value.get())) ? "true" : "false";
    }

    return result;
}

bool CopyBoolAttribute(AXUIElementRef element, CFStringRef attribute, bool defaultValue = false) {
    if (!element) {
        return defaultValue;
    }
    CFTypeRef rawValue = nullptr;
    AXError error = AXUIElementCopyAttributeValue(element, attribute, &rawValue);
    ScopedCFRef<CFTypeRef> value(rawValue);
    if (error != kAXErrorSuccess || !value) {
        return defaultValue;
    }
    bool result = defaultValue;
    if (CFGetTypeID(value.get()) == CFBooleanGetTypeID()) {
        result = CFBooleanGetValue(static_cast<CFBooleanRef>(value.get()));
    }
    return result;
}

Bounds CopyBounds(AXUIElementRef element) {
    Bounds bounds;
    CFTypeRef rawPositionValue = nullptr;
    CFTypeRef rawSizeValue = nullptr;
    AXError positionError = AXUIElementCopyAttributeValue(element, kAXPositionAttribute, &rawPositionValue);
    AXError sizeError = AXUIElementCopyAttributeValue(element, kAXSizeAttribute, &rawSizeValue);
    ScopedCFRef<CFTypeRef> positionValue(rawPositionValue);
    ScopedCFRef<CFTypeRef> sizeValue(rawSizeValue);
    if (positionError != kAXErrorSuccess ||
        sizeError != kAXErrorSuccess ||
        !positionValue ||
        !sizeValue) {
        return bounds;
    }

    CGPoint point = CGPointZero;
    CGSize size = CGSizeZero;
    bool ok = AXValueGetValue(static_cast<AXValueRef>(positionValue.get()), static_cast<AXValueType>(kAXValueCGPointType), &point) &&
              AXValueGetValue(static_cast<AXValueRef>(sizeValue.get()), static_cast<AXValueType>(kAXValueCGSizeType), &size);
    if (!ok) {
        return bounds;
    }

    bounds.available = true;
    bounds.x = point.x;
    bounds.y = point.y;
    bounds.width = size.width;
    bounds.height = size.height;
    return bounds;
}

std::string BestName(AXUIElementRef element) {
    std::string title = CopyStringAttribute(element, kAXTitleAttribute);
    if (!title.empty()) return title;
    std::string description = CopyStringAttribute(element, kAXDescriptionAttribute);
    if (!description.empty()) return description;
    std::string value = CopyStringAttribute(element, kAXValueAttribute);
    if (!value.empty() && value.size() <= 120) return value;
    return "";
}

bool IsTextInputRole(const std::string& role) {
    return role == "AXTextField" ||
           role == "AXTextArea" ||
           role == "AXSearchField" ||
           role == "AXComboBox";
}

bool IsInteractiveRole(const std::string& role) {
    return role == "AXButton" ||
           role == "AXCheckBox" ||
           role == "AXRadioButton" ||
           role == "AXTextField" ||
           role == "AXTextArea" ||
           role == "AXSearchField" ||
           role == "AXComboBox" ||
           role == "AXPopUpButton" ||
           role == "AXMenuButton" ||
           role == "AXLink" ||
           role == "AXSlider" ||
           role == "AXTab" ||
           role == "AXMenuItem" ||
           role == "AXOutline" ||
           role == "AXTable";
}

bool IsContentRole(const std::string& role) {
    return role == "AXStaticText" ||
           role == "AXHeading" ||
           role == "AXCell" ||
           role == "AXRow";
}

std::string RoleLabel(const std::string& role) {
    if (role.rfind("AX", 0) == 0) {
        return role.substr(2);
    }
    return role;
}

pid_t GetAccessibilityFocusedAppPid() {
    ScopedCFRef<AXUIElementRef> systemWide(AXUIElementCreateSystemWide());
    if (!systemWide) {
        return -1;
    }
    CFTypeRef rawFocusedAppValue = nullptr;
    AXError error = AXUIElementCopyAttributeValue(systemWide.get(), kAXFocusedApplicationAttribute, &rawFocusedAppValue);
    ScopedCFRef<CFTypeRef> focusedAppValue(rawFocusedAppValue);
    if (error != kAXErrorSuccess || !focusedAppValue || CFGetTypeID(focusedAppValue.get()) != AXUIElementGetTypeID()) {
        return -1;
    }
    pid_t pid = -1;
    AXUIElementGetPid(static_cast<AXUIElementRef>(focusedAppValue.get()), &pid);
    return pid;
}

std::vector<std::string> CopyActionNames(AXUIElementRef element) {
    std::vector<std::string> actions;
    CFArrayRef rawActionNames = nullptr;
    if (AXUIElementCopyActionNames(element, &rawActionNames) != kAXErrorSuccess || !rawActionNames) {
        return actions;
    }
    ScopedCFRef<CFArrayRef> actionNames(rawActionNames);
    CFIndex count = CFArrayGetCount(actionNames.get());
    for (CFIndex i = 0; i < count; ++i) {
        CFStringRef action = static_cast<CFStringRef>(CFArrayGetValueAtIndex(actionNames.get(), i));
        if (action && CFGetTypeID(action) == CFStringGetTypeID()) {
            actions.push_back(NSStringToString((__bridge NSString*)action));
        }
    }
    return actions;
}

void AppendElementSnapshot(AXUIElementRef element,
                           const SnapshotOptions& options,
                           const std::string& appName,
                           int pid,
                           int depth,
                           int& nextElementRef,
                           int& visitedNodes,
                           bool& truncated,
                           std::ostringstream& text,
                           std::vector<RefRecord>& refs) {
    if (!element || depth > options.maxDepth) {
        return;
    }
    if (options.maxNodes > 0 && visitedNodes >= options.maxNodes) {
        truncated = true;
        return;
    }
    ++visitedNodes;

    std::string role = CopyStringAttribute(element, kAXRoleAttribute);
    std::string name = BestName(element);
    bool include = !role.empty() &&
                   ((options.interactiveOnly && IsInteractiveRole(role)) ||
                    (!options.interactiveOnly && (IsInteractiveRole(role) || IsContentRole(role) || !name.empty())));

    std::string refId;
    Bounds bounds;
    if (include) {
        bounds = CopyBounds(element);
        if (bounds.available && (bounds.width < 1.0 || bounds.height < 1.0)) {
            include = false;
        }
    }
    if (include) {
        refId = "e" + std::to_string(nextElementRef++);
        RefRecord record;
        record.ref = refId;
        record.kind = "element";
        record.source = "accessibility";
        record.role = role;
        record.name = name;
        record.value = CopyStringAttribute(element, kAXValueAttribute);
        record.app = appName;
        record.pid = pid;
        record.bounds = bounds;
        refs.push_back(record);

        text << std::string(static_cast<size_t>(depth) * 2, ' ')
             << "@" << refId << " [" << RoleLabel(role) << "]";
        if (!name.empty()) {
            text << " \"" << name << "\"";
        }
        if (CopyBoolAttribute(element, kAXFocusedAttribute)) {
            text << " focused";
        }
        if (options.includeBounds && bounds.available) {
            text << " bounds=" << static_cast<int>(std::round(bounds.x)) << ","
                 << static_cast<int>(std::round(bounds.y)) << ","
                 << static_cast<int>(std::round(bounds.width)) << ","
                 << static_cast<int>(std::round(bounds.height));
        }
        if (options.includeActions) {
            auto actions = CopyActionNames(element);
            if (!actions.empty()) {
                text << " actions=" << ComputerCpp::Join(actions, ",");
            }
        }
        text << "\n";
    }

    CFTypeRef rawChildrenValue = nullptr;
    AXError childrenError = AXUIElementCopyAttributeValue(element, kAXChildrenAttribute, &rawChildrenValue);
    ScopedCFRef<CFTypeRef> childrenValue(rawChildrenValue);
    if (childrenError != kAXErrorSuccess ||
        !childrenValue ||
        CFGetTypeID(childrenValue.get()) != CFArrayGetTypeID()) {
        return;
    }

    CFArrayRef children = static_cast<CFArrayRef>(childrenValue.get());
    CFIndex count = CFArrayGetCount(children);
    for (CFIndex i = 0; i < count; ++i) {
        if (truncated) {
            break;
        }
        AXUIElementRef child = static_cast<AXUIElementRef>(CFArrayGetValueAtIndex(children, i));
        if (child && CFGetTypeID(child) == AXUIElementGetTypeID()) {
            AppendElementSnapshot(child,
                                  options,
                                  appName,
                                  pid,
                                  include ? depth + 1 : depth,
                                  nextElementRef,
                                  visitedNodes,
                                  truncated,
                                  text,
                                  refs);
        }
    }
}

AXUIElementRef CopyFocusedWindow(AXUIElementRef appElement) {
    if (!appElement) {
        return nullptr;
    }
    CFTypeRef rawFocusedWindow = nullptr;
    ScopedCFRef<CFTypeRef> focusedWindow;
    if (AXUIElementCopyAttributeValue(appElement, kAXFocusedWindowAttribute, &rawFocusedWindow) == kAXErrorSuccess &&
        rawFocusedWindow) {
        focusedWindow.reset(rawFocusedWindow);
    }
    if (focusedWindow &&
        CFGetTypeID(focusedWindow.get()) == AXUIElementGetTypeID()) {
        return static_cast<AXUIElementRef>(focusedWindow.release());
    }

    CFTypeRef rawWindowsValue = nullptr;
    AXError windowsError = AXUIElementCopyAttributeValue(appElement, kAXWindowsAttribute, &rawWindowsValue);
    ScopedCFRef<CFTypeRef> windowsValue(rawWindowsValue);
    if (windowsError != kAXErrorSuccess ||
        !windowsValue ||
        CFGetTypeID(windowsValue.get()) != CFArrayGetTypeID()) {
        return nullptr;
    }
    CFArrayRef windows = static_cast<CFArrayRef>(windowsValue.get());
    AXUIElementRef result = nullptr;
    if (CFArrayGetCount(windows) > 0) {
        AXUIElementRef first = static_cast<AXUIElementRef>(CFArrayGetValueAtIndex(windows, 0));
        if (first) {
            CFRetain(first);
            result = first;
        }
    }
    return result;
}

AXUIElementRef CopyFocusedElementWindowFallback(AXUIElementRef appElement) {
    if (!appElement) {
        return nullptr;
    }

    CFTypeRef rawFocusedValue = nullptr;
    AXError focusedError = AXUIElementCopyAttributeValue(appElement, kAXFocusedUIElementAttribute, &rawFocusedValue);
    ScopedCFRef<CFTypeRef> focusedValue(rawFocusedValue);
    if (focusedError != kAXErrorSuccess ||
        !focusedValue ||
        CFGetTypeID(focusedValue.get()) != AXUIElementGetTypeID()) {
        return nullptr;
    }

    AXUIElementRef focused = static_cast<AXUIElementRef>(focusedValue.get());
    if (CopyStringAttribute(focused, kAXRoleAttribute) == "AXWindow") {
        return static_cast<AXUIElementRef>(focusedValue.release());
    }

    CFTypeRef rawWindowValue = nullptr;
    ScopedCFRef<CFTypeRef> windowValue;
    if (AXUIElementCopyAttributeValue(focused, kAXWindowAttribute, &rawWindowValue) == kAXErrorSuccess &&
        rawWindowValue) {
        windowValue.reset(rawWindowValue);
    }
    if (windowValue &&
        CFGetTypeID(windowValue.get()) == AXUIElementGetTypeID()) {
        return static_cast<AXUIElementRef>(windowValue.release());
    }
    return nullptr;
}

}

PermissionStatus CheckPermissions(bool requestIfMissing) {
    PermissionStatus status;
    status.accessibility = AXIsProcessTrusted();
    status.screenCapture = CGPreflightScreenCaptureAccess();

    if (requestIfMissing) {
        AppendPermissionTrace("CheckPermissions(requestIfMissing=true) before accessibility=" +
                              PermissionBoolString(status.accessibility) +
                              " screen_capture=" + PermissionBoolString(status.screenCapture));
        void (^requestBlock)(void) = ^{
            if (!status.accessibility) {
                NSDictionary* options = @{(__bridge id)kAXTrustedCheckOptionPrompt: @YES};
                bool prompted = AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
                AppendPermissionTrace("CheckPermissions AXIsProcessTrustedWithOptions returned=" +
                                      PermissionBoolString(prompted));
            }
            if (!status.screenCapture) {
                bool requested = CGRequestScreenCaptureAccess();
                AppendPermissionTrace("CheckPermissions CGRequestScreenCaptureAccess returned=" +
                                      PermissionBoolString(requested));
            }
        };

        if ([NSThread isMainThread] || !NSApp || ![NSApp isRunning]) {
            requestBlock();
        } else {
            dispatch_sync(dispatch_get_main_queue(), requestBlock);
        }

        status.accessibility = AXIsProcessTrusted();
        status.screenCapture = CGPreflightScreenCaptureAccess();
        AppendPermissionTrace("CheckPermissions(requestIfMissing=true) after accessibility=" +
                              PermissionBoolString(status.accessibility) +
                              " screen_capture=" + PermissionBoolString(status.screenCapture));
    }

    return status;
}

bool OpenPermissionsSettings() {
    return OpenAccessibilitySettings();
}

bool OpenAccessibilitySettings() {
    NSURL* url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"];
    return [[NSWorkspace sharedWorkspace] openURL:url];
}

bool OpenScreenCaptureSettings() {
    NSURL* url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"];
    return [[NSWorkspace sharedWorkspace] openURL:url];
}

bool RequestAccessibilityPermission() {
    bool before = AXIsProcessTrusted();
    AppendPermissionTrace("RequestAccessibilityPermission begin trusted_before=" +
                          PermissionBoolString(before));
    if (AXIsProcessTrusted()) {
        AppendPermissionTrace("RequestAccessibilityPermission already_trusted");
        return true;
    }

    __block bool trusted = false;
    void (^requestBlock)(void) = ^{
        NSDictionary* options = @{(__bridge id)kAXTrustedCheckOptionPrompt: @YES};
        bool promptReturn = AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
        trusted = AXIsProcessTrusted();
        AppendPermissionTrace("RequestAccessibilityPermission prompt_return=" +
                              PermissionBoolString(promptReturn) +
                              " trusted_after_prompt=" + PermissionBoolString(trusted));
    };

    if ([NSThread isMainThread] || !NSApp || ![NSApp isRunning]) {
        requestBlock();
    } else {
        dispatch_sync(dispatch_get_main_queue(), requestBlock);
    }

    bool finalTrusted = trusted || AXIsProcessTrusted();
    AppendPermissionTrace("RequestAccessibilityPermission end trusted=" +
                          PermissionBoolString(finalTrusted));
    return finalTrusted;
}

bool RequestScreenCapturePermission() {
    bool before = CGPreflightScreenCaptureAccess();
    AppendPermissionTrace("RequestScreenCapturePermission begin preflight_before=" +
                          PermissionBoolString(before));
    if (before) {
        AppendPermissionTrace("RequestScreenCapturePermission already_granted");
        return true;
    }

    __block bool granted = false;
    void (^requestBlock)(void) = ^{
        granted = CGRequestScreenCaptureAccess();
        AppendPermissionTrace("RequestScreenCapturePermission CGRequestScreenCaptureAccess returned=" +
                              PermissionBoolString(granted) +
                              " preflight_after_request=" +
                              PermissionBoolString(CGPreflightScreenCaptureAccess()));
        if (!granted && !CGPreflightScreenCaptureAccess()) {
            if (@available(macOS 14.0, *)) {
                [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                                            onScreenWindowsOnly:YES
                                                              completionHandler:^(SCShareableContent* content, NSError* error) {
                    if (error) {
                        AppendPermissionTrace("RequestScreenCapturePermission ScreenCaptureKit fallback error=" +
                                              NSErrorToString(error));
                        std::cerr << "ScreenCaptureKit permission request failed: "
                                  << NSErrorToString(error) << std::endl;
                    } else {
                        AppendPermissionTrace("RequestScreenCapturePermission ScreenCaptureKit fallback completed displays=" +
                                              std::to_string(static_cast<long long>(content.displays.count)) +
                                              " windows=" +
                                              std::to_string(static_cast<long long>(content.windows.count)));
                    }
                    (void)content;
                }];
            }
        }
    };

    if ([NSThread isMainThread] || !NSApp || ![NSApp isRunning]) {
        requestBlock();
    } else {
        dispatch_sync(dispatch_get_main_queue(), requestBlock);
    }

    bool finalGranted = granted || CGPreflightScreenCaptureAccess();
    AppendPermissionTrace("RequestScreenCapturePermission end granted=" +
                          PermissionBoolString(finalGranted));
    return finalGranted;
}

AppInfo GetFrontmostApp() {
    AppInfo info;
    pid_t focusedPid = GetAccessibilityFocusedAppPid();
    NSRunningApplication* app = nil;
    if (focusedPid > 0) {
        app = [NSRunningApplication runningApplicationWithProcessIdentifier:focusedPid];
    }
    if (!app || [app isTerminated]) {
        app = [[NSWorkspace sharedWorkspace] frontmostApplication];
    }
    if (!app) {
        return info;
    }
    info.available = true;
    info.pid = [app processIdentifier];
    info.name = NSStringToString([app localizedName]);
    info.bundleId = NSStringToString([app bundleIdentifier]);
    return info;
}

std::string GetFrontmostAppSummary() {
    AppInfo app = GetFrontmostApp();
    if (!app.available) {
        return "unknown";
    }
    return app.name + " [" + app.bundleId + "] pid=" + std::to_string(app.pid);
}

FocusedElementInfo GetFocusedElementInfo() {
    FocusedElementInfo info;
    ScopedCFRef<AXUIElementRef> systemWide(AXUIElementCreateSystemWide());
    if (!systemWide) {
        return info;
    }
    CFTypeRef rawFocusedValue = nullptr;
    AXError error = AXUIElementCopyAttributeValue(systemWide.get(), kAXFocusedUIElementAttribute, &rawFocusedValue);
    ScopedCFRef<CFTypeRef> focusedValue(rawFocusedValue);
    if (error != kAXErrorSuccess || !focusedValue || CFGetTypeID(focusedValue.get()) != AXUIElementGetTypeID()) {
        return info;
    }

    AXUIElementRef focused = static_cast<AXUIElementRef>(focusedValue.get());
    info.available = true;
    info.role = CopyStringAttribute(focused, kAXRoleAttribute);
    info.subrole = CopyStringAttribute(focused, kAXSubroleAttribute);
    info.title = CopyStringAttribute(focused, kAXTitleAttribute);
    info.description = CopyStringAttribute(focused, kAXDescriptionAttribute);
    info.value = CopyStringAttribute(focused, kAXValueAttribute);
    info.bounds = CopyBounds(focused);

    Boolean valueSettable = false;
    if (AXUIElementIsAttributeSettable(focused, kAXValueAttribute, &valueSettable) == kAXErrorSuccess) {
        info.valueSettable = valueSettable != 0;
    }
    bool editable = CopyBoolAttribute(focused, CFSTR("AXEditable"));
    info.acceptsTextInput = editable || info.valueSettable || IsTextInputRole(info.role);

    return info;
}

Bounds GetFrontmostWindowBounds() {
    Bounds bounds;
    pid_t focusedPid = GetAccessibilityFocusedAppPid();
    if (focusedPid <= 0) {
        NSRunningApplication* app = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (app && ![app isTerminated]) {
            focusedPid = [app processIdentifier];
        }
    }
    if (focusedPid <= 0) {
        return bounds;
    }

    ScopedCFRef<CFArrayRef> windowInfo(CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID));
    if (!windowInfo) {
        return bounds;
    }

    CFIndex count = CFArrayGetCount(windowInfo.get());
    for (CFIndex i = 0; i < count; ++i) {
        CFDictionaryRef entry = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windowInfo.get(), i));
        if (!entry) continue;
        int ownerPid = -1;
        CFNumberRef ownerPidRef = static_cast<CFNumberRef>(CFDictionaryGetValue(entry, kCGWindowOwnerPID));
        if (!ownerPidRef || !CFNumberGetValue(ownerPidRef, kCFNumberIntType, &ownerPid) || ownerPid != focusedPid) {
            continue;
        }
        int layer = 0;
        CFNumberRef layerRef = static_cast<CFNumberRef>(CFDictionaryGetValue(entry, kCGWindowLayer));
        if (!layerRef || !CFNumberGetValue(layerRef, kCFNumberIntType, &layer) || layer != 0) {
            continue;
        }
        CGRect rect = CGRectZero;
        CFDictionaryRef boundsRef = static_cast<CFDictionaryRef>(CFDictionaryGetValue(entry, kCGWindowBounds));
        if (!boundsRef || !CGRectMakeWithDictionaryRepresentation(boundsRef, &rect)) {
            continue;
        }
        if (rect.size.width < 50.0 || rect.size.height < 50.0) {
            continue;
        }
        bounds.available = true;
        bounds.x = rect.origin.x;
        bounds.y = rect.origin.y;
        bounds.width = rect.size.width;
        bounds.height = rect.size.height;
        break;
    }

    return bounds;
}

WindowInfo GetActiveWindow() {
    auto app = GetFrontmostApp();
    WindowInfo window;
    window.available = app.available;
    window.id = app.available ? std::to_string(app.pid) : "";
    window.title = app.name;
    window.appClass = app.bundleId;
    window.pid = app.pid;
    window.active = app.available;
    window.bounds = GetFrontmostWindowBounds();
    return window;
}

std::vector<WindowInfo> ListWindows(const std::string& appQuery) {
    auto window = GetActiveWindow();
    if (!window.available) {
        return {};
    }
    if (!appQuery.empty()) {
        std::string haystack = window.appClass + " " + window.title;
        std::string query = appQuery;
        std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        std::transform(query.begin(), query.end(), query.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (haystack.find(query) == std::string::npos) {
            return {};
        }
    }
    return {window};
}

bool CloseWindow(const std::string& id) {
    int pid = -1;
    try {
        size_t consumed = 0;
        pid = std::stoi(id, &consumed);
        if (consumed != id.size()) {
            return false;
        }
    } catch (...) {
        return false;
    }
    if (pid <= 0 || pid == [[NSProcessInfo processInfo] processIdentifier]) {
        return false;
    }

    NSRunningApplication* app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
    if (!app || [app isTerminated]) {
        return false;
    }
    [app activateWithOptions:NSApplicationActivateAllWindows];
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    ScopedCFRef<AXUIElementRef> appElement(AXUIElementCreateApplication(pid));
    if (!appElement) {
        return false;
    }
    ScopedCFRef<AXUIElementRef> window(CopyFocusedWindow(appElement.get()));
    if (!window) {
        window.reset(CopyFocusedElementWindowFallback(appElement.get()));
    }
    if (!window) {
        return SendHotkey({"Cmd", "W"}, 25);
    }

    AXUIElementPerformAction(window.get(), kAXRaiseAction);

    CFTypeRef rawCloseButton = nullptr;
    ScopedCFRef<CFTypeRef> closeButton;
    if (AXUIElementCopyAttributeValue(window.get(), kAXCloseButtonAttribute, &rawCloseButton) == kAXErrorSuccess &&
        rawCloseButton) {
        closeButton.reset(rawCloseButton);
    }
    if (closeButton && CFGetTypeID(closeButton.get()) == AXUIElementGetTypeID()) {
        AXError pressError = AXUIElementPerformAction(static_cast<AXUIElementRef>(closeButton.get()), kAXPressAction);
        if (pressError == kAXErrorSuccess) {
            return true;
        }
    }

    return SendHotkey({"Cmd", "W"}, 25);
}

bool SetWindowBoundsForPid(int pid, const Bounds& bounds) {
    if (!bounds.available || bounds.width <= 0.0 || bounds.height <= 0.0) {
        return false;
    }
    if (pid <= 0) {
        return false;
    }

    NSRunningApplication* app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
    if (app && ![app isTerminated]) {
        [app activateWithOptions:NSApplicationActivateAllWindows];
    }

    ScopedCFRef<AXUIElementRef> appElement(AXUIElementCreateApplication(pid));
    if (!appElement) {
        return false;
    }
    ScopedCFRef<AXUIElementRef> window(CopyFocusedWindow(appElement.get()));
    if (!window) {
        window.reset(CopyFocusedElementWindowFallback(appElement.get()));
    }
    if (!window) {
        return false;
    }

    CGPoint point = CGPointMake(bounds.x, bounds.y);
    CGSize size = CGSizeMake(bounds.width, bounds.height);
    ScopedCFRef<AXValueRef> positionValue(AXValueCreate(static_cast<AXValueType>(kAXValueCGPointType), &point));
    ScopedCFRef<AXValueRef> sizeValue(AXValueCreate(static_cast<AXValueType>(kAXValueCGSizeType), &size));
    bool ok = false;
    if (positionValue && sizeValue) {
        AXError posErr = AXUIElementSetAttributeValue(window.get(), kAXPositionAttribute, positionValue.get());
        AXError sizeErr = AXUIElementSetAttributeValue(window.get(), kAXSizeAttribute, sizeValue.get());
        ok = (posErr == kAXErrorSuccess && sizeErr == kAXErrorSuccess);
    }
    return ok;
}

bool SetFrontmostWindowBounds(const Bounds& bounds) {
    pid_t focusedPid = GetAccessibilityFocusedAppPid();
    if (focusedPid <= 0) {
        return false;
    }
    return SetWindowBoundsForPid(focusedPid, bounds);
}

int GetFrontmostAppPid() {
    AppInfo app = GetFrontmostApp();
    return app.available ? app.pid : -1;
}

bool ActivateAppByPid(int pid) {
    if (pid <= 0 || pid == [[NSProcessInfo processInfo] processIdentifier]) {
        return false;
    }
    NSRunningApplication* app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
    if (!app || [app isTerminated]) {
        return false;
    }

    [app unhide];

    auto isExactAppFrontmost = [&]() {
        if ([app isActive]) {
            return true;
        }
        NSRunningApplication* workspaceFrontmost = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (workspaceFrontmost && [workspaceFrontmost processIdentifier] == pid) {
            return true;
        }
        return GetFrontmostAppPid() == pid;
    };

    ScopedCFRef<AXUIElementRef> appElement(AXUIElementCreateApplication(pid));
    if (appElement) {
        AXUIElementSetAttributeValue(appElement.get(), kAXFrontmostAttribute, kCFBooleanTrue);

        ScopedCFRef<AXUIElementRef> window(CopyFocusedWindow(appElement.get()));
        if (!window) {
            window.reset(CopyFocusedElementWindowFallback(appElement.get()));
        }
        if (window) {
            AXUIElementSetAttributeValue(window.get(), kAXMinimizedAttribute, kCFBooleanFalse);
            AXUIElementSetAttributeValue(window.get(), kAXMainAttribute, kCFBooleanTrue);
            AXUIElementSetAttributeValue(window.get(), kAXFocusedAttribute, kCFBooleanTrue);
            AXUIElementPerformAction(window.get(), kAXRaiseAction);
        }
    }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    const bool activationRequested = [app activateWithOptions:
        NSApplicationActivateAllWindows | NSApplicationActivateIgnoringOtherApps];
#pragma clang diagnostic pop
    if (!activationRequested && !isExactAppFrontmost()) {
        return false;
    }

    constexpr auto kFocusTimeout = std::chrono::milliseconds(2500);
    constexpr auto kFocusPollInterval = std::chrono::milliseconds(50);
    const auto deadline = std::chrono::steady_clock::now() + kFocusTimeout;
    do {
        if (isExactAppFrontmost()) {
            return true;
        }
        std::this_thread::sleep_for(kFocusPollInterval);
    } while (std::chrono::steady_clock::now() < deadline);
    return isExactAppFrontmost();
}

bool LaunchOrActivateApp(const std::string& query, AppInfo& appInfo) {
    if (query.empty()) {
        return false;
    }

    NSString* nsQuery = [NSString stringWithUTF8String:query.c_str()];
    if (!nsQuery) {
        return false;
    }
    NSString* lowerQuery = [nsQuery lowercaseString];

    for (NSRunningApplication* app in [[NSWorkspace sharedWorkspace] runningApplications]) {
        NSString* bundleId = [[app bundleIdentifier] lowercaseString];
        NSString* name = [[app localizedName] lowercaseString];
        if ((bundleId && [bundleId isEqualToString:lowerQuery]) ||
            (name && [name isEqualToString:lowerQuery])) {
            bool activated = [app activateWithOptions:NSApplicationActivateAllWindows];
            appInfo.available = true;
            appInfo.pid = [app processIdentifier];
            appInfo.name = NSStringToString([app localizedName]);
            appInfo.bundleId = NSStringToString([app bundleIdentifier]);
            return activated;
        }
    }

    NSURL* appURL = ResolveApplicationUrl(query);
    if (!appURL) {
        return false;
    }

    if (![[NSWorkspace sharedWorkspace] openURL:appURL]) {
        std::cerr << "Launch app failed: unable to open " << NSStringToString(appURL.path) << std::endl;
        return false;
    }

    NSString* expectedBundleId = [[NSBundle bundleWithURL:appURL] bundleIdentifier];
    NSRunningApplication* launchedApp = nil;
    for (int attempt = 0; attempt < 40 && !launchedApp; ++attempt) {
        for (NSRunningApplication* candidate in [[NSWorkspace sharedWorkspace] runningApplications]) {
            NSString* candidateBundleId = [candidate bundleIdentifier];
            if ((expectedBundleId && candidateBundleId && [candidateBundleId isEqualToString:expectedBundleId]) ||
                (!expectedBundleId && [candidate.localizedName caseInsensitiveCompare:appURL.lastPathComponent] == NSOrderedSame)) {
                launchedApp = candidate;
                break;
            }
        }
        if (!launchedApp) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    NSRunningApplication* app = launchedApp;
    if (app) {
        [app activateWithOptions:NSApplicationActivateAllWindows];
    }
    appInfo.available = true;
    appInfo.pid = app ? [app processIdentifier] : -1;
    appInfo.name = app ? NSStringToString([app localizedName]) : NSStringToString(appURL.lastPathComponent);
    appInfo.bundleId = app ? NSStringToString([app bundleIdentifier]) : NSStringToString(expectedBundleId);
    return true;
}

bool OpenUrl(const std::string& url, const std::string& browser, bool newWindow, bool newInstance) {
    if (url.empty()) {
        return false;
    }

    NSString* nsUrl = [NSString stringWithUTF8String:url.c_str()];
    NSURL* targetUrl = nsUrl ? [NSURL URLWithString:nsUrl] : nil;
    if (!targetUrl) {
        return false;
    }

    NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
    NSURL* appUrl = ResolveApplicationUrl(browser);
    if (browser.empty() && !newWindow && !newInstance) {
        return [workspace openURL:targetUrl];
    }
    if (!browser.empty() && !appUrl) {
        return false;
    }

    NSWorkspaceOpenConfiguration* configuration = [NSWorkspaceOpenConfiguration configuration];
    configuration.activates = YES;
    configuration.createsNewApplicationInstance = newWindow || newInstance;

    __block BOOL opened = NO;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    void (^completion)(NSRunningApplication*, NSError*) = ^(NSRunningApplication* app, NSError* error) {
        if (error) {
            std::cerr << "Open URL failed: " << NSErrorToString(error) << std::endl;
        }
        opened = app != nil && error == nil;
        dispatch_semaphore_signal(done);
    };

    if (appUrl) {
        [workspace openURLs:@[targetUrl] withApplicationAtURL:appUrl configuration:configuration completionHandler:completion];
    } else {
        [workspace openURL:targetUrl configuration:configuration completionHandler:completion];
    }

    dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(5 * NSEC_PER_SEC));
    return dispatch_semaphore_wait(done, timeout) == 0 && opened;
}

void DeactivateAgentApp() {
    if (NSApp) {
        [NSApp hide:nil];
    }
}

void ActivateAgentApp() {
    void (^activateBlock)(void) = ^{
        if (NSApp) {
            [NSApp unhide:nil];
        }
        if (@available(macOS 14.0, *)) {
            [NSApp activate];
        } else {
            NSRunningApplication* app = [NSRunningApplication currentApplication];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            [app activateWithOptions:NSApplicationActivateAllWindows | NSApplicationActivateIgnoringOtherApps];
#pragma clang diagnostic pop
        }
    };

    if ([NSThread isMainThread] || !NSApp || ![NSApp isRunning]) {
        activateBlock();
    } else {
        dispatch_async(dispatch_get_main_queue(), activateBlock);
    }
}

void GetScreenSize(int& width, int& height) {
    CGDirectDisplayID displayID = CGMainDisplayID();
    width = static_cast<int>(CGDisplayPixelsWide(displayID));
    height = static_cast<int>(CGDisplayPixelsHigh(displayID));
}

void GetCursorPosition(double& x, double& y) {
    ScopedCFRef<CGEventRef> event(CGEventCreate(nullptr));
    if (!event) {
        x = 0.0;
        y = 0.0;
        return;
    }
    CGPoint loc = CGEventGetLocation(event.get());
    x = loc.x;
    y = loc.y;
}

void MoveMouse(double x, double y) {
    CGPoint point = CGPointMake(x, y);
    CGWarpMouseCursorPosition(point);
    CGEventType type = kCGEventMouseMoved;
    CGMouseButton button = kCGMouseButtonLeft;
    if (gLeftDown) {
        type = kCGEventLeftMouseDragged;
    } else if (gRightDown) {
        type = kCGEventRightMouseDragged;
        button = kCGMouseButtonRight;
    } else if (gMiddleDown) {
        type = kCGEventOtherMouseDragged;
        button = kCGMouseButtonCenter;
    }
    ScopedCFRef<CGEventRef> event(CGEventCreateMouseEvent(nullptr, type, point, button));
    if (!event) {
        return;
    }
    CGEventPost(kCGHIDEventTap, event.get());
}

void MoveMouseSmooth(double x, double y, int durationMs, int steps) {
    double startX = 0.0;
    double startY = 0.0;
    GetCursorPosition(startX, startY);
    double distance = std::hypot(x - startX, y - startY);
    auto plan = HumanInput::PlanPointerMove(distance, durationMs, steps);

    int sleepMs = std::max(1, plan.durationMs / plan.steps);
    auto path = HumanInput::CurvedPath({startX, startY}, {x, y}, plan.steps);
    for (const auto& point : path) {
        MoveMouse(point.x, point.y);
        std::this_thread::sleep_for(std::chrono::milliseconds(HumanInput::ScaledDelayMs(sleepMs)));
    }
    MoveMouse(x, y);
}

void DragMouseSmooth(double fromX, double fromY, double toX, double toY, const std::string& button, int durationMs, int steps) {
    MoveMouseSmooth(fromX, fromY, 220, 12);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    MouseDown(button, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    MoveMouseSmooth(toX, toY, durationMs, steps);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    MouseUp(button, 1);
}

void MouseDown(const std::string& button, int clickCount) {
    double x = 0.0;
    double y = 0.0;
    GetCursorPosition(x, y);
    if (button == "right" || button == "secondary") {
        gRightDown = true;
        PostMouseEvent(kCGEventRightMouseDown, kCGMouseButtonRight, x, y, clickCount);
    } else if (button == "middle" || button == "center") {
        gMiddleDown = true;
        PostMouseEvent(kCGEventOtherMouseDown, kCGMouseButtonCenter, x, y, clickCount);
    } else {
        gLeftDown = true;
        PostMouseEvent(kCGEventLeftMouseDown, kCGMouseButtonLeft, x, y, clickCount);
    }
}

void MouseUp(const std::string& button, int clickCount) {
    double x = 0.0;
    double y = 0.0;
    GetCursorPosition(x, y);
    if (button == "right" || button == "secondary") {
        gRightDown = false;
        PostMouseEvent(kCGEventRightMouseUp, kCGMouseButtonRight, x, y, clickCount);
    } else if (button == "middle" || button == "center") {
        gMiddleDown = false;
        PostMouseEvent(kCGEventOtherMouseUp, kCGMouseButtonCenter, x, y, clickCount);
    } else {
        gLeftDown = false;
        PostMouseEvent(kCGEventLeftMouseUp, kCGMouseButtonLeft, x, y, clickCount);
    }
}

void Scroll(int deltaY, int deltaX) {
    double x = 0.0;
    double y = 0.0;
    GetCursorPosition(x, y);
    ScopedCFRef<CGEventRef> event(CGEventCreateScrollWheelEvent(
        nullptr,
        kCGScrollEventUnitPixel,
        2,
        deltaY,
        deltaX));
    if (!event) {
        return;
    }
    CGEventSetIntegerValueField(event.get(), kCGScrollWheelEventIsContinuous, 1);
    CGEventSetLocation(event.get(), CGPointMake(x, y));
    CGEventPost(kCGSessionEventTap, event.get());
}

void ScrollGesture(int deltaY, int deltaX, int durationMs, int steps, double jitter) {
    if ((durationMs <= 0 && steps <= 1) || (deltaY == 0 && deltaX == 0)) {
        Scroll(deltaY, deltaX);
        return;
    }

    steps = steps > 0 ? steps : std::clamp(std::abs(deltaY) / 4, 6, 22);
    steps = std::clamp(steps, 2, 80);
    durationMs = durationMs > 0 ? durationMs : std::clamp(steps * 24, 120, 900);
    durationMs = std::clamp(durationMs, 30, 5000);
    jitter = std::clamp(jitter, 0.0, 0.6);

    int totalY = deltaY;
    int totalX = deltaX;

    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> jitterScale(1.0 - jitter, 1.0 + jitter);
    std::uniform_real_distribution<double> timingScale(0.7, 1.35);
    std::uniform_real_distribution<double> pointerJitter(-1.6, 1.6);

    std::vector<double> weights;
    weights.reserve(static_cast<size_t>(steps));
    double weightSum = 0.0;
    for (int i = 0; i < steps; ++i) {
        double t = (static_cast<double>(i) + 0.5) / static_cast<double>(steps);
        double weight = (0.35 + std::sin(M_PI * t)) * jitterScale(rng);
        weights.push_back(weight);
        weightSum += weight;
    }

    double cursorX = 0.0;
    double cursorY = 0.0;
    GetCursorPosition(cursorX, cursorY);

    double cumulative = 0.0;
    int sentY = 0;
    int sentX = 0;
    int baseSleepMs = std::max(1, durationMs / steps);
    for (int i = 0; i < steps; ++i) {
        cumulative += weights[static_cast<size_t>(i)];
        int targetY = static_cast<int>(std::round(totalY * cumulative / weightSum));
        int targetX = static_cast<int>(std::round(totalX * cumulative / weightSum));
        int chunkY = targetY - sentY;
        int chunkX = targetX - sentX;
        sentY = targetY;
        sentX = targetX;

        if (chunkY != 0 || chunkX != 0) {
            ScopedCFRef<CGEventRef> event(CGEventCreateScrollWheelEvent(
                nullptr,
                kCGScrollEventUnitPixel,
                2,
                chunkY,
                chunkX));
            if (event) {
                CGEventSetIntegerValueField(event.get(), kCGScrollWheelEventIsContinuous, 1);
                CGEventSetLocation(event.get(), CGPointMake(cursorX + pointerJitter(rng), cursorY + pointerJitter(rng)));
                CGEventPost(kCGSessionEventTap, event.get());
            }
        }

        int sleepMs = std::max(1, static_cast<int>(std::round(baseSleepMs * timingScale(rng))));
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
}

bool ClickSmooth(double x, double y, const std::string& button, int clickCount, int durationMs, int steps) {
    double startX = 0.0;
    double startY = 0.0;
    GetCursorPosition(startX, startY);
    auto plan = HumanInput::PlanPointerMove(std::hypot(x - startX, y - startY), durationMs, steps);
    MoveMouseSmooth(x, y, plan.durationMs, plan.steps);
    std::this_thread::sleep_for(std::chrono::milliseconds(HumanInput::PreClickSettleMs()));
    MouseDown(button, clickCount);
    std::this_thread::sleep_for(std::chrono::milliseconds(HumanInput::ClickHoldMs()));
    MouseUp(button, clickCount);
    return true;
}

bool Click(double x, double y, const std::string& button, int clickCount) {
    MoveMouse(x, y);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    MouseDown(button, clickCount);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    MouseUp(button, clickCount);
    return true;
}

int ResolveKeycode(const std::string& keyName) {
    std::string key = NormalizeKeyName(keyName);
    auto it = kKeycodes.find(key);
    return it == kKeycodes.end() ? -1 : it->second;
}

bool SendHotkey(const std::vector<std::string>& keys, int holdMs) {
    std::vector<HotkeyKey> modifiers;
    std::vector<HotkeyKey> primaries;
    for (const auto& key : keys) {
        int code = ResolveKeycode(key);
        if (code < 0) {
            return false;
        }
        CGEventFlags flag = ModifierFlagForKeycode(code);
        HotkeyKey spec{static_cast<CGKeyCode>(code), flag, flag != 0};
        if (spec.isModifier) {
            modifiers.push_back(spec);
        } else {
            primaries.push_back(spec);
        }
    }

    if (modifiers.empty() && primaries.empty()) {
        return false;
    }

    CGEventFlags activeFlags = 0;
    PressModifiers(modifiers, activeFlags);
    if (primaries.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(holdMs, 1)));
    } else {
        for (const auto& primary : primaries) {
            PostKeyboardEvent(primary.code, true, activeFlags);
            std::this_thread::sleep_for(std::chrono::milliseconds(std::max(holdMs, 1)));
            PostKeyboardEvent(primary.code, false, activeFlags);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    ReleaseModifiers(modifiers, activeFlags);
    return true;
}

bool TypeCharacter(const std::string& character, int holdMs) {
    auto stroke = ResolveCharacterKeyStroke(character);
    if (!stroke.has_value()) {
        return false;
    }

    std::vector<HotkeyKey> modifiers = BuildModifierSequence(stroke->flags);
    CGEventFlags activeFlags = 0;
    PressModifiers(modifiers, activeFlags);
    PostKeyboardEvent(stroke->keycode, true, activeFlags);
    std::this_thread::sleep_for(std::chrono::milliseconds(std::max(holdMs, 1)));
    PostKeyboardEvent(stroke->keycode, false, activeFlags);
    ReleaseModifiers(modifiers, activeFlags);
    return true;
}

bool TypeText(const std::string& text, int holdMs) {
    NSString* nsText = [NSString stringWithUTF8String:text.c_str()];
    if (!nsText) {
        return false;
    }
    for (NSUInteger i = 0; i < [nsText length]; ++i) {
        NSString* ch = [nsText substringWithRange:NSMakeRange(i, 1)];
        if (!TypeCharacter(NSStringToString(ch), holdMs)) {
            return PasteText(text);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    return true;
}

bool ReadClipboardText(std::string& text) {
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    NSString* value = [pasteboard stringForType:NSPasteboardTypeString];
    if (!value) {
        text.clear();
        return false;
    }
    text = NSStringToString(value);
    return true;
}

bool WriteClipboardText(const std::string& text) {
    NSString* nsText = [NSString stringWithUTF8String:text.c_str()];
    if (!nsText) {
        return false;
    }
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];
    return [pasteboard setString:nsText forType:NSPasteboardTypeString];
}

bool PasteText(const std::string& text) {
    if (!WriteClipboardText(text)) {
        return false;
    }
    return SendHotkey({"command", "v"}, 55);
}

std::vector<std::string> GetSelectAllHotkey() {
    return {"command", "a"};
}

bool WriteImageToPng(CGImageRef image, const std::string& filePath) {
    if (!image) {
        return false;
    }
    NSString* nsPath = [NSString stringWithUTF8String:filePath.c_str()];
    NSURL* url = nsPath ? [NSURL fileURLWithPath:nsPath] : nil;
    ScopedCFRef<CGImageDestinationRef> destination(url
        ? CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, nullptr)
        : nullptr);
    if (!destination) {
        return false;
    }
    CGImageDestinationAddImage(destination.get(), image, nullptr);
    return CGImageDestinationFinalize(destination.get());
}

int64_t NowMsForTempPath() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::string ShellQuote(const std::string& value) {
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted += "'";
    return quoted;
}

CGImageRef LoadImageFromPath(const std::string& filePath) {
    NSString* nsPath = [NSString stringWithUTF8String:filePath.c_str()];
    NSURL* url = nsPath ? [NSURL fileURLWithPath:nsPath] : nil;
    ScopedCFRef<CGImageSourceRef> source(url ? CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr) : nullptr);
    if (!source) {
        return nullptr;
    }
    return CGImageSourceCreateImageAtIndex(source.get(), 0, nullptr);
}

CGImageRef CaptureMainDisplayImageWithScreencapture() {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("computer.cpp-screencapture-" + std::to_string(NowMsForTempPath()) + ".png");
    std::string command = "/usr/sbin/screencapture -x " + ShellQuote(path.string());
    int status = std::system(command.c_str());
    if (status != 0) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return nullptr;
    }

    CGImageRef image = LoadImageFromPath(path.string());
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return image;
}

bool CaptureScreenshotFileWithScreencapture(const std::string& filePath) {
    std::filesystem::path path(filePath);
    if (!path.parent_path().empty()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::string command = "/usr/sbin/screencapture -x " + ShellQuote(filePath);
    return std::system(command.c_str()) == 0;
}

CGImageRef ScaleImageToMaxDimension(CGImageRef image, int maxDimension) {
    if (!image || maxDimension <= 0) {
        return image ? CGImageRetain(image) : nullptr;
    }
    const size_t sourceWidth = CGImageGetWidth(image);
    const size_t sourceHeight = CGImageGetHeight(image);
    if (sourceWidth == 0 || sourceHeight == 0) {
        return nullptr;
    }
    const size_t largest = std::max(sourceWidth, sourceHeight);
    if (largest <= static_cast<size_t>(maxDimension)) {
        return CGImageRetain(image);
    }

    const double scale = static_cast<double>(maxDimension) / static_cast<double>(largest);
    const size_t scaledWidth = std::max<size_t>(1, static_cast<size_t>(std::llround(static_cast<double>(sourceWidth) * scale)));
    const size_t scaledHeight = std::max<size_t>(1, static_cast<size_t>(std::llround(static_cast<double>(sourceHeight) * scale)));
    ScopedCFRef<CGColorSpaceRef> colorSpace(CGColorSpaceCreateDeviceRGB());
    if (!colorSpace) {
        return nullptr;
    }

    CGBitmapInfo bitmapInfo = static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big;
    ScopedCGContextRef context(CGBitmapContextCreate(
        nullptr,
        scaledWidth,
        scaledHeight,
        8,
        scaledWidth * 4,
        colorSpace.get(),
        bitmapInfo));
    if (!context) {
        return nullptr;
    }
    CGContextSetInterpolationQuality(context.get(), kCGInterpolationHigh);
    CGContextDrawImage(context.get(), CGRectMake(0, 0, scaledWidth, scaledHeight), image);
    return CGBitmapContextCreateImage(context.get());
}

CGImageRef CaptureMainDisplayImageWithCoreGraphics() {
    CGImageRef image = nullptr;
    using CGDisplayCreateImageFunction = CGImageRef (*)(CGDirectDisplayID);
    auto* createImage = reinterpret_cast<CGDisplayCreateImageFunction>(dlsym(RTLD_DEFAULT, "CGDisplayCreateImage"));
    if (createImage) {
        image = createImage(CGMainDisplayID());
    }
    return image;
}

CGImageRef CaptureMainDisplayImage() {
    @autoreleasepool {
        if (@available(macOS 14.0, *)) {
            __block CGImageRef captured = nullptr;
            dispatch_semaphore_t done = dispatch_semaphore_create(0);

            [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                                        onScreenWindowsOnly:YES
                                                  completionHandler:^(SCShareableContent* content, NSError* error) {
                if (error || !content || content.displays.count == 0) {
                    if (error) {
                        std::cerr << "ScreenCaptureKit shareable content failed: "
                                  << NSErrorToString(error) << std::endl;
                    } else {
                        std::cerr << "ScreenCaptureKit shareable content failed: no displays" << std::endl;
                    }
                    dispatch_semaphore_signal(done);
                    return;
                }

                CGDirectDisplayID mainDisplayId = CGMainDisplayID();
                SCDisplay* display = nil;
                for (SCDisplay* candidate in content.displays) {
                    if (candidate.displayID == mainDisplayId) {
                        display = candidate;
                        break;
                    }
                }
                if (!display) {
                    display = content.displays.firstObject;
                }

                CGFloat scale = 1.0;
                for (NSScreen* screen in [NSScreen screens]) {
                    NSNumber* screenNumber = screen.deviceDescription[@"NSScreenNumber"];
                    if (screenNumber && screenNumber.unsignedIntValue == display.displayID) {
                        scale = screen.backingScaleFactor;
                        break;
                    }
                }

                SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
                SCStreamConfiguration* configuration = [[SCStreamConfiguration alloc] init];
                configuration.width = static_cast<size_t>(std::max<NSInteger>(1, display.width) * scale);
                configuration.height = static_cast<size_t>(std::max<NSInteger>(1, display.height) * scale);
                configuration.showsCursor = YES;

                [SCScreenshotManager captureImageWithFilter:filter
                                              configuration:configuration
                                          completionHandler:^(CGImageRef image, NSError* captureError) {
                    if (captureError) {
                        std::cerr << "ScreenCaptureKit screenshot failed: "
                                  << NSErrorToString(captureError) << std::endl;
                    }
                    if (!captureError && image) {
                        captured = CGImageRetain(image);
                    }
                    dispatch_semaphore_signal(done);
                }];
            }];

            dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC);
            if (dispatch_semaphore_wait(done, timeout) != 0) {
                return CaptureMainDisplayImageWithCoreGraphics();
            }
            if (!captured) {
                captured = CaptureMainDisplayImageWithCoreGraphics();
            }
            if (!captured) {
                captured = CaptureMainDisplayImageWithScreencapture();
            }
            return captured;
        }

        CGImageRef image = CaptureMainDisplayImageWithCoreGraphics();
        if (!image) {
            image = CaptureMainDisplayImageWithScreencapture();
        }
        if (!image) {
            return nullptr;
        }
        return image;
    }
}

bool SaveScreenshot(const std::string& filePath) {
    ScopedCGImageRef image(CaptureMainDisplayImage());
    if (!image) {
        return CaptureScreenshotFileWithScreencapture(filePath);
    }
    return WriteImageToPng(image.get(), filePath);
}

bool SaveScreenshotScaled(const std::string& filePath, int maxDimension) {
    ScopedCGImageRef image(CaptureMainDisplayImage());
    if (!image) {
        if (!CaptureScreenshotFileWithScreencapture(filePath)) {
            return false;
        }
        if (maxDimension <= 0) {
            return true;
        }
        image.reset(LoadImageFromPath(filePath));
        if (!image) {
            return false;
        }
    }
    if (maxDimension <= 0) {
        return WriteImageToPng(image.get(), filePath);
    }
    ScopedCGImageRef scaled(ScaleImageToMaxDimension(image.get(), maxDimension));
    if (!scaled) {
        return false;
    }
    return WriteImageToPng(scaled.get(), filePath);
}

bool SaveScreenshotRegion(const std::string& filePath, const Bounds& bounds) {
    if (!bounds.available || bounds.width <= 1.0 || bounds.height <= 1.0) {
        return SaveScreenshot(filePath);
    }

    ScopedCGImageRef image(CaptureMainDisplayImage());
    if (!image) {
        return false;
    }

    int screenWidth = 0;
    int screenHeight = 0;
    GetScreenSize(screenWidth, screenHeight);
    const double scaleX = screenWidth > 0 ? static_cast<double>(CGImageGetWidth(image.get())) / screenWidth : 1.0;
    const double scaleY = screenHeight > 0 ? static_cast<double>(CGImageGetHeight(image.get())) / screenHeight : scaleX;
    CGRect crop = CGRectMake(std::max(0.0, bounds.x) * scaleX,
                             std::max(0.0, bounds.y) * scaleY,
                             std::max(1.0, bounds.width) * scaleX,
                             std::max(1.0, bounds.height) * scaleY);
    crop = CGRectIntersection(crop, CGRectMake(0, 0, CGImageGetWidth(image.get()), CGImageGetHeight(image.get())));
    if (CGRectIsNull(crop) || CGRectIsEmpty(crop)) {
        return false;
    }

    ScopedCGImageRef cropped(CGImageCreateWithImageInRect(image.get(), crop));
    if (!cropped) {
        return false;
    }
    return WriteImageToPng(cropped.get(), filePath);
}

bool SaveScreenshotRegionScaled(const std::string& filePath, const Bounds& bounds, int maxDimension) {
    if (!bounds.available || bounds.width <= 1.0 || bounds.height <= 1.0) {
        return SaveScreenshotScaled(filePath, maxDimension);
    }

    ScopedCGImageRef image(CaptureMainDisplayImage());
    if (!image) {
        return false;
    }

    int screenWidth = 0;
    int screenHeight = 0;
    GetScreenSize(screenWidth, screenHeight);
    const double scaleX = screenWidth > 0 ? static_cast<double>(CGImageGetWidth(image.get())) / screenWidth : 1.0;
    const double scaleY = screenHeight > 0 ? static_cast<double>(CGImageGetHeight(image.get())) / screenHeight : scaleX;
    CGRect crop = CGRectMake(std::max(0.0, bounds.x) * scaleX,
                             std::max(0.0, bounds.y) * scaleY,
                             std::max(1.0, bounds.width) * scaleX,
                             std::max(1.0, bounds.height) * scaleY);
    crop = CGRectIntersection(crop, CGRectMake(0, 0, CGImageGetWidth(image.get()), CGImageGetHeight(image.get())));
    if (CGRectIsNull(crop) || CGRectIsEmpty(crop)) {
        return false;
    }

    ScopedCGImageRef cropped(CGImageCreateWithImageInRect(image.get(), crop));
    if (!cropped) {
        return false;
    }
    if (maxDimension <= 0) {
        return WriteImageToPng(cropped.get(), filePath);
    }
    ScopedCGImageRef scaled(ScaleImageToMaxDimension(cropped.get(), maxDimension));
    if (!scaled) {
        return false;
    }
    return WriteImageToPng(scaled.get(), filePath);
}

std::vector<uint8_t> CaptureScreenRaw(int& outWidth, int& outHeight) {
    outWidth = 0;
    outHeight = 0;
    ScopedCGImageRef image(CaptureMainDisplayImage());
    if (!image) {
        return {};
    }

    GetScreenSize(outWidth, outHeight);
    if (outWidth <= 0 || outHeight <= 0) {
        outWidth = static_cast<int>(CGImageGetWidth(image.get()));
        outHeight = static_cast<int>(CGImageGetHeight(image.get()));
    }
    if (outWidth <= 0 || outHeight <= 0) {
        outWidth = 0;
        outHeight = 0;
        return {};
    }

    std::vector<uint8_t> rgba(static_cast<size_t>(outWidth * outHeight * 4));
    ScopedCFRef<CGColorSpaceRef> colorSpace(CGColorSpaceCreateDeviceRGB());
    CGBitmapInfo bitmapInfo = static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big;
    ScopedCGContextRef context(colorSpace
        ? CGBitmapContextCreate(rgba.data(), outWidth, outHeight, 8, outWidth * 4, colorSpace.get(),
                                bitmapInfo)
        : nullptr);
    if (!context) {
        outWidth = 0;
        outHeight = 0;
        return {};
    }

    CGContextDrawImage(context.get(), CGRectMake(0, 0, outWidth, outHeight), image.get());

    std::vector<uint8_t> rgb(static_cast<size_t>(outWidth * outHeight * 3));
    for (int y = 0; y < outHeight; ++y) {
        for (int x = 0; x < outWidth; ++x) {
            size_t src = static_cast<size_t>((y * outWidth + x) * 4);
            size_t dst = static_cast<size_t>((y * outWidth + x) * 3);
            rgb[dst] = rgba[src];
            rgb[dst + 1] = rgba[src + 1];
            rgb[dst + 2] = rgba[src + 2];
        }
    }
    return rgb;
}

SnapshotResult TakeSnapshot(const SnapshotOptions& options) {
    SnapshotResult result;
    result.frontmostApp = GetFrontmostApp();
    result.frontmostWindowBounds = GetFrontmostWindowBounds();

    std::ostringstream out;
    out << "Computer: local\n";
    if (result.frontmostApp.available) {
        RefRecord appRef;
        appRef.ref = "a1";
        appRef.kind = "app";
        appRef.source = "platform";
        appRef.role = "application";
        appRef.name = result.frontmostApp.name;
        appRef.app = result.frontmostApp.name;
        appRef.pid = result.frontmostApp.pid;
        result.refs.push_back(appRef);

        out << "Frontmost: " << result.frontmostApp.name
            << " [@" << appRef.ref << "]"
            << " bundle=" << result.frontmostApp.bundleId
            << " pid=" << result.frontmostApp.pid << "\n";
    } else {
        out << "Frontmost: unknown\n";
    }

    if (result.frontmostWindowBounds.available) {
        RefRecord windowRef;
        windowRef.ref = "w1";
        windowRef.kind = "window";
        windowRef.source = "platform";
        windowRef.role = "window";
        windowRef.name = "frontmost window";
        windowRef.app = result.frontmostApp.name;
        windowRef.pid = result.frontmostApp.pid;
        windowRef.bounds = result.frontmostWindowBounds;
        result.refs.push_back(windowRef);

        out << "Window: frontmost [@" << windowRef.ref << "]";
        if (options.includeBounds) {
            out << " bounds=" << static_cast<int>(std::round(windowRef.bounds.x)) << ","
                << static_cast<int>(std::round(windowRef.bounds.y)) << ","
                << static_cast<int>(std::round(windowRef.bounds.width)) << ","
                << static_cast<int>(std::round(windowRef.bounds.height));
        }
        out << "\n";
    }

    if (!AXIsProcessTrusted()) {
        result.warning = "Accessibility permission is not granted; element snapshot is unavailable.";
        out << "Warning: " << result.warning << "\n";
        result.text = out.str();
        return result;
    }

    if (!result.frontmostApp.available || result.frontmostApp.pid <= 0) {
        result.warning = "No frontmost application is available.";
        out << "Warning: " << result.warning << "\n";
        result.text = out.str();
        return result;
    }

    ScopedCFRef<AXUIElementRef> appElement(AXUIElementCreateApplication(result.frontmostApp.pid));
    if (!appElement) {
        result.warning = "Could not create accessibility application element.";
        out << "Warning: " << result.warning << "\n";
        result.text = out.str();
        return result;
    }

    ScopedCFRef<AXUIElementRef> root(CopyFocusedWindow(appElement.get()));
    if (!root) {
        CFRetain(appElement.get());
        root.reset(appElement.get());
    }

    int nextElementRef = 1;
    int visitedNodes = 0;
    bool truncated = false;
    AppendElementSnapshot(root.get(),
                          options,
                          result.frontmostApp.name,
                          result.frontmostApp.pid,
                          0,
                          nextElementRef,
                          visitedNodes,
                          truncated,
                          out,
                          result.refs);
    if (truncated) {
        result.warning = "Snapshot truncated at " + std::to_string(options.maxNodes) + " accessibility nodes.";
        out << "Warning: " << result.warning << "\n";
    }

    result.text = out.str();
    return result;
}

}
