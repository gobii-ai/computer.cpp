#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ComputerCpp::Platform {

struct Bounds {
    bool available = false;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

struct PermissionStatus {
    bool accessibility = false;
    bool screenCapture = false;
};

struct AppInfo {
    bool available = false;
    int pid = -1;
    std::string name;
    std::string bundleId;
};

struct WindowInfo {
    bool available = false;
    std::string id;
    std::string title;
    std::string appClass;
    int pid = -1;
    bool active = false;
    Bounds bounds;
};

struct FocusedElementInfo {
    bool available = false;
    bool acceptsTextInput = false;
    bool valueSettable = false;
    Bounds bounds;
    std::string role;
    std::string subrole;
    std::string title;
    std::string description;
    std::string value;
};

struct RefRecord {
    std::string ref;
    std::string kind;
    std::string source;
    std::string role;
    std::string name;
    std::string value;
    std::string app;
    int pid = -1;
    Bounds bounds;
    double confidence = 1.0;
};

struct SnapshotOptions {
    bool interactiveOnly = false;
    bool includeBounds = false;
    bool includeActions = false;
    int maxDepth = 8;
    int maxNodes = 700;
};

struct SnapshotResult {
    AppInfo frontmostApp;
    Bounds frontmostWindowBounds;
    std::string text;
    std::vector<RefRecord> refs;
    std::string warning;
};

PermissionStatus CheckPermissions(bool requestIfMissing);
bool OpenPermissionsSettings();
bool OpenAccessibilitySettings();
bool OpenScreenCaptureSettings();
bool RequestAccessibilityPermission();
bool RequestScreenCapturePermission();

AppInfo GetFrontmostApp();
std::string GetFrontmostAppSummary();
FocusedElementInfo GetFocusedElementInfo();
Bounds GetFrontmostWindowBounds();
WindowInfo GetActiveWindow();
std::vector<WindowInfo> ListWindows(const std::string& appQuery = "");
bool CloseWindow(const std::string& id);
bool SetFrontmostWindowBounds(const Bounds& bounds);
bool SetWindowBoundsForPid(int pid, const Bounds& bounds);
int GetFrontmostAppPid();
bool ActivateAppByPid(int pid);
bool LaunchOrActivateApp(const std::string& query, AppInfo& appInfo);
bool OpenUrl(const std::string& url, const std::string& browser, bool newWindow, bool newInstance);
void ActivateAgentApp();
void DeactivateAgentApp();

void GetScreenSize(int& width, int& height);
void GetCursorPosition(double& x, double& y);
void MoveMouse(double x, double y);
void MoveMouseSmooth(double x, double y, int durationMs, int steps);
void DragMouseSmooth(double fromX, double fromY, double toX, double toY, const std::string& button, int durationMs, int steps);
bool ClickSmooth(double x, double y, const std::string& button, int clickCount, int durationMs, int steps);
void MouseDown(const std::string& button, int clickCount = 1);
void MouseUp(const std::string& button, int clickCount = 1);
void Scroll(int deltaY, int deltaX);
void ScrollGesture(int deltaY, int deltaX, int durationMs, int steps, double jitter);
bool Click(double x, double y, const std::string& button, int clickCount);

int ResolveKeycode(const std::string& keyName);
bool SendHotkey(const std::vector<std::string>& keys, int holdMs);
bool TypeCharacter(const std::string& character, int holdMs);
bool TypeText(const std::string& text, int holdMs);
bool PasteText(const std::string& text);
std::vector<std::string> GetSelectAllHotkey();

bool ReadClipboardText(std::string& text);
bool WriteClipboardText(const std::string& text);

bool SaveScreenshot(const std::string& filePath);
bool SaveScreenshotRegion(const std::string& filePath, const Bounds& bounds);
bool SaveScreenshotScaled(const std::string& filePath, int maxDimension);
bool SaveScreenshotRegionScaled(const std::string& filePath, const Bounds& bounds, int maxDimension);
std::vector<uint8_t> CaptureScreenRaw(int& outWidth, int& outHeight);
SnapshotResult TakeSnapshot(const SnapshotOptions& options);

}
