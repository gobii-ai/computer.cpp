#include "computer_cpp/Platform.h"

namespace ComputerCpp::Platform {

PermissionStatus CheckPermissions(bool) { return {}; }
bool OpenPermissionsSettings() { return false; }
bool OpenAccessibilitySettings() { return false; }
bool OpenScreenCaptureSettings() { return false; }
bool RequestAccessibilityPermission() { return false; }
bool RequestScreenCapturePermission() { return false; }
AppInfo GetFrontmostApp() { return {}; }
std::string GetFrontmostAppSummary() { return "unsupported"; }
FocusedElementInfo GetFocusedElementInfo() { return {}; }
Bounds GetFrontmostWindowBounds() { return {}; }
WindowInfo GetActiveWindow() { return {}; }
std::vector<WindowInfo> ListWindows(const std::string&) { return {}; }
bool CloseWindow(const std::string&) { return false; }
bool SetFrontmostWindowBounds(const Bounds&) { return false; }
bool SetWindowBoundsForPid(int, const Bounds&) { return false; }
int GetFrontmostAppPid() { return -1; }
bool ActivateAppByPid(int) { return false; }
bool LaunchOrActivateApp(const std::string&, AppInfo&) { return false; }
bool OpenUrl(const std::string&, const std::string&, bool, bool) { return false; }
void ActivateAgentApp() {}
void DeactivateAgentApp() {}
void GetScreenSize(int& width, int& height) { width = 0; height = 0; }
void GetCursorPosition(double& x, double& y) { x = 0.0; y = 0.0; }
void MoveMouse(double, double) {}
void MoveMouseSmooth(double, double, int, int) {}
void DragMouseSmooth(double, double, double, double, const std::string&, int, int) {}
bool ClickSmooth(double, double, const std::string&, int, int, int) { return false; }
void MouseDown(const std::string&, int) {}
void MouseUp(const std::string&, int) {}
void Scroll(int, int) {}
void ScrollGesture(int, int, int, int, double) {}
bool Click(double, double, const std::string&, int) { return false; }
int ResolveKeycode(const std::string&) { return -1; }
bool SendHotkey(const std::vector<std::string>&, int) { return false; }
bool TypeCharacter(const std::string&, int) { return false; }
bool TypeText(const std::string&, int) { return false; }
bool PasteText(const std::string&) { return false; }
std::vector<std::string> GetSelectAllHotkey() { return {"control", "a"}; }
bool ReadClipboardText(std::string&) { return false; }
bool WriteClipboardText(const std::string&) { return false; }
bool SaveScreenshot(const std::string&) { return false; }
bool SaveScreenshotRegion(const std::string&, const Bounds&) { return false; }
bool SaveScreenshotScaled(const std::string&, int) { return false; }
bool SaveScreenshotRegionScaled(const std::string&, const Bounds&, int) { return false; }
std::vector<uint8_t> CaptureScreenRaw(int& width, int& height) { width = 0; height = 0; return {}; }
SnapshotResult TakeSnapshot(const SnapshotOptions&) {
    SnapshotResult result;
    result.warning = "Windows platform adapter is not implemented yet.";
    result.text = "Computer: local\nWarning: Windows platform adapter is not implemented yet.\n";
    return result;
}

}
