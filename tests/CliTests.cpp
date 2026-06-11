#include "TestSupport.h"

#include "CliCommands.h"
#include "CliConfig.h"
#include "CliOptions.h"
#include "CliRunCommand.h"
#include "CliSession.h"
#include "CliSessionParsing.h"
#include "computer_cpp/AppConfig.h"
#include "computer_cpp/AppPaths.h"
#include "computer_cpp/LuaRunner.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#endif

namespace {

struct ScopedEnvVar {
    std::string name;
    std::string previous;
    bool hadPrevious = false;

    explicit ScopedEnvVar(std::string variableName) : name(std::move(variableName)) {
        const char* value = std::getenv(name.c_str());
        if (value != nullptr) {
            hadPrevious = true;
            previous = value;
        }
    }

    ~ScopedEnvVar() {
        if (hadPrevious) {
            setenv(name.c_str(), previous.c_str(), 1);
        } else {
            unsetenv(name.c_str());
        }
    }
};

std::filesystem::path RepoRoot() {
    return std::filesystem::path(__FILE__).parent_path().parent_path();
}

struct CapturedConfigCommand {
    int exitCode = 0;
    std::string stdoutText;
    std::string stderrText;
};

CapturedConfigCommand RunConfigCommand(std::initializer_list<std::string> args, bool jsonOutput = true) {
    ComputerCpp::Cli::CliOptions options;
    options.jsonOutput = jsonOutput;
    std::ostringstream stdoutCapture;
    std::ostringstream stderrCapture;
    auto* oldOut = std::cout.rdbuf(stdoutCapture.rdbuf());
    auto* oldErr = std::cerr.rdbuf(stderrCapture.rdbuf());
    int exitCode = ComputerCpp::Cli::HandleConfigCommand(options, std::vector<std::string>(args));
    std::cout.rdbuf(oldOut);
    std::cerr.rdbuf(oldErr);
    return {exitCode, stdoutCapture.str(), stderrCapture.str()};
}

std::vector<std::string> ParseGlobalArgs(
    const std::vector<std::string>& input,
    ComputerCpp::Cli::CliOptions& options
) {
    std::vector<std::string> storage;
    storage.reserve(input.size() + 1);
    storage.push_back("computer.cpp");
    storage.insert(storage.end(), input.begin(), input.end());

    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (auto& arg : storage) {
        argv.push_back(arg.data());
    }
    return ComputerCpp::Cli::ParseGlobalOptions(static_cast<int>(argv.size()), argv.data(), options);
}

void TestGlobalOptionParsing() {
    ComputerCpp::Cli::CliOptions options;
    auto args = ParseGlobalArgs(
        {
            "--session",
            "contest",
            "--control-session",
            "token",
            "--control-scope",
            "desktop:test",
            "--json",
            "--no-start",
            "ping",
        },
        options);

    assert(options.parseError.empty());
    assert(options.session == "contest");
    assert(options.controlSessionToken == "token");
    assert(options.controlScope == "desktop:test");
    assert(options.jsonOutput);
    assert(options.noStart);
    assert(args.size() == 1);
    assert(args[0] == "ping");

    ComputerCpp::Cli::CliOptions missingSession;
    auto sessionArgs = ParseGlobalArgs({"--session"}, missingSession);
    assert(missingSession.parseError.find("--session requires a value") != std::string::npos);
    assert(sessionArgs.empty());

    ComputerCpp::Cli::CliOptions missingControlSession;
    auto controlSessionArgs = ParseGlobalArgs({"ping", "--control-session"}, missingControlSession);
    assert(missingControlSession.parseError.find("--control-session requires a value") != std::string::npos);
    assert(controlSessionArgs.size() == 1);
    assert(controlSessionArgs[0] == "ping");

    ComputerCpp::Cli::CliOptions blankControlSession;
    auto blankControlSessionArgs = ParseGlobalArgs({"--control-session", "   "}, blankControlSession);
    assert(blankControlSession.parseError.find("--control-session requires a non-empty value") != std::string::npos);
    assert(blankControlSessionArgs.empty());

    ComputerCpp::Cli::CliOptions missingControlSessionToken;
    auto controlSessionTokenArgs = ParseGlobalArgs({"--control-session-token"}, missingControlSessionToken);
    assert(missingControlSessionToken.parseError.find("--control-session-token requires a value") != std::string::npos);
    assert(controlSessionTokenArgs.empty());

    ComputerCpp::Cli::CliOptions missingControlScope;
    auto controlScopeArgs = ParseGlobalArgs({"--control-scope"}, missingControlScope);
    assert(missingControlScope.parseError.find("--control-scope requires a value") != std::string::npos);
    assert(controlScopeArgs.empty());

    ComputerCpp::Cli::CliOptions blankControlScope;
    auto blankControlScopeArgs = ParseGlobalArgs({"--control-scope", "   "}, blankControlScope);
    assert(blankControlScope.parseError.find("--control-scope requires a non-empty value") != std::string::npos);
    assert(blankControlScopeArgs.empty());

    ScopedEnvVar controlSessionEnv("COMPUTER_CPP_CONTROL_SESSION");
    ScopedEnvVar controlScopeEnv("COMPUTER_CPP_CONTROL_SCOPE");

    setenv("COMPUTER_CPP_CONTROL_SESSION", "   ", 1);
    setenv("COMPUTER_CPP_CONTROL_SCOPE", "   ", 1);
    ComputerCpp::Cli::CliOptions blankEnvDefaults;
    ComputerCpp::Cli::ApplyEnvDefaults(blankEnvDefaults);
    assert(blankEnvDefaults.controlSessionToken.empty());
    assert(blankEnvDefaults.controlScope == "desktop:local");

    setenv("COMPUTER_CPP_CONTROL_SESSION", "env-token", 1);
    setenv("COMPUTER_CPP_CONTROL_SCOPE", "desktop:env", 1);
    ComputerCpp::Cli::CliOptions envDefaults;
    ComputerCpp::Cli::ApplyEnvDefaults(envDefaults);
    assert(envDefaults.controlSessionToken == "env-token");
    assert(envDefaults.controlScope == "desktop:env");
}

void TestCliCommandBuilders() {
    auto app = ComputerCpp::Cli::BuildAppCommand({"app", "activate", "Safari"});
    assert(app.ok());
    assert(app.method == "app_launch");
    assert(app.params["query"] == "Safari");
    auto appPid = ComputerCpp::Cli::BuildAppCommand({"app", "activate-pid", "1234"});
    assert(appPid.ok());
    assert(appPid.method == "app_activate_pid");
    assert(appPid.params["pid"] == 1234);

    auto openUrl = ComputerCpp::Cli::BuildOpenCommand({
        "open",
        "url",
        "https://example.com",
        "--browser",
        "Safari",
        "--no-new-window",
        "--new-instance"
    });
    assert(openUrl.ok());
    assert(openUrl.method == "open_url");
    assert(openUrl.params["url"] == "https://example.com");
    assert(openUrl.params["browser"] == "Safari");
    assert(openUrl.params["newWindow"] == false);
    assert(openUrl.params["newInstance"] == true);
    auto routedOpenUrl = ComputerCpp::Cli::BuildDaemonCommand({"open", "url", "https://example.com"});
    assert(routedOpenUrl.ok());
    assert(routedOpenUrl.method == "open_url");
    assert(routedOpenUrl.params["url"] == "https://example.com");

    auto observe = ComputerCpp::Cli::BuildObserveCommand({"observe", "frames", "last", "3"});
    assert(observe.ok());
    assert(observe.method == "observe_frames");
    assert(observe.params["event"] == "last");
    assert(observe.params["limit"] == 3);

    auto target = ComputerCpp::Cli::BuildTargetCommand({
        "target",
        "find",
        "role",
        "button",
        "Submit",
        "5"
    });
    assert(target.ok());
    assert(target.method == "target_find");
    assert(target.params["query"] == "role:button[name=\"Submit\"]");
    assert(target.params["limit"] == 5);

    auto get = ComputerCpp::Cli::BuildGetCommand({"get", "@e1", "text"});
    assert(get.ok());
    assert(get.method == "get");
    assert(get.params["target"] == "@e1");
    assert(get.params["field"] == "text");

    auto getAll = ComputerCpp::Cli::BuildGetCommand({"get", "@e1"});
    assert(getAll.ok());
    assert(getAll.params["field"] == "all");

    auto permissions = ComputerCpp::Cli::BuildPermissionsCommand({"permissions", "open-settings", "screen"});
    assert(permissions.ok());
    assert(permissions.method == "open_permissions");
    assert(permissions.params["pane"] == "screen");

    auto image = ComputerCpp::Cli::BuildImageCommand({
        "image",
        "split",
        "/tmp/source.png",
        "--out-dir",
        "/tmp/chunks",
        "--chunk-height",
        "128",
        "--overlap",
        "8",
        "--prefix",
        "tile",
    });
    assert(image.ok());
    assert(image.method == "image_split");
    assert(image.params["path"] == "/tmp/source.png");
    assert(image.params["outDir"] == "/tmp/chunks");
    assert(image.params["chunkHeight"] == 128);
    assert(image.params["overlap"] == 8);
    assert(image.params["prefix"] == "tile");

    auto imageOutputDir = ComputerCpp::Cli::BuildImageCommand({
        "image",
        "split",
        "/tmp/source.png",
        "--output-dir",
        "/tmp/output-chunks",
    });
    assert(imageOutputDir.ok());
    assert(imageOutputDir.method == "image_split");
    assert(imageOutputDir.params["outDir"] == "/tmp/output-chunks");

    auto click = ComputerCpp::Cli::BuildClickCommand({
        "click",
        "role:button[name=\"Save\"]",
        "--count",
        "2",
        "--hover-safe",
        "--park-before-click",
        "--park-x-fraction",
        "0.82",
        "--park-y-fraction",
        "0.45",
        "--park-duration-ms",
        "180",
        "--park-steps",
        "7",
        "--rect-click-x-fraction",
        "0.30",
        "--rect-click-y-fraction",
        "0.70",
    });
    assert(click.ok());
    assert(click.method == "click");
    assert(click.params["target"] == "role:button[name=\"Save\"]");
    assert(click.params["clickCount"] == 2);
    assert(click.params["hoverSafe"] == true);
    assert(click.params["motion"] == "hover_safe");
    assert(click.params["parkBeforeClick"] == true);
    assert(click.params["parkXFraction"] == 0.82);
    assert(click.params["parkYFraction"] == 0.45);
    assert(click.params["parkDurationMs"] == 180);
    assert(click.params["parkSteps"] == 7);
    assert(click.params["rectClickXFraction"] == 0.30);
    assert(click.params["rectClickYFraction"] == 0.70);

    auto mouse = ComputerCpp::Cli::BuildMouseCommand({
        "mouse",
        "drag",
        "1",
        "2",
        "30",
        "40",
        "--button",
        "right",
        "--duration-ms",
        "250",
        "--observe",
    });
    assert(mouse.ok());
    assert(mouse.method == "mouse_drag");
    assert(mouse.params["from"] == "point:1,2");
    assert(mouse.params["to"] == "point:30,40");
    assert(mouse.params["button"] == "right");
    assert(mouse.params["durationMs"] == 250);
    assert(mouse.params["observe"] == true);

    auto mouseClick = ComputerCpp::Cli::BuildMouseCommand({
        "mouse",
        "click",
        "12",
        "34",
        "--button",
        "right",
        "--count",
        "2",
        "--duration-ms",
        "75",
        "--steps",
        "4",
        "--motion",
        "linear",
    });
    assert(mouseClick.ok());
    assert(mouseClick.method == "click");
    assert(mouseClick.params["target"] == "point:12,34");
    assert(mouseClick.params["button"] == "right");
    assert(mouseClick.params["clickCount"] == 2);
    assert(mouseClick.params["durationMs"] == 75);
    assert(mouseClick.params["steps"] == 4);
    assert(mouseClick.params["motion"] == "linear");

    auto instantMouseClick = ComputerCpp::Cli::BuildMouseCommand({"mouse", "click", "12", "34", "--instant"});
    assert(instantMouseClick.ok());
    assert(instantMouseClick.method == "click");
    assert(instantMouseClick.params["motion"] == "instant");
    auto mouseDownCount = ComputerCpp::Cli::BuildMouseCommand({"mouse", "down", "right", "--count", "2"});
    assert(mouseDownCount.ok());
    assert(mouseDownCount.method == "mouse_down");
    assert(mouseDownCount.params["button"] == "right");
    assert(mouseDownCount.params["clickCount"] == 2);
    auto mouseUpDefaultButtonCount = ComputerCpp::Cli::BuildMouseCommand({"mouse", "up", "--count", "3"});
    assert(mouseUpDefaultButtonCount.ok());
    assert(mouseUpDefaultButtonCount.method == "mouse_up");
    assert(mouseUpDefaultButtonCount.params["button"] == "left");
    assert(mouseUpDefaultButtonCount.params["clickCount"] == 3);

    auto window = ComputerCpp::Cli::BuildWindowCommand({
        "window",
        "bounds",
        "10",
        "20",
        "640",
        "480",
        "--pid",
        "123",
    });
    assert(window.ok());
    assert(window.method == "window_bounds");
    assert(window.params["x"] == 10.0);
    auto windowList = ComputerCpp::Cli::BuildWindowCommand({"window", "list", "Finder"});
    assert(windowList.ok());
    assert(windowList.method == "window_list");
    assert(windowList.params["app"] == "Finder");
    auto windowActive = ComputerCpp::Cli::BuildWindowCommand({"window", "active"});
    assert(windowActive.ok());
    assert(windowActive.method == "window_active");
    auto windowClose = ComputerCpp::Cli::BuildWindowCommand({"window", "close", "abc"});
    assert(windowClose.ok());
    assert(windowClose.method == "window_close");
    assert(windowClose.params["id"] == "abc");
    assert(windowClose.params["frontmost"] == false);
    auto windowCloseFrontmost = ComputerCpp::Cli::BuildWindowCommand({"window", "close", "--frontmost"});
    assert(windowCloseFrontmost.ok());
    assert(windowCloseFrontmost.method == "window_close");
    assert(windowCloseFrontmost.params["frontmost"] == true);
    assert(!windowCloseFrontmost.params.contains("id"));
    assert(window.params["y"] == 20.0);
    assert(window.params["width"] == 640.0);
    assert(window.params["height"] == 480.0);
    assert(window.params["pid"] == 123);

    auto snapshot = ComputerCpp::Cli::BuildSnapshotCommand({
        "snapshot",
        "--interactive",
        "--with-bounds",
        "--with-actions",
        "--max-depth",
        "4",
        "--max-nodes",
        "99",
    });
    assert(snapshot.ok());
    assert(snapshot.method == "snapshot");
    assert(snapshot.params["interactive"] == true);
    assert(snapshot.params["bounds"] == true);
    assert(snapshot.params["actions"] == true);
    assert(snapshot.params["maxDepth"] == 4);
    assert(snapshot.params["maxNodes"] == 99);

    auto screenshot = ComputerCpp::Cli::BuildScreenshotCommand({
        "screenshot",
        "/tmp/shot.png",
        "--frontmost-window",
        "--max-dim",
        "1200",
    });
    assert(screenshot.ok());
    assert(screenshot.method == "screenshot");
    assert(screenshot.params["path"] == "/tmp/shot.png");
    assert(screenshot.params["frontmostWindowOnly"] == true);
    assert(screenshot.params["maxDimension"] == 1200);

    auto screenshotOutput = ComputerCpp::Cli::BuildScreenshotCommand({
        "screenshot",
        "--output",
        "/tmp/output.png",
        "--max-dim",
        "800",
    });
    assert(screenshotOutput.ok());
    assert(screenshotOutput.params["path"] == "/tmp/output.png");
    assert(screenshotOutput.params["maxDimension"] == 800);

    auto screenshotPath = ComputerCpp::Cli::BuildScreenshotCommand({
        "screenshot",
        "--path",
        "/tmp/path.png",
        "--frontmost-window",
    });
    assert(screenshotPath.ok());
    assert(screenshotPath.params["path"] == "/tmp/path.png");
    assert(screenshotPath.params["frontmostWindowOnly"] == true);

    auto screenshotRegion = ComputerCpp::Cli::BuildScreenshotCommand({
        "screenshot",
        "/tmp/region.png",
        "--region",
        "10",
        "20",
        "300",
        "200",
        "--max-dim",
        "600",
    });
    assert(screenshotRegion.ok());
    assert(screenshotRegion.params["path"] == "/tmp/region.png");
    assert(screenshotRegion.params["x"] == 10.0);
    assert(screenshotRegion.params["y"] == 20.0);
    assert(screenshotRegion.params["width"] == 300.0);
    assert(screenshotRegion.params["height"] == 200.0);
    assert(screenshotRegion.params["maxDimension"] == 600);

    auto scroll = ComputerCpp::Cli::BuildScrollCommand({
        "scroll",
        "read",
        "--samples",
        "6",
        "--at",
        "role:scrollarea",
        "--no-anchor",
        "--max-gesture-delta",
        "120",
        "--no-humanize",
    });
    assert(scroll.ok());
    assert(scroll.method == "scroll");
    assert(scroll.params["dy"] == -200);
    assert(scroll.params["dx"] == 0);
    assert(scroll.params["samples"] == 6);
    assert(scroll.params["at"] == "role:scrollarea");
    assert(scroll.params["centerAnchor"] == true);
    assert(scroll.params["anchor"] == false);
    assert(scroll.params["maxGestureDelta"] == 120);
    assert(scroll.params["humanize"] == false);

    auto humanizedScroll = ComputerCpp::Cli::BuildScrollCommand({"scroll", "120", "--humanize"});
    assert(humanizedScroll.ok());
    assert(humanizedScroll.params["humanize"] == true);
    auto centeredScroll = ComputerCpp::Cli::BuildScrollCommand({"scroll", "120", "--center-anchor"});
    assert(centeredScroll.ok());
    assert(centeredScroll.params["centerAnchor"] == true);
    auto nonCenteredReadScroll = ComputerCpp::Cli::BuildScrollCommand({"scroll", "read", "--no-center-anchor"});
    assert(nonCenteredReadScroll.ok());
    assert(nonCenteredReadScroll.params["centerAnchor"] == false);

    auto wait = ComputerCpp::Cli::BuildWaitCommand({
        "wait",
        "--frontmost",
        "Finder",
        "--timeout-ms",
        "500",
        "--poll-ms",
        "50"
    });
    assert(wait.ok());
    assert(wait.method == "wait");
    assert(wait.params["frontmost"] == "Finder");
    assert(wait.params["timeoutMs"] == 500);
    assert(wait.params["pollMs"] == 50);

    auto press = ComputerCpp::Cli::BuildPressCommand({"press", "Cmd+L", "--hold-ms", "125"});
    assert(press.ok());
    assert(press.method == "press");
    assert(press.params["keys"] == "Cmd+L");
    assert(press.params["holdMs"] == 125);

    auto type = ComputerCpp::Cli::BuildTypeCommand({"type", "hello", "--target", "role:textbox", "--paste", "--hold-ms", "45"});
    assert(type.ok());
    assert(type.method == "type");
    assert(type.params["text"] == "hello");
    assert(type.params["target"] == "role:textbox");
    assert(type.params["paste"] == true);
    assert(type.params["holdMs"] == 45);

    auto clipboard = ComputerCpp::Cli::BuildClipboardCommand({"clipboard", "write", "hello"});
    assert(clipboard.ok());
    assert(clipboard.method == "clipboard_write");
    assert(clipboard.params["text"] == "hello");

    auto routedState = ComputerCpp::Cli::BuildDaemonCommand({"doctor"});
    assert(routedState.ok());
    assert(routedState.method == "state");

    auto routedPing = ComputerCpp::Cli::BuildDaemonCommand({"ping"});
    assert(routedPing.ok());
    assert(routedPing.method == "ping");

    auto routedApp = ComputerCpp::Cli::BuildDaemonCommand({"app", "active"});
    assert(routedApp.ok());
    assert(routedApp.method == "app_active");

    auto routedClipboard = ComputerCpp::Cli::BuildDaemonCommand({"clipboard", "read"});
    assert(routedClipboard.ok());
    assert(routedClipboard.method == "clipboard_read");

    auto routedBatch = ComputerCpp::Cli::BuildDaemonCommand({"batch", "--continue-on-error"}, R"([{"method":"ping"}])");
    assert(routedBatch.ok());
    assert(routedBatch.method == "batch");
    assert(routedBatch.params["steps"].is_array());
    assert(routedBatch.params["steps"].size() == 1);
    assert(routedBatch.params["stopOnError"] == false);

    auto invalidBatch = ComputerCpp::Cli::BuildDaemonCommand({"batch"}, "{}");
    assert(!invalidBatch.ok());
    assert(invalidBatch.error.find("batch stdin must be a JSON array") != std::string::npos);

    auto unknownBatchOption = ComputerCpp::Cli::BuildDaemonCommand({"batch", "--keep-going"}, R"([{"method":"ping"}])");
    assert(!unknownBatchOption.ok());
    assert(unknownBatchOption.error.find("unknown batch option") != std::string::npos);

    auto unknownPingOption = ComputerCpp::Cli::BuildDaemonCommand({"ping", "--raw"});
    assert(!unknownPingOption.ok());
    assert(unknownPingOption.error.find("unknown ping option") != std::string::npos);

    auto unknownSchemaOption = ComputerCpp::Cli::BuildDaemonCommand({"schema", "extra"});
    assert(!unknownSchemaOption.ok());
    assert(unknownSchemaOption.error.find("unknown schema option") != std::string::npos);

    auto unknownStateOption = ComputerCpp::Cli::BuildDaemonCommand({"state", "extra"});
    assert(!unknownStateOption.ok());
    assert(unknownStateOption.error.find("unknown state option") != std::string::npos);

    auto unknownDoctorOption = ComputerCpp::Cli::BuildDaemonCommand({"doctor", "extra"});
    assert(!unknownDoctorOption.ok());
    assert(unknownDoctorOption.error.find("unknown doctor option") != std::string::npos);

    auto unknown = ComputerCpp::Cli::BuildDaemonCommand({"not-a-command"});
    assert(!unknown.ok());
    assert(unknown.error.find("unknown command") != std::string::npos);

    auto unknownAppActiveOption = ComputerCpp::Cli::BuildAppCommand({"app", "active", "--verbose"});
    assert(!unknownAppActiveOption.ok());
    assert(unknownAppActiveOption.error.find("unknown app active option") != std::string::npos);

    auto unknownAppLaunchOption = ComputerCpp::Cli::BuildAppCommand({"app", "launch", "Safari", "--new"});
    assert(!unknownAppLaunchOption.ok());
    assert(unknownAppLaunchOption.error.find("unknown app launch option") != std::string::npos);

    auto emptyAppLaunch = ComputerCpp::Cli::BuildAppCommand({"app", "launch", ""});
    assert(!emptyAppLaunch.ok());
    assert(emptyAppLaunch.error.find("requires non-empty app name") != std::string::npos);

    auto blankAppLaunch = ComputerCpp::Cli::BuildAppCommand({"app", "launch", "   "});
    assert(!blankAppLaunch.ok());
    assert(blankAppLaunch.error.find("requires non-empty app name") != std::string::npos);

    auto missingAppActivatePid = ComputerCpp::Cli::BuildAppCommand({"app", "activate-pid"});
    assert(!missingAppActivatePid.ok());
    assert(missingAppActivatePid.error.find("activate-pid requires pid") != std::string::npos);

    auto invalidAppActivatePid = ComputerCpp::Cli::BuildAppCommand({"app", "activate-pid", "abc"});
    assert(!invalidAppActivatePid.ok());
    assert(invalidAppActivatePid.error.find("requires an integer pid") != std::string::npos);

    auto nonPositiveAppActivatePid = ComputerCpp::Cli::BuildAppCommand({"app", "activate-pid", "0"});
    assert(!nonPositiveAppActivatePid.ok());
    assert(nonPositiveAppActivatePid.error.find("requires positive pid") != std::string::npos);

    auto unknownAppActivatePidOption = ComputerCpp::Cli::BuildAppCommand({"app", "activate-pid", "1234", "--force"});
    assert(!unknownAppActivatePidOption.ok());
    assert(unknownAppActivatePidOption.error.find("unknown app activate-pid option") != std::string::npos);

    auto missingOpenUrl = ComputerCpp::Cli::BuildOpenCommand({"open", "url"});
    assert(!missingOpenUrl.ok());
    assert(missingOpenUrl.error.find("requires http or https URL") != std::string::npos);

    auto invalidOpenUrlScheme = ComputerCpp::Cli::BuildOpenCommand({"open", "url", "file:///tmp/a"});
    assert(!invalidOpenUrlScheme.ok());
    assert(invalidOpenUrlScheme.error.find("requires http or https URL") != std::string::npos);

    auto emptyOpenBrowser = ComputerCpp::Cli::BuildOpenCommand({"open", "url", "https://example.com", "--browser", ""});
    assert(!emptyOpenBrowser.ok());
    assert(emptyOpenBrowser.error.find("--browser requires a non-empty value") != std::string::npos);

    auto unknownOpenUrlOption = ComputerCpp::Cli::BuildOpenCommand({"open", "url", "https://example.com", "--tab"});
    assert(!unknownOpenUrlOption.ok());
    assert(unknownOpenUrlOption.error.find("unknown open url option") != std::string::npos);

    auto unknownGetOption = ComputerCpp::Cli::BuildGetCommand({"get", "@e1", "text", "--raw"});
    assert(!unknownGetOption.ok());
    assert(unknownGetOption.error.find("unknown get option") != std::string::npos);

    auto invalidGetField = ComputerCpp::Cli::BuildGetCommand({"get", "@e1", "label"});
    assert(!invalidGetField.ok());
    assert(invalidGetField.error.find("get field must be text, value, bounds, or all") != std::string::npos);

    auto emptyGetRef = ComputerCpp::Cli::BuildGetCommand({"get", ""});
    assert(!emptyGetRef.ok());
    assert(emptyGetRef.error.find("non-empty ref") != std::string::npos);

    auto blankGetRef = ComputerCpp::Cli::BuildGetCommand({"get", "   "});
    assert(!blankGetRef.ok());
    assert(blankGetRef.error.find("non-empty ref") != std::string::npos);

    auto invalid = ComputerCpp::Cli::BuildMouseCommand({"mouse", "warp"});
    assert(!invalid.ok());
    assert(invalid.errorCode == 2);
    assert(invalid.error.find("unknown mouse subcommand") != std::string::npos);

    auto removedCanvas = ComputerCpp::Cli::BuildDaemonCommand({"canvas", "scan", "10", "20", "300", "200"});
    assert(!removedCanvas.ok());
    assert(removedCanvas.error.find("unknown command") != std::string::npos);

    auto invalidWindow = ComputerCpp::Cli::BuildWindowCommand({"window", "bounds", "0", "0", "wide", "200"});
    assert(!invalidWindow.ok());
    assert(invalidWindow.error.find("numeric x y width height") != std::string::npos);

    auto nonFiniteWindow = ComputerCpp::Cli::BuildWindowCommand({"window", "bounds", "0", "0", "inf", "200"});
    assert(!nonFiniteWindow.ok());
    assert(nonFiniteWindow.error.find("numeric x y width height") != std::string::npos);

    auto invalidWindowSize = ComputerCpp::Cli::BuildWindowCommand({"window", "bounds", "0", "0", "0", "200"});
    assert(!invalidWindowSize.ok());
    assert(invalidWindowSize.error.find("positive width and height") != std::string::npos);

    auto missingWindowPid = ComputerCpp::Cli::BuildWindowCommand({"window", "bounds", "0", "0", "100", "200", "--pid"});
    assert(!missingWindowPid.ok());
    assert(missingWindowPid.error.find("--pid requires a value") != std::string::npos);

    auto invalidWindowPid = ComputerCpp::Cli::BuildWindowCommand({"window", "bounds", "0", "0", "100", "200", "--pid", "-1"});
    assert(!invalidWindowPid.ok());
    assert(invalidWindowPid.error.find("--pid must be non-negative") != std::string::npos);

    auto unknownWindowOption = ComputerCpp::Cli::BuildWindowCommand({"window", "active", "--verbose"});
    assert(!unknownWindowOption.ok());
    assert(unknownWindowOption.error.find("unknown window active option") != std::string::npos);

    auto emptyWindowListApp = ComputerCpp::Cli::BuildWindowCommand({"window", "list", ""});
    assert(!emptyWindowListApp.ok());
    assert(emptyWindowListApp.error.find("app must be non-empty") != std::string::npos);

    auto blankWindowListApp = ComputerCpp::Cli::BuildWindowCommand({"window", "list", "   "});
    assert(!blankWindowListApp.ok());
    assert(blankWindowListApp.error.find("app must be non-empty") != std::string::npos);

    auto emptyWindowCloseId = ComputerCpp::Cli::BuildWindowCommand({"window", "close", ""});
    assert(!emptyWindowCloseId.ok());
    assert(emptyWindowCloseId.error.find("id must be non-empty") != std::string::npos);

    auto blankWindowCloseId = ComputerCpp::Cli::BuildWindowCommand({"window", "close", "   "});
    assert(!blankWindowCloseId.ok());
    assert(blankWindowCloseId.error.find("id must be non-empty") != std::string::npos);

    auto unknownWindowCloseFrontmostOption = ComputerCpp::Cli::BuildWindowCommand({"window", "close", "--frontmost", "--force"});
    assert(!unknownWindowCloseFrontmostOption.ok());
    assert(unknownWindowCloseFrontmostOption.error.find("unknown window close option") != std::string::npos);

    auto unknownPermissionsOption = ComputerCpp::Cli::BuildPermissionsCommand({"permissions", "--verbose"});
    assert(!unknownPermissionsOption.ok());
    assert(unknownPermissionsOption.error.find("unknown permissions option") != std::string::npos);

    auto unknownOpenSettingsOption = ComputerCpp::Cli::BuildPermissionsCommand({
        "permissions",
        "open-settings",
        "screen",
        "--extra",
    });
    assert(!unknownOpenSettingsOption.ok());
    assert(unknownOpenSettingsOption.error.find("unknown permissions open-settings option") != std::string::npos);

    auto emptyOpenSettingsPane = ComputerCpp::Cli::BuildPermissionsCommand({"permissions", "open-settings", ""});
    assert(!emptyOpenSettingsPane.ok());
    assert(emptyOpenSettingsPane.error.find("pane must be non-empty") != std::string::npos);

    auto blankOpenSettingsPane = ComputerCpp::Cli::BuildPermissionsCommand({"permissions", "open-settings", "   "});
    assert(!blankOpenSettingsPane.ok());
    assert(blankOpenSettingsPane.error.find("pane must be non-empty") != std::string::npos);

    auto invalidOpenSettingsPane = ComputerCpp::Cli::BuildPermissionsCommand({"permissions", "open-settings", "camera"});
    assert(!invalidOpenSettingsPane.ok());
    assert(invalidOpenSettingsPane.error.find("pane must be accessibility") != std::string::npos);

    auto invalidSnapshotDepth = ComputerCpp::Cli::BuildSnapshotCommand({"snapshot", "--max-depth"});
    assert(!invalidSnapshotDepth.ok());
    assert(invalidSnapshotDepth.error.find("--max-depth requires a value") != std::string::npos);

    auto negativeSnapshotDepth = ComputerCpp::Cli::BuildSnapshotCommand({"snapshot", "--max-depth", "-1"});
    assert(!negativeSnapshotDepth.ok());
    assert(negativeSnapshotDepth.error.find("--max-depth must be non-negative") != std::string::npos);

    auto missingSnapshotNodes = ComputerCpp::Cli::BuildSnapshotCommand({"snapshot", "--max-nodes"});
    assert(!missingSnapshotNodes.ok());
    assert(missingSnapshotNodes.error.find("--max-nodes requires a value") != std::string::npos);

    auto negativeSnapshotNodes = ComputerCpp::Cli::BuildSnapshotCommand({"snapshot", "--max-nodes", "-1"});
    assert(!negativeSnapshotNodes.ok());
    assert(negativeSnapshotNodes.error.find("--max-nodes must be non-negative") != std::string::npos);

    auto unknownSnapshotOption = ComputerCpp::Cli::BuildSnapshotCommand({"snapshot", "--full"});
    assert(!unknownSnapshotOption.ok());
    assert(unknownSnapshotOption.error.find("unknown snapshot option") != std::string::npos);

    auto invalidScreenshotMaxDim = ComputerCpp::Cli::BuildScreenshotCommand({"screenshot", "--max-dim"});
    assert(!invalidScreenshotMaxDim.ok());
    assert(invalidScreenshotMaxDim.error.find("--max-dim requires a value") != std::string::npos);

    auto invalidScreenshotMaxDimLow = ComputerCpp::Cli::BuildScreenshotCommand({"screenshot", "--max-dim", "-1"});
    assert(!invalidScreenshotMaxDimLow.ok());
    assert(invalidScreenshotMaxDimLow.error.find("--max-dim must be between 0 and 8192") != std::string::npos);

    auto invalidScreenshotMaxDimHigh = ComputerCpp::Cli::BuildScreenshotCommand({"screenshot", "--max-dim", "8193"});
    assert(!invalidScreenshotMaxDimHigh.ok());
    assert(invalidScreenshotMaxDimHigh.error.find("--max-dim must be between 0 and 8192") != std::string::npos);

    auto missingScreenshotRegion = ComputerCpp::Cli::BuildScreenshotCommand({"screenshot", "--region", "1", "2", "3"});
    assert(!missingScreenshotRegion.ok());
    assert(missingScreenshotRegion.error.find("--region requires x y width height") != std::string::npos);

    auto invalidScreenshotRegion = ComputerCpp::Cli::BuildScreenshotCommand({
        "screenshot",
        "--region",
        "1",
        "2px",
        "3",
        "4",
    });
    assert(!invalidScreenshotRegion.ok());
    assert(invalidScreenshotRegion.error.find("--region requires numeric x y width height") != std::string::npos);

    auto invalidScreenshotRegionSize = ComputerCpp::Cli::BuildScreenshotCommand({
        "screenshot",
        "--region",
        "1",
        "2",
        "0",
        "4",
    });
    assert(!invalidScreenshotRegionSize.ok());
    assert(invalidScreenshotRegionSize.error.find("--region requires positive width and height") != std::string::npos);

    auto unknownScreenshotOption = ComputerCpp::Cli::BuildScreenshotCommand({"screenshot", "--quality"});
    assert(!unknownScreenshotOption.ok());
    assert(unknownScreenshotOption.error.find("unknown screenshot option") != std::string::npos);

    auto missingScreenshotOutput = ComputerCpp::Cli::BuildScreenshotCommand({"screenshot", "--output"});
    assert(!missingScreenshotOutput.ok());
    assert(missingScreenshotOutput.error.find("--output requires a path") != std::string::npos);

    auto missingScreenshotPathFlag = ComputerCpp::Cli::BuildScreenshotCommand({"screenshot", "--path"});
    assert(!missingScreenshotPathFlag.ok());
    assert(missingScreenshotPathFlag.error.find("--path requires a path") != std::string::npos);

    auto emptyScreenshotPath = ComputerCpp::Cli::BuildScreenshotCommand({"screenshot", ""});
    assert(emptyScreenshotPath.ok());
    assert(emptyScreenshotPath.params["path"] == "");

    auto blankScreenshotPath = ComputerCpp::Cli::BuildScreenshotCommand({"screenshot", "   "});
    assert(!blankScreenshotPath.ok());
    assert(blankScreenshotPath.error.find("path must be non-empty") != std::string::npos);

    auto blankScreenshotOutput = ComputerCpp::Cli::BuildScreenshotCommand({"screenshot", "--output", "   "});
    assert(!blankScreenshotOutput.ok());
    assert(blankScreenshotOutput.error.find("path must be non-empty") != std::string::npos);

    auto extraScreenshotPath = ComputerCpp::Cli::BuildScreenshotCommand({"screenshot", "/tmp/a.png", "/tmp/b.png"});
    assert(!extraScreenshotPath.ok());
    assert(extraScreenshotPath.error.find("at most one path") != std::string::npos);

    auto duplicateScreenshotOutput = ComputerCpp::Cli::BuildScreenshotCommand({
        "screenshot",
        "--output",
        "/tmp/a.png",
        "/tmp/b.png",
    });
    assert(!duplicateScreenshotOutput.ok());
    assert(duplicateScreenshotOutput.error.find("at most one path") != std::string::npos);

    auto invalidMouse = ComputerCpp::Cli::BuildMouseCommand({"mouse", "move", "left", "20"});
    assert(!invalidMouse.ok());
    assert(invalidMouse.error.find("numeric x y") != std::string::npos);

    auto nonFiniteMouseMove = ComputerCpp::Cli::BuildMouseCommand({"mouse", "move", "nan", "20"});
    assert(!nonFiniteMouseMove.ok());
    assert(nonFiniteMouseMove.error.find("numeric x y") != std::string::npos);

    auto missingMouseMoveDuration = ComputerCpp::Cli::BuildMouseCommand({"mouse", "move", "1", "2", "--duration-ms"});
    assert(!missingMouseMoveDuration.ok());
    assert(missingMouseMoveDuration.error.find("--duration-ms requires a value") != std::string::npos);

    auto missingMouseMoveSteps = ComputerCpp::Cli::BuildMouseCommand({"mouse", "move", "1", "2", "--steps"});
    assert(!missingMouseMoveSteps.ok());
    assert(missingMouseMoveSteps.error.find("--steps requires a value") != std::string::npos);

    auto invalidMouseClickCoordinate = ComputerCpp::Cli::BuildMouseCommand({"mouse", "click", "left", "20"});
    assert(!invalidMouseClickCoordinate.ok());
    assert(invalidMouseClickCoordinate.error.find("numeric x y") != std::string::npos);

    auto nonFiniteMouseClickCoordinate = ComputerCpp::Cli::BuildMouseCommand({"mouse", "click", "nan", "20"});
    assert(!nonFiniteMouseClickCoordinate.ok());
    assert(nonFiniteMouseClickCoordinate.error.find("numeric x y") != std::string::npos);

    auto unknownMouseClickOption = ComputerCpp::Cli::BuildMouseCommand({"mouse", "click", "1", "2", "--double"});
    assert(!unknownMouseClickOption.ok());
    assert(unknownMouseClickOption.error.find("unknown mouse click option") != std::string::npos);

    auto missingMouseClickButton = ComputerCpp::Cli::BuildMouseCommand({"mouse", "click", "1", "2", "--button"});
    assert(!missingMouseClickButton.ok());
    assert(missingMouseClickButton.error.find("--button requires a value") != std::string::npos);

    auto blankMouseClickButton = ComputerCpp::Cli::BuildMouseCommand({"mouse", "click", "1", "2", "--button", "   "});
    assert(!blankMouseClickButton.ok());
    assert(blankMouseClickButton.error.find("--button requires a non-empty value") != std::string::npos);

    auto missingMouseClickCount = ComputerCpp::Cli::BuildMouseCommand({"mouse", "click", "1", "2", "--count"});
    assert(!missingMouseClickCount.ok());
    assert(missingMouseClickCount.error.find("--count requires a value") != std::string::npos);

    auto invalidMouseClickCount = ComputerCpp::Cli::BuildMouseCommand({"mouse", "click", "1", "2", "--count", "0"});
    assert(!invalidMouseClickCount.ok());
    assert(invalidMouseClickCount.error.find("--count must be between 1 and 5") != std::string::npos);

    auto invalidMouseClickDuration = ComputerCpp::Cli::BuildMouseCommand({"mouse", "click", "1", "2", "--duration-ms", "-1"});
    assert(!invalidMouseClickDuration.ok());
    assert(invalidMouseClickDuration.error.find("--duration-ms must be between 0 and 5000") != std::string::npos);

    auto missingMouseClickDuration = ComputerCpp::Cli::BuildMouseCommand({"mouse", "click", "1", "2", "--duration-ms"});
    assert(!missingMouseClickDuration.ok());
    assert(missingMouseClickDuration.error.find("--duration-ms requires a value") != std::string::npos);

    auto invalidMouseClickSteps = ComputerCpp::Cli::BuildMouseCommand({"mouse", "click", "1", "2", "--steps", "121"});
    assert(!invalidMouseClickSteps.ok());
    assert(invalidMouseClickSteps.error.find("--steps must be between 0 and 120") != std::string::npos);

    auto missingMouseClickMotion = ComputerCpp::Cli::BuildMouseCommand({"mouse", "click", "1", "2", "--motion"});
    assert(!missingMouseClickMotion.ok());
    assert(missingMouseClickMotion.error.find("--motion requires a value") != std::string::npos);

    auto blankMouseClickMotion = ComputerCpp::Cli::BuildMouseCommand({"mouse", "click", "1", "2", "--motion", "   "});
    assert(!blankMouseClickMotion.ok());
    assert(blankMouseClickMotion.error.find("--motion requires a non-empty value") != std::string::npos);

    auto unknownMouseDownOption = ComputerCpp::Cli::BuildMouseCommand({"mouse", "down", "left", "--hold"});
    assert(!unknownMouseDownOption.ok());
    assert(unknownMouseDownOption.error.find("unknown mouse down option") != std::string::npos);

    auto unknownMouseUpOption = ComputerCpp::Cli::BuildMouseCommand({"mouse", "up", "left", "--hold"});
    assert(!unknownMouseUpOption.ok());
    assert(unknownMouseUpOption.error.find("unknown mouse up option") != std::string::npos);
    auto missingMouseDownCount = ComputerCpp::Cli::BuildMouseCommand({"mouse", "down", "--count"});
    assert(!missingMouseDownCount.ok());
    assert(missingMouseDownCount.error.find("mouse down --count requires a value") != std::string::npos);
    auto invalidMouseDownCount = ComputerCpp::Cli::BuildMouseCommand({"mouse", "down", "--count", "0"});
    assert(!invalidMouseDownCount.ok());
    assert(invalidMouseDownCount.error.find("mouse down --count must be between 1 and 5") != std::string::npos);
    auto invalidMouseUpCount = ComputerCpp::Cli::BuildMouseCommand({"mouse", "up", "left", "--count", "many"});
    assert(!invalidMouseUpCount.ok());
    assert(invalidMouseUpCount.error.find("mouse up --count requires an integer") != std::string::npos);

    auto invalidScroll = ComputerCpp::Cli::BuildScrollCommand({"scroll", "0", "0", "--jitter", "much"});
    assert(!invalidScroll.ok());
    assert(invalidScroll.error.find("--jitter requires a number") != std::string::npos);

    auto missingScrollDuration = ComputerCpp::Cli::BuildScrollCommand({"scroll", "0", "0", "--duration-ms"});
    assert(!missingScrollDuration.ok());
    assert(missingScrollDuration.error.find("--duration-ms requires a value") != std::string::npos);

    auto missingScrollSteps = ComputerCpp::Cli::BuildScrollCommand({"scroll", "0", "0", "--steps"});
    assert(!missingScrollSteps.ok());
    assert(missingScrollSteps.error.find("--steps requires a value") != std::string::npos);

    auto missingScrollJitter = ComputerCpp::Cli::BuildScrollCommand({"scroll", "0", "0", "--jitter"});
    assert(!missingScrollJitter.ok());
    assert(missingScrollJitter.error.find("--jitter requires a value") != std::string::npos);

    auto missingScrollSamples = ComputerCpp::Cli::BuildScrollCommand({"scroll", "0", "0", "--samples"});
    assert(!missingScrollSamples.ok());
    assert(missingScrollSamples.error.find("--samples requires a value") != std::string::npos);

    auto missingScrollMaxGestureDelta = ComputerCpp::Cli::BuildScrollCommand({"scroll", "0", "0", "--max-gesture-delta"});
    assert(!missingScrollMaxGestureDelta.ok());
    assert(missingScrollMaxGestureDelta.error.find("--max-gesture-delta requires a value") != std::string::npos);

    auto nonFiniteScrollJitter = ComputerCpp::Cli::BuildScrollCommand({"scroll", "0", "0", "--jitter", "nan"});
    assert(!nonFiniteScrollJitter.ok());
    assert(nonFiniteScrollJitter.error.find("--jitter requires a number") != std::string::npos);

    auto nonIntegerScrollMaxGestureDelta = ComputerCpp::Cli::BuildScrollCommand({"scroll", "0", "0", "--max-gesture-delta", "wide"});
    assert(!nonIntegerScrollMaxGestureDelta.ok());
    assert(nonIntegerScrollMaxGestureDelta.error.find("--max-gesture-delta requires an integer") != std::string::npos);

    auto invalidScrollDuration = ComputerCpp::Cli::BuildScrollCommand({"scroll", "0", "0", "--duration-ms", "-1"});
    assert(!invalidScrollDuration.ok());
    assert(invalidScrollDuration.error.find("--duration-ms must be non-negative") != std::string::npos);

    auto invalidScrollSteps = ComputerCpp::Cli::BuildScrollCommand({"scroll", "0", "0", "--steps", "-1"});
    assert(!invalidScrollSteps.ok());
    assert(invalidScrollSteps.error.find("--steps must be non-negative") != std::string::npos);

    auto invalidScrollJitter = ComputerCpp::Cli::BuildScrollCommand({"scroll", "0", "0", "--jitter", "-0.1"});
    assert(!invalidScrollJitter.ok());
    assert(invalidScrollJitter.error.find("--jitter must be non-negative") != std::string::npos);

    auto invalidScrollMaxGestureDelta = ComputerCpp::Cli::BuildScrollCommand({"scroll", "0", "0", "--max-gesture-delta", "-1"});
    assert(!invalidScrollMaxGestureDelta.ok());
    assert(invalidScrollMaxGestureDelta.error.find("--max-gesture-delta must be non-negative") != std::string::npos);

    auto invalidScrollSamplesLow = ComputerCpp::Cli::BuildScrollCommand({"scroll", "0", "0", "--samples", "0"});
    assert(!invalidScrollSamplesLow.ok());
    assert(invalidScrollSamplesLow.error.find("--samples must be between 1 and 6") != std::string::npos);

    auto invalidScrollSamplesHigh = ComputerCpp::Cli::BuildScrollCommand({"scroll", "0", "0", "--samples", "7"});
    assert(!invalidScrollSamplesHigh.ok());
    assert(invalidScrollSamplesHigh.error.find("--samples must be between 1 and 6") != std::string::npos);

    auto invalidObserve = ComputerCpp::Cli::BuildObserveCommand({"observe", "events", "many"});
    assert(!invalidObserve.ok());
    assert(invalidObserve.error.find("limit must be an integer") != std::string::npos);

    auto flagLikeObserveEventsLimit = ComputerCpp::Cli::BuildObserveCommand({"observe", "events", "--raw"});
    assert(!flagLikeObserveEventsLimit.ok());
    assert(flagLikeObserveEventsLimit.error.find("unknown observe events option") != std::string::npos);

    auto invalidObserveLimit = ComputerCpp::Cli::BuildObserveCommand({"observe", "events", "0"});
    assert(!invalidObserveLimit.ok());
    assert(invalidObserveLimit.error.find("limit must be positive") != std::string::npos);

    auto unknownObserveEventsOption = ComputerCpp::Cli::BuildObserveCommand({"observe", "events", "5", "--raw"});
    assert(!unknownObserveEventsOption.ok());
    assert(unknownObserveEventsOption.error.find("unknown observe events option") != std::string::npos);

    auto invalidObserveFramesLimit = ComputerCpp::Cli::BuildObserveCommand({"observe", "frames", "last", "0"});
    assert(!invalidObserveFramesLimit.ok());
    assert(invalidObserveFramesLimit.error.find("limit must be positive") != std::string::npos);

    auto flagLikeObserveFramesLimit = ComputerCpp::Cli::BuildObserveCommand({"observe", "frames", "last", "--raw"});
    assert(!flagLikeObserveFramesLimit.ok());
    assert(flagLikeObserveFramesLimit.error.find("unknown observe frames option") != std::string::npos);

    auto emptyObserveFramesEvent = ComputerCpp::Cli::BuildObserveCommand({"observe", "frames", ""});
    assert(!emptyObserveFramesEvent.ok());
    assert(emptyObserveFramesEvent.error.find("event must be non-empty") != std::string::npos);

    auto blankObserveFramesEvent = ComputerCpp::Cli::BuildObserveCommand({"observe", "frames", "   "});
    assert(!blankObserveFramesEvent.ok());
    assert(blankObserveFramesEvent.error.find("event must be non-empty") != std::string::npos);

    auto unknownObserveFramesOption = ComputerCpp::Cli::BuildObserveCommand({"observe", "frames", "last", "5", "--raw"});
    assert(!unknownObserveFramesOption.ok());
    assert(unknownObserveFramesOption.error.find("unknown observe frames option") != std::string::npos);

    auto invalidTarget = ComputerCpp::Cli::BuildTargetCommand({"target", "find", "text", "Done"});
    assert(!invalidTarget.ok());
    assert(invalidTarget.error.find("only supports role targets") != std::string::npos);

    auto invalidTargetLimit = ComputerCpp::Cli::BuildTargetCommand({"target", "find", "role", "button", "Save", "0"});
    assert(!invalidTargetLimit.ok());
    assert(invalidTargetLimit.error.find("limit must be positive") != std::string::npos);

    auto flagLikeTargetLimit = ComputerCpp::Cli::BuildTargetCommand({"target", "find", "role", "button", "--raw"});
    assert(!flagLikeTargetLimit.ok());
    assert(flagLikeTargetLimit.error.find("unknown target find option") != std::string::npos);

    auto emptyTargetRole = ComputerCpp::Cli::BuildTargetCommand({"target", "find", "role", ""});
    assert(!emptyTargetRole.ok());
    assert(emptyTargetRole.error.find("role must be non-empty") != std::string::npos);

    auto blankTargetRole = ComputerCpp::Cli::BuildTargetCommand({"target", "find", "role", "   "});
    assert(!blankTargetRole.ok());
    assert(blankTargetRole.error.find("role must be non-empty") != std::string::npos);

    auto emptyTargetRoleName = ComputerCpp::Cli::BuildTargetCommand({"target", "find", "role", "button", ""});
    assert(!emptyTargetRoleName.ok());
    assert(emptyTargetRoleName.error.find("name must be non-empty") != std::string::npos);

    auto blankTargetRoleName = ComputerCpp::Cli::BuildTargetCommand({"target", "find", "role", "button", "   "});
    assert(!blankTargetRoleName.ok());
    assert(blankTargetRoleName.error.find("name must be non-empty") != std::string::npos);

    auto emptyTargetResolve = ComputerCpp::Cli::BuildTargetCommand({"target", "resolve", ""});
    assert(!emptyTargetResolve.ok());
    assert(emptyTargetResolve.error.find("target must be non-empty") != std::string::npos);

    auto blankTargetResolve = ComputerCpp::Cli::BuildTargetCommand({"target", "resolve", "   "});
    assert(!blankTargetResolve.ok());
    assert(blankTargetResolve.error.find("target must be non-empty") != std::string::npos);

    auto emptyTargetExplain = ComputerCpp::Cli::BuildTargetCommand({"target", "explain", ""});
    assert(!emptyTargetExplain.ok());
    assert(emptyTargetExplain.error.find("target must be non-empty") != std::string::npos);

    auto blankTargetExplain = ComputerCpp::Cli::BuildTargetCommand({"target", "explain", "   "});
    assert(!blankTargetExplain.ok());
    assert(blankTargetExplain.error.find("target must be non-empty") != std::string::npos);

    auto unknownTargetResolveOption = ComputerCpp::Cli::BuildTargetCommand({"target", "resolve", "@e1", "--raw"});
    assert(!unknownTargetResolveOption.ok());
    assert(unknownTargetResolveOption.error.find("unknown target resolve option") != std::string::npos);

    auto unknownTargetExplainOption = ComputerCpp::Cli::BuildTargetCommand({"target", "explain", "@e1", "--raw"});
    assert(!unknownTargetExplainOption.ok());
    assert(unknownTargetExplainOption.error.find("unknown target explain option") != std::string::npos);

    auto invalidWaitText = ComputerCpp::Cli::BuildWaitCommand({"wait", "--text", "Ready"});
    assert(!invalidWaitText.ok());
    assert(invalidWaitText.error.find("wait --text was removed") != std::string::npos);

    auto removedObserve = ComputerCpp::Cli::BuildObserveCommand({"observe", "find", "Done"});
    assert(!removedObserve.ok());
    assert(removedObserve.error.find("unknown observe subcommand") != std::string::npos);

    auto invalidImageCommand = ComputerCpp::Cli::BuildImageCommand({"image", "split"});
    assert(!invalidImageCommand.ok());
    assert(invalidImageCommand.error.find("--output-dir dir") != std::string::npos);
    assert(invalidImageCommand.error.find("--prefix p") != std::string::npos);

    auto invalidImage = ComputerCpp::Cli::BuildImageCommand({
        "image",
        "split",
        "/tmp/source.png",
        "--overlap",
        "wide",
    });
    assert(!invalidImage.ok());
    assert(invalidImage.error.find("--overlap requires an integer") != std::string::npos);

    auto missingImageChunkHeight = ComputerCpp::Cli::BuildImageCommand({
        "image",
        "split",
        "/tmp/source.png",
        "--chunk-height",
    });
    assert(!missingImageChunkHeight.ok());
    assert(missingImageChunkHeight.error.find("--chunk-height requires a value") != std::string::npos);

    auto invalidImageChunkHeight = ComputerCpp::Cli::BuildImageCommand({
        "image",
        "split",
        "/tmp/source.png",
        "--chunk-height",
        "0",
    });
    assert(!invalidImageChunkHeight.ok());
    assert(invalidImageChunkHeight.error.find("--chunk-height must be positive") != std::string::npos);

    auto missingImageOverlap = ComputerCpp::Cli::BuildImageCommand({
        "image",
        "split",
        "/tmp/source.png",
        "--overlap",
    });
    assert(!missingImageOverlap.ok());
    assert(missingImageOverlap.error.find("--overlap requires a value") != std::string::npos);

    auto invalidImageOverlap = ComputerCpp::Cli::BuildImageCommand({
        "image",
        "split",
        "/tmp/source.png",
        "--overlap",
        "-1",
    });
    assert(!invalidImageOverlap.ok());
    assert(invalidImageOverlap.error.find("--overlap must be non-negative") != std::string::npos);

    auto invalidImageSelfOverlap = ComputerCpp::Cli::BuildImageCommand({
        "image",
        "split",
        "/tmp/source.png",
        "--chunk-height",
        "64",
        "--overlap",
        "64",
    });
    assert(!invalidImageSelfOverlap.ok());
    assert(invalidImageSelfOverlap.error.find("--overlap must be smaller than --chunk-height") != std::string::npos);

    auto unknownImageInfoOption = ComputerCpp::Cli::BuildImageCommand({"image", "info", "/tmp/source.png", "--verbose"});
    assert(!unknownImageInfoOption.ok());
    assert(unknownImageInfoOption.error.find("unknown image info option") != std::string::npos);

    auto emptyImageInfoPath = ComputerCpp::Cli::BuildImageCommand({"image", "info", ""});
    assert(!emptyImageInfoPath.ok());
    assert(emptyImageInfoPath.error.find("path must be non-empty") != std::string::npos);

    auto blankImageInfoPath = ComputerCpp::Cli::BuildImageCommand({"image", "info", "   "});
    assert(!blankImageInfoPath.ok());
    assert(blankImageInfoPath.error.find("path must be non-empty") != std::string::npos);

    auto emptyImageSplitPath = ComputerCpp::Cli::BuildImageCommand({"image", "split", ""});
    assert(!emptyImageSplitPath.ok());
    assert(emptyImageSplitPath.error.find("path must be non-empty") != std::string::npos);

    auto blankImageSplitPath = ComputerCpp::Cli::BuildImageCommand({"image", "split", "   "});
    assert(!blankImageSplitPath.ok());
    assert(blankImageSplitPath.error.find("path must be non-empty") != std::string::npos);

    auto unknownImageOption = ComputerCpp::Cli::BuildImageCommand({
        "image",
        "split",
        "/tmp/source.png",
        "--tile-width",
        "64",
    });
    assert(!unknownImageOption.ok());
    assert(unknownImageOption.error.find("unknown image split option") != std::string::npos);

    auto emptyImageOutDir = ComputerCpp::Cli::BuildImageCommand({
        "image",
        "split",
        "/tmp/source.png",
        "--out-dir",
        "",
    });
    assert(!emptyImageOutDir.ok());
    assert(emptyImageOutDir.error.find("--out-dir must be non-empty") != std::string::npos);

    auto blankImageOutDir = ComputerCpp::Cli::BuildImageCommand({
        "image",
        "split",
        "/tmp/source.png",
        "--out-dir",
        "   ",
    });
    assert(!blankImageOutDir.ok());
    assert(blankImageOutDir.error.find("--out-dir must be non-empty") != std::string::npos);

    auto missingImageOutputDir = ComputerCpp::Cli::BuildImageCommand({
        "image",
        "split",
        "/tmp/source.png",
        "--output-dir",
    });
    assert(!missingImageOutputDir.ok());
    assert(missingImageOutputDir.error.find("--output-dir requires a directory") != std::string::npos);

    auto blankImageOutputDir = ComputerCpp::Cli::BuildImageCommand({
        "image",
        "split",
        "/tmp/source.png",
        "--output-dir",
        "   ",
    });
    assert(!blankImageOutputDir.ok());
    assert(blankImageOutputDir.error.find("--output-dir must be non-empty") != std::string::npos);

    auto emptyImagePrefix = ComputerCpp::Cli::BuildImageCommand({
        "image",
        "split",
        "/tmp/source.png",
        "--prefix",
        "",
    });
    assert(!emptyImagePrefix.ok());
    assert(emptyImagePrefix.error.find("--prefix must be non-empty") != std::string::npos);

    auto blankImagePrefix = ComputerCpp::Cli::BuildImageCommand({
        "image",
        "split",
        "/tmp/source.png",
        "--prefix",
        "   ",
    });
    assert(!blankImagePrefix.ok());
    assert(blankImagePrefix.error.find("--prefix must be non-empty") != std::string::npos);

    auto invalidClick = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--count", "twice"});
    assert(!invalidClick.ok());
    assert(invalidClick.error.find("--count requires an integer") != std::string::npos);

    auto missingClickCount = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--count"});
    assert(!missingClickCount.ok());
    assert(missingClickCount.error.find("--count requires a value") != std::string::npos);

    auto invalidClickCountLow = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--count", "0"});
    assert(!invalidClickCountLow.ok());
    assert(invalidClickCountLow.error.find("--count must be between 1 and 5") != std::string::npos);

    auto invalidClickCountHigh = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--count", "6"});
    assert(!invalidClickCountHigh.ok());
    assert(invalidClickCountHigh.error.find("--count must be between 1 and 5") != std::string::npos);

    auto invalidClickDuration = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--duration-ms", "-1"});
    assert(!invalidClickDuration.ok());
    assert(invalidClickDuration.error.find("--duration-ms must be between 0 and 5000") != std::string::npos);

    auto missingClickDuration = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--duration-ms"});
    assert(!missingClickDuration.ok());
    assert(missingClickDuration.error.find("--duration-ms requires a value") != std::string::npos);

    auto invalidClickSteps = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--steps", "121"});
    assert(!invalidClickSteps.ok());
    assert(invalidClickSteps.error.find("--steps must be between 0 and 120") != std::string::npos);

    auto missingClickSteps = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--steps"});
    assert(!missingClickSteps.ok());
    assert(missingClickSteps.error.find("--steps requires a value") != std::string::npos);

    auto invalidClickHold = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--click-hold-ms", "-2"});
    assert(!invalidClickHold.ok());
    assert(invalidClickHold.error.find("--click-hold-ms must be between -1 and 5000") != std::string::npos);

    auto missingClickHold = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--click-hold-ms"});
    assert(!missingClickHold.ok());
    assert(missingClickHold.error.find("--click-hold-ms requires a value") != std::string::npos);

    auto invalidClickSettle = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--pre-click-settle-ms", "5001"});
    assert(!invalidClickSettle.ok());
    assert(invalidClickSettle.error.find("--pre-click-settle-ms must be between -1 and 5000") != std::string::npos);

    auto missingClickSettle = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--pre-click-settle-ms"});
    assert(!missingClickSettle.ok());
    assert(missingClickSettle.error.find("--pre-click-settle-ms requires a value") != std::string::npos);

    auto missingClickParkDuration = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--park-duration-ms"});
    assert(!missingClickParkDuration.ok());
    assert(missingClickParkDuration.error.find("--park-duration-ms requires a value") != std::string::npos);

    auto invalidClickParkDuration = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--park-duration-ms", "slow"});
    assert(!invalidClickParkDuration.ok());
    assert(invalidClickParkDuration.error.find("--park-duration-ms requires an integer") != std::string::npos);

    auto outOfRangeClickParkDuration = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--park-duration-ms", "1201"});
    assert(!outOfRangeClickParkDuration.ok());
    assert(outOfRangeClickParkDuration.error.find("--park-duration-ms must be between 0 and 1200") != std::string::npos);

    auto missingClickParkSteps = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--park-steps"});
    assert(!missingClickParkSteps.ok());
    assert(missingClickParkSteps.error.find("--park-steps requires a value") != std::string::npos);

    auto invalidClickParkSteps = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--park-steps", "many"});
    assert(!invalidClickParkSteps.ok());
    assert(invalidClickParkSteps.error.find("--park-steps requires an integer") != std::string::npos);

    auto outOfRangeClickParkSteps = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--park-steps", "81"});
    assert(!outOfRangeClickParkSteps.ok());
    assert(outOfRangeClickParkSteps.error.find("--park-steps must be between 0 and 80") != std::string::npos);

    auto missingClickParkX = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--park-x-fraction"});
    assert(!missingClickParkX.ok());
    assert(missingClickParkX.error.find("--park-x-fraction requires a value") != std::string::npos);

    auto invalidClickParkX = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--park-x-fraction", "right"});
    assert(!invalidClickParkX.ok());
    assert(invalidClickParkX.error.find("--park-x-fraction requires a number") != std::string::npos);

    auto outOfRangeClickParkX = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--park-x-fraction", "0.01"});
    assert(!outOfRangeClickParkX.ok());
    assert(outOfRangeClickParkX.error.find("--park-x-fraction must be between 0.05 and 0.95") != std::string::npos);

    auto missingClickParkY = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--park-y-fraction"});
    assert(!missingClickParkY.ok());
    assert(missingClickParkY.error.find("--park-y-fraction requires a value") != std::string::npos);

    auto invalidClickParkY = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--park-y-fraction", "middle"});
    assert(!invalidClickParkY.ok());
    assert(invalidClickParkY.error.find("--park-y-fraction requires a number") != std::string::npos);

    auto missingRectClickX = ComputerCpp::Cli::BuildClickCommand({"click", "rect:1,2,3,4", "--rect-click-x-fraction"});
    assert(!missingRectClickX.ok());
    assert(missingRectClickX.error.find("--rect-click-x-fraction requires a value") != std::string::npos);

    auto invalidRectClickX = ComputerCpp::Cli::BuildClickCommand({"click", "rect:1,2,3,4", "--rect-click-x-fraction", "right"});
    assert(!invalidRectClickX.ok());
    assert(invalidRectClickX.error.find("--rect-click-x-fraction requires a number") != std::string::npos);

    auto outOfRangeRectClickY = ComputerCpp::Cli::BuildClickCommand({"click", "rect:1,2,3,4", "--rect-click-y-fraction", "1.0"});
    assert(!outOfRangeRectClickY.ok());
    assert(outOfRangeRectClickY.error.find("--rect-click-y-fraction must be between 0.05 and 0.95") != std::string::npos);

    auto unknownPressOption = ComputerCpp::Cli::BuildPressCommand({"press", "Escape", "--hold"});
    assert(!unknownPressOption.ok());
    assert(unknownPressOption.error.find("unknown press option") != std::string::npos);

    auto missingPressHold = ComputerCpp::Cli::BuildPressCommand({"press", "Escape", "--hold-ms"});
    assert(!missingPressHold.ok());
    assert(missingPressHold.error.find("--hold-ms requires a value") != std::string::npos);

    auto invalidPressHold = ComputerCpp::Cli::BuildPressCommand({"press", "Escape", "--hold-ms", "0"});
    assert(!invalidPressHold.ok());
    assert(invalidPressHold.error.find("--hold-ms must be between 1 and 5000") != std::string::npos);

    auto nonIntegerPressHold = ComputerCpp::Cli::BuildPressCommand({"press", "Escape", "--hold-ms", "slow"});
    assert(!nonIntegerPressHold.ok());
    assert(nonIntegerPressHold.error.find("--hold-ms requires an integer") != std::string::npos);

    auto emptyPressChord = ComputerCpp::Cli::BuildPressCommand({"press", ""});
    assert(!emptyPressChord.ok());
    assert(emptyPressChord.error.find("non-empty key chord") != std::string::npos);

    auto blankPressChord = ComputerCpp::Cli::BuildPressCommand({"press", "   "});
    assert(!blankPressChord.ok());
    assert(blankPressChord.error.find("non-empty key chord") != std::string::npos);

    auto unknownTypeOption = ComputerCpp::Cli::BuildTypeCommand({"type", "hello", "--fast"});
    assert(!unknownTypeOption.ok());
    assert(unknownTypeOption.error.find("unknown type option") != std::string::npos);

    auto missingTypeTarget = ComputerCpp::Cli::BuildTypeCommand({"type", "hello", "--target"});
    assert(!missingTypeTarget.ok());
    assert(missingTypeTarget.error.find("--target requires a target") != std::string::npos);

    auto emptyTypeTarget = ComputerCpp::Cli::BuildTypeCommand({"type", "hello", "--target", ""});
    assert(!emptyTypeTarget.ok());
    assert(emptyTypeTarget.error.find("--target requires a non-empty target") != std::string::npos);

    auto blankTypeTarget = ComputerCpp::Cli::BuildTypeCommand({"type", "hello", "--target", "   "});
    assert(!blankTypeTarget.ok());
    assert(blankTypeTarget.error.find("--target requires a non-empty target") != std::string::npos);

    auto missingTypeHold = ComputerCpp::Cli::BuildTypeCommand({"type", "hello", "--hold-ms"});
    assert(!missingTypeHold.ok());
    assert(missingTypeHold.error.find("--hold-ms requires a value") != std::string::npos);

    auto invalidTypeHold = ComputerCpp::Cli::BuildTypeCommand({"type", "hello", "--hold-ms", "5001"});
    assert(!invalidTypeHold.ok());
    assert(invalidTypeHold.error.find("--hold-ms must be between 1 and 5000") != std::string::npos);

    auto nonIntegerTypeHold = ComputerCpp::Cli::BuildTypeCommand({"type", "hello", "--hold-ms", "slow"});
    assert(!nonIntegerTypeHold.ok());
    assert(nonIntegerTypeHold.error.find("--hold-ms requires an integer") != std::string::npos);

    auto emptyTypeText = ComputerCpp::Cli::BuildTypeCommand({"type", ""});
    assert(!emptyTypeText.ok());
    assert(emptyTypeText.error.find("text must be non-empty") != std::string::npos);

    auto whitespaceTypeText = ComputerCpp::Cli::BuildTypeCommand({"type", "   "});
    assert(whitespaceTypeText.ok());
    assert(whitespaceTypeText.params["text"] == "   ");

    auto unknownClipboardReadOption = ComputerCpp::Cli::BuildClipboardCommand({"clipboard", "read", "--raw"});
    assert(!unknownClipboardReadOption.ok());
    assert(unknownClipboardReadOption.error.find("unknown clipboard read option") != std::string::npos);

    auto unknownClipboardWriteOption = ComputerCpp::Cli::BuildClipboardCommand({"clipboard", "write", "hello", "--append"});
    assert(!unknownClipboardWriteOption.ok());
    assert(unknownClipboardWriteOption.error.find("unknown clipboard write option") != std::string::npos);

    auto unknownClipboardPasteOption = ComputerCpp::Cli::BuildClipboardCommand({"clipboard", "paste", "--raw"});
    assert(!unknownClipboardPasteOption.ok());
    assert(unknownClipboardPasteOption.error.find("unknown clipboard paste option") != std::string::npos);

    auto unknownClickOption = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--surprise"});
    assert(!unknownClickOption.ok());
    assert(unknownClickOption.error.find("unknown click option") != std::string::npos);

    auto emptyClickTarget = ComputerCpp::Cli::BuildClickCommand({"click", ""});
    assert(!emptyClickTarget.ok());
    assert(emptyClickTarget.error.find("target must be non-empty") != std::string::npos);

    auto blankClickTarget = ComputerCpp::Cli::BuildClickCommand({"click", "   "});
    assert(!blankClickTarget.ok());
    assert(blankClickTarget.error.find("target must be non-empty") != std::string::npos);

    auto missingClickButton = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--button"});
    assert(!missingClickButton.ok());
    assert(missingClickButton.error.find("--button requires a value") != std::string::npos);

    auto emptyClickButton = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--button", ""});
    assert(!emptyClickButton.ok());
    assert(emptyClickButton.error.find("--button requires a non-empty value") != std::string::npos);

    auto blankClickButton = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--button", "   "});
    assert(!blankClickButton.ok());
    assert(blankClickButton.error.find("--button requires a non-empty value") != std::string::npos);

    auto emptyClickMotion = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--motion", ""});
    assert(!emptyClickMotion.ok());
    assert(emptyClickMotion.error.find("--motion requires a non-empty value") != std::string::npos);

    auto blankClickMotion = ComputerCpp::Cli::BuildClickCommand({"click", "point:1,2", "--motion", "   "});
    assert(!blankClickMotion.ok());
    assert(blankClickMotion.error.find("--motion requires a non-empty value") != std::string::npos);

    auto unknownMouseOption = ComputerCpp::Cli::BuildMouseCommand({"mouse", "move", "1", "2", "--curve"});
    assert(!unknownMouseOption.ok());
    assert(unknownMouseOption.error.find("unknown mouse move option") != std::string::npos);

    auto invalidMouseMoveDuration = ComputerCpp::Cli::BuildMouseCommand({
        "mouse",
        "move",
        "1",
        "2",
        "--duration-ms",
        "-1",
    });
    assert(!invalidMouseMoveDuration.ok());
    assert(invalidMouseMoveDuration.error.find("--duration-ms must be non-negative") != std::string::npos);

    auto missingMouseDragButton = ComputerCpp::Cli::BuildMouseCommand({
        "mouse",
        "drag",
        "1",
        "2",
        "3",
        "4",
        "--button",
    });
    assert(!missingMouseDragButton.ok());
    assert(missingMouseDragButton.error.find("--button requires a value") != std::string::npos);

    auto emptyMouseDragButton = ComputerCpp::Cli::BuildMouseCommand({
        "mouse",
        "drag",
        "1",
        "2",
        "3",
        "4",
        "--button",
        "",
    });
    assert(!emptyMouseDragButton.ok());
    assert(emptyMouseDragButton.error.find("--button requires a non-empty value") != std::string::npos);

    auto blankMouseDragButton = ComputerCpp::Cli::BuildMouseCommand({
        "mouse",
        "drag",
        "1",
        "2",
        "3",
        "4",
        "--button",
        "   ",
    });
    assert(!blankMouseDragButton.ok());
    assert(blankMouseDragButton.error.find("--button requires a non-empty value") != std::string::npos);

    auto emptyMouseDownButton = ComputerCpp::Cli::BuildMouseCommand({"mouse", "down", ""});
    assert(!emptyMouseDownButton.ok());
    assert(emptyMouseDownButton.error.find("button must be non-empty") != std::string::npos);

    auto blankMouseDownButton = ComputerCpp::Cli::BuildMouseCommand({"mouse", "down", "   "});
    assert(!blankMouseDownButton.ok());
    assert(blankMouseDownButton.error.find("button must be non-empty") != std::string::npos);

    auto emptyMouseUpButton = ComputerCpp::Cli::BuildMouseCommand({"mouse", "up", ""});
    assert(!emptyMouseUpButton.ok());
    assert(emptyMouseUpButton.error.find("button must be non-empty") != std::string::npos);

    auto blankMouseUpButton = ComputerCpp::Cli::BuildMouseCommand({"mouse", "up", "   "});
    assert(!blankMouseUpButton.ok());
    assert(blankMouseUpButton.error.find("button must be non-empty") != std::string::npos);

    auto invalidMouseDragSteps = ComputerCpp::Cli::BuildMouseCommand({
        "mouse",
        "drag",
        "1",
        "2",
        "3",
        "4",
        "--steps",
        "-1",
    });
    assert(!invalidMouseDragSteps.ok());
    assert(invalidMouseDragSteps.error.find("--steps must be non-negative") != std::string::npos);

    auto missingMouseDragDuration = ComputerCpp::Cli::BuildMouseCommand({
        "mouse",
        "drag",
        "1",
        "2",
        "3",
        "4",
        "--duration-ms",
    });
    assert(!missingMouseDragDuration.ok());
    assert(missingMouseDragDuration.error.find("--duration-ms requires a value") != std::string::npos);

    auto missingMouseDragSteps = ComputerCpp::Cli::BuildMouseCommand({
        "mouse",
        "drag",
        "1",
        "2",
        "3",
        "4",
        "--steps",
    });
    assert(!missingMouseDragSteps.ok());
    assert(missingMouseDragSteps.error.find("--steps requires a value") != std::string::npos);

    auto invalidMouseDragCoordinate = ComputerCpp::Cli::BuildMouseCommand({
        "mouse",
        "drag",
        "1",
        "top",
        "3",
        "4",
    });
    assert(!invalidMouseDragCoordinate.ok());
    assert(invalidMouseDragCoordinate.error.find("numeric from-x from-y to-x to-y") != std::string::npos);

    auto nonFiniteMouseDragCoordinate = ComputerCpp::Cli::BuildMouseCommand({
        "mouse",
        "drag",
        "1",
        "2",
        "inf",
        "4",
    });
    assert(!nonFiniteMouseDragCoordinate.ok());
    assert(nonFiniteMouseDragCoordinate.error.find("numeric from-x from-y to-x to-y") != std::string::npos);

    auto missingWaitStable = ComputerCpp::Cli::BuildWaitCommand({"wait", "--stable-screen"});
    assert(!missingWaitStable.ok());
    assert(missingWaitStable.error.find("--stable-screen requires a value") != std::string::npos);

    auto missingWaitTimeout = ComputerCpp::Cli::BuildWaitCommand({"wait", "--timeout-ms"});
    assert(!missingWaitTimeout.ok());
    assert(missingWaitTimeout.error.find("--timeout-ms requires a value") != std::string::npos);

    auto missingWaitPoll = ComputerCpp::Cli::BuildWaitCommand({"wait", "--poll-ms"});
    assert(!missingWaitPoll.ok());
    assert(missingWaitPoll.error.find("--poll-ms requires a value") != std::string::npos);

    auto invalidWait = ComputerCpp::Cli::BuildWaitCommand({"wait", "--timeout-ms", "soon"});
    assert(!invalidWait.ok());
    assert(invalidWait.error.find("--timeout-ms requires an integer") != std::string::npos);

    auto invalidWaitTimeoutRange = ComputerCpp::Cli::BuildWaitCommand({"wait", "--timeout-ms", "0"});
    assert(!invalidWaitTimeoutRange.ok());
    assert(invalidWaitTimeoutRange.error.find("--timeout-ms must be between 1 and 120000") != std::string::npos);

    auto invalidWaitPollRange = ComputerCpp::Cli::BuildWaitCommand({"wait", "--poll-ms", "25"});
    assert(!invalidWaitPollRange.ok());
    assert(invalidWaitPollRange.error.find("--poll-ms must be between 50 and 5000") != std::string::npos);

    auto invalidWaitStableRange = ComputerCpp::Cli::BuildWaitCommand({"wait", "--stable-screen", "-1"});
    assert(!invalidWaitStableRange.ok());
    assert(invalidWaitStableRange.error.find("--stable-screen must be non-negative") != std::string::npos);

    auto missingWaitFrontmost = ComputerCpp::Cli::BuildWaitCommand({"wait", "--frontmost"});
    assert(!missingWaitFrontmost.ok());
    assert(missingWaitFrontmost.error.find("--frontmost requires an app name") != std::string::npos);

    auto emptyWaitFrontmost = ComputerCpp::Cli::BuildWaitCommand({"wait", "--frontmost", ""});
    assert(!emptyWaitFrontmost.ok());
    assert(emptyWaitFrontmost.error.find("--frontmost requires a non-empty app name") != std::string::npos);

    auto blankWaitFrontmost = ComputerCpp::Cli::BuildWaitCommand({"wait", "--frontmost", "   "});
    assert(!blankWaitFrontmost.ok());
    assert(blankWaitFrontmost.error.find("--frontmost requires a non-empty app name") != std::string::npos);

    auto unknownWaitOption = ComputerCpp::Cli::BuildWaitCommand({"wait", "--until-ready"});
    assert(!unknownWaitOption.ok());
    assert(unknownWaitOption.error.find("unknown wait option") != std::string::npos);

    auto invalidScrollDy = ComputerCpp::Cli::BuildScrollCommand({"scroll", "down"});
    assert(!invalidScrollDy.ok());
    assert(invalidScrollDy.error.find("dy must be an integer") != std::string::npos);

    auto invalidScrollReadDirection = ComputerCpp::Cli::BuildScrollCommand({"scroll", "read", "sideways"});
    assert(!invalidScrollReadDirection.ok());
    assert(invalidScrollReadDirection.error.find("direction must be down or up") != std::string::npos);

    auto missingScrollAt = ComputerCpp::Cli::BuildScrollCommand({"scroll", "10", "--at"});
    assert(!missingScrollAt.ok());
    assert(missingScrollAt.error.find("--at requires a target") != std::string::npos);

    auto emptyScrollAt = ComputerCpp::Cli::BuildScrollCommand({"scroll", "10", "--at", ""});
    assert(!emptyScrollAt.ok());
    assert(emptyScrollAt.error.find("--at requires a non-empty target") != std::string::npos);

    auto blankScrollAt = ComputerCpp::Cli::BuildScrollCommand({"scroll", "10", "--at", "   "});
    assert(!blankScrollAt.ok());
    assert(blankScrollAt.error.find("--at requires a non-empty target") != std::string::npos);

    auto emptyScrollFocus = ComputerCpp::Cli::BuildScrollCommand({"scroll", "10", "--focus", ""});
    assert(!emptyScrollFocus.ok());
    assert(emptyScrollFocus.error.find("--focus requires a non-empty app name") != std::string::npos);

    auto blankScrollFocus = ComputerCpp::Cli::BuildScrollCommand({"scroll", "10", "--focus", "   "});
    assert(!blankScrollFocus.ok());
    assert(blankScrollFocus.error.find("--focus requires a non-empty app name") != std::string::npos);

    auto unknownScrollOption = ComputerCpp::Cli::BuildScrollCommand({"scroll", "10", "--snap"});
    assert(!unknownScrollOption.ok());
    assert(unknownScrollOption.error.find("unknown scroll option") != std::string::npos);
}

void TestCliDurationParsing() {
    using ComputerCpp::Cli::ParseDurationOption;
    using ComputerCpp::Cli::ParseDurationMs;

    assert(ParseDurationMs("") == 0);
    assert(ParseDurationMs("250") == 250);
    assert(ParseDurationMs("250ms") == 250);
    assert(ParseDurationMs("1.5s") == 1500);
    assert(ParseDurationMs("2min") == 120000);
    assert(ParseDurationMs("3hrs") == 10800000);
    assert(ParseDurationOption("250", true).value() == 250);
    assert(!ParseDurationOption("250ms", true).has_value());
    assert(!ParseDurationOption("-1", true).has_value());

    bool rejectedHugeDuration = false;
    try {
        (void)ParseDurationMs("9223372036854775807h");
    } catch (const std::runtime_error&) {
        rejectedHugeDuration = true;
    }
    assert(rejectedHugeDuration);

    bool rejectedMissingAmount = false;
    try {
        (void)ParseDurationMs("ms");
    } catch (const std::runtime_error&) {
        rejectedMissingAmount = true;
    }
    assert(rejectedMissingAmount);

    bool rejectedUnknownUnit = false;
    try {
        (void)ParseDurationMs("10fortnights");
    } catch (const std::runtime_error&) {
        rejectedUnknownUnit = true;
    }
    assert(rejectedUnknownUnit);

    bool rejectedMalformedNumber = false;
    try {
        (void)ParseDurationMs("1.2.3s");
    } catch (const std::runtime_error&) {
        rejectedMalformedNumber = true;
    }
    assert(rejectedMalformedNumber);
}

void TestSessionChildCommandParsing() {
    ComputerCpp::Cli::CliOptions options;
    options.session = "unit-session";
    options.controlScope = "desktop:test";

    auto parsed = ComputerCpp::Cli::ParseSessionChildCommand(
        options,
        {
            "session",
            "exec",
            "--owner",
            "owner",
            "--purpose",
            "purpose",
            "--ttl",
            "2s",
            "--max",
            "5s",
            "--cleanup",
            "--",
            "/bin/echo",
            "ok",
        },
        "exec",
        true,
        600000);

    assert(parsed.command.has_value());
    assert(parsed.error.empty());
    assert(parsed.exitCode == 0);
    const auto& command = *parsed.command;
    assert(command.acquireParams["scope"] == "desktop:test");
    assert(command.acquireParams["daemonSession"] == "unit-session");
    assert(command.acquireParams["owner"] == "owner");
    assert(command.acquireParams["purpose"] == "purpose");
    assert(command.acquireParams["ttlMs"] == 2000);
    assert(command.acquireParams["maxRuntimeMs"] == 5000);
    assert(command.maxMs == 5000);
    assert(command.releaseAfter);
    assert(command.cleanupBeforeRelease);
    assert(command.command.size() == 2);
    assert(command.command[0] == "/bin/echo");
    assert(command.command[1] == "ok");

    auto invalid = ComputerCpp::Cli::ParseSessionChildCommand(
        options,
        {"session", "run", "--max", "later", "--", "/bin/echo"},
        "run",
        false,
        600000);
    assert(!invalid.command.has_value());
    assert(invalid.exitCode == 2);
    assert(invalid.error.find("--max requires a valid duration") != std::string::npos);

    auto oversizedMax = ComputerCpp::Cli::ParseSessionChildCommand(
        options,
        {"session", "run", "--max", "9223372036854775807h", "--", "/bin/echo"},
        "run",
        false,
        600000);
    assert(!oversizedMax.command.has_value());
    assert(oversizedMax.exitCode == 2);
    assert(oversizedMax.error.find("--max requires a valid duration") != std::string::npos);

    auto negativeMaxMs = ComputerCpp::Cli::ParseSessionChildCommand(
        options,
        {"session", "run", "--max-ms", "-1", "--", "/bin/echo"},
        "run",
        false,
        600000);
    assert(!negativeMaxMs.command.has_value());
    assert(negativeMaxMs.exitCode == 2);
    assert(negativeMaxMs.error.find("--max requires a valid duration") != std::string::npos);

    auto blankMax = ComputerCpp::Cli::ParseSessionChildCommand(
        options,
        {"session", "run", "--max", "", "--", "/bin/echo"},
        "run",
        false,
        600000);
    assert(!blankMax.command.has_value());
    assert(blankMax.exitCode == 2);
    assert(blankMax.error.find("--max requires a valid duration") != std::string::npos);

    auto whitespaceMax = ComputerCpp::Cli::ParseSessionChildCommand(
        options,
        {"session", "run", "--max", "   ", "--", "/bin/echo"},
        "run",
        false,
        600000);
    assert(!whitespaceMax.command.has_value());
    assert(whitespaceMax.exitCode == 2);
    assert(whitespaceMax.error.find("--max requires a valid duration") != std::string::npos);

    auto missingMax = ComputerCpp::Cli::ParseSessionChildCommand(
        options,
        {"session", "run", "--max"},
        "run",
        false,
        600000);
    assert(!missingMax.command.has_value());
    assert(missingMax.exitCode == 2);
    assert(missingMax.error.find("--max requires a value") != std::string::npos);

    auto missingTtl = ComputerCpp::Cli::ParseSessionChildCommand(
        options,
        {"session", "run", "--ttl"},
        "run",
        false,
        600000);
    assert(!missingTtl.command.has_value());
    assert(missingTtl.exitCode == 2);
    assert(missingTtl.error.find("--ttl requires a value") != std::string::npos);

    auto invalidTtl = ComputerCpp::Cli::ParseSessionChildCommand(
        options,
        {"session", "run", "--ttl", "later", "--", "/bin/echo"},
        "run",
        false,
        600000);
    assert(!invalidTtl.command.has_value());
    assert(invalidTtl.exitCode == 2);
    assert(invalidTtl.error.find("--ttl requires a valid duration") != std::string::npos);

    auto missingAcquireOwner = ComputerCpp::Cli::HandleSessionCommand(options, {"session", "acquire", "--owner"});
    assert(missingAcquireOwner == 2);

    auto invalidAcquireWait = ComputerCpp::Cli::HandleSessionCommand(options, {"session", "acquire", "--wait", "later"});
    assert(invalidAcquireWait == 2);

    auto missingRenewTtl = ComputerCpp::Cli::HandleSessionCommand(options, {"session", "renew", "token", "--ttl"});
    assert(missingRenewTtl == 2);

    auto missingReleaseActiveScope =
        ComputerCpp::Cli::HandleSessionCommand(options, {"session", "release-active", "--scope"});
    assert(missingReleaseActiveScope == 2);

    auto missingReleaseActiveOwner =
        ComputerCpp::Cli::HandleSessionCommand(options, {"session", "release-active", "--owner"});
    assert(missingReleaseActiveOwner == 2);

    auto missingReleaseActiveReason =
        ComputerCpp::Cli::HandleSessionCommand(options, {"session", "release-active", "--reason"});
    assert(missingReleaseActiveReason == 2);

    auto blankReleaseActiveScope =
        ComputerCpp::Cli::HandleSessionCommand(options, {"session", "release-active", "--scope", "   "});
    assert(blankReleaseActiveScope == 2);

    auto blankReleaseActiveOwner =
        ComputerCpp::Cli::HandleSessionCommand(options, {"session", "release-active", "--owner", ""});
    assert(blankReleaseActiveOwner == 2);

    auto blankReleaseActivePurpose =
        ComputerCpp::Cli::HandleSessionCommand(options, {"session", "release-active", "--purpose", "   "});
    assert(blankReleaseActivePurpose == 2);

    auto blankReleaseActiveReason =
        ComputerCpp::Cli::HandleSessionCommand(options, {"session", "release-active", "--reason", ""});
    assert(blankReleaseActiveReason == 2);

    auto missingStatusScope = ComputerCpp::Cli::HandleSessionCommand(options, {"session", "status", "--scope"});
    assert(missingStatusScope == 2);

    auto blankStatusScope =
        ComputerCpp::Cli::HandleSessionCommand(options, {"session", "status", "--scope", "   "});
    assert(blankStatusScope == 2);

    auto missingStatusToken = ComputerCpp::Cli::HandleSessionCommand(options, {"session", "status", "--token"});
    assert(missingStatusToken == 2);

    auto blankRenew = ComputerCpp::Cli::HandleSessionCommand(options, {"session", "renew", "   "});
    assert(blankRenew == 2);

    auto blankRelease = ComputerCpp::Cli::HandleSessionCommand(options, {"session", "release", "   "});
    assert(blankRelease == 2);

    auto blankStatus = ComputerCpp::Cli::HandleSessionCommand(options, {"session", "status", "--token", "   "});
    assert(blankStatus == 2);

    auto missingMetricsScope = ComputerCpp::Cli::HandleSessionCommand(options, {"session", "metrics", "--scope"});
    assert(missingMetricsScope == 2);

    auto blankMetricsScope =
        ComputerCpp::Cli::HandleSessionCommand(options, {"session", "metrics", "--scope", ""});
    assert(blankMetricsScope == 2);

    auto missingMetricsStale = ComputerCpp::Cli::HandleSessionCommand(options, {"session", "metrics", "--stale-after"});
    assert(missingMetricsStale == 2);

    auto missingMetricsEvents = ComputerCpp::Cli::HandleSessionCommand(options, {"session", "metrics", "--events"});
    assert(missingMetricsEvents == 2);

    auto missingEventsLimit = ComputerCpp::Cli::HandleSessionCommand(options, {"session", "events", "--limit"});
    assert(missingEventsLimit == 2);

    auto blankEventsScope =
        ComputerCpp::Cli::HandleSessionCommand(options, {"session", "events", "--scope", "   "});
    assert(blankEventsScope == 2);
}

void TestLuaRunCommandParsing() {
    ComputerCpp::Cli::CliOptions options;
    options.session = "unit-session";
    options.controlScope = "desktop:test";
    options.jsonOutput = true;

    ComputerCpp::LuaRunOptions runOptions;
    std::string error;
    bool ok = ComputerCpp::Cli::ParseLuaRunOptions(
        {
            "run",
            "--owner",
            "owner",
            "--purpose",
            "purpose",
            "--ttl",
            "2s",
            "--wait-ms",
            "250",
            "--max",
            "5s",
            "--var",
            "name=value",
            "--var=count=3",
            "tests/lua/dry-run.lua",
            "--",
            "arg1",
        },
        options,
        "/tmp/computer.cpp",
        runOptions,
        error);

    assert(ok);
    assert(error.empty());
    assert(runOptions.session == "unit-session");
    assert(runOptions.controlScope == "desktop:test");
    assert(runOptions.acquireControlSession);
    assert(runOptions.leaseOwner == "owner");
    assert(runOptions.leasePurpose == "purpose");
    assert(runOptions.leaseTtlMs == 2000);
    assert(runOptions.leaseWaitMs == 250);
    assert(runOptions.leaseMaxRuntimeMs == 5000);
    assert(runOptions.vars["name"] == "value");
    assert(runOptions.vars["count"] == "3");
    assert(runOptions.scriptPath == "tests/lua/dry-run.lua");
    assert(runOptions.scriptArgs.size() == 1);
    assert(runOptions.scriptArgs[0] == "arg1");

    auto child = ComputerCpp::Cli::BuildLuaRunChildCommand(options, runOptions);
    assert(child.size() >= 11);
    assert(child[0] == "/tmp/computer.cpp");
    assert(child[1] == "--session");
    assert(child[2] == "unit-session");
    assert(child[3] == "--control-scope");
    assert(child[4] == "desktop:test");
    assert(std::find(child.begin(), child.end(), "--json") != child.end());
    assert(std::find(child.begin(), child.end(), "--no-start") != child.end());
    assert(std::find(child.begin(), child.end(), "tests/lua/dry-run.lua") != child.end());

    ComputerCpp::LuaRunOptions invalid;
    error.clear();
    assert(!ComputerCpp::Cli::ParseLuaRunOptions({"run", "--var", "missing-equals", "script.lua"}, options, "/tmp/computer.cpp", invalid, error));
    assert(error.find("--var requires key=value") != std::string::npos);

    error.clear();
    ComputerCpp::LuaRunOptions missingOwner;
    assert(!ComputerCpp::Cli::ParseLuaRunOptions({"run", "--owner"}, options, "/tmp/computer.cpp", missingOwner, error));
    assert(error.find("--owner requires a value") != std::string::npos);

    error.clear();
    ComputerCpp::LuaRunOptions blankOwner;
    assert(!ComputerCpp::Cli::ParseLuaRunOptions({"run", "--owner", "   ", "script.lua"}, options, "/tmp/computer.cpp", blankOwner, error));
    assert(error.find("--owner requires a non-empty value") != std::string::npos);

    error.clear();
    ComputerCpp::LuaRunOptions missingTtl;
    assert(!ComputerCpp::Cli::ParseLuaRunOptions({"run", "--ttl"}, options, "/tmp/computer.cpp", missingTtl, error));
    assert(error.find("--ttl requires a value") != std::string::npos);

    error.clear();
    ComputerCpp::LuaRunOptions invalidTtlMsUnit;
    assert(!ComputerCpp::Cli::ParseLuaRunOptions(
        {"run", "--ttl-ms", "2s", "script.lua"},
        options,
        "/tmp/computer.cpp",
        invalidTtlMsUnit,
        error));
    assert(error.find("--ttl-ms requires a valid duration") != std::string::npos);

    error.clear();
    ComputerCpp::LuaRunOptions invalidMaxMsNegative;
    assert(!ComputerCpp::Cli::ParseLuaRunOptions(
        {"run", "--max-ms", "-1", "script.lua"},
        options,
        "/tmp/computer.cpp",
        invalidMaxMsNegative,
        error));
    assert(error.find("--max-ms requires a valid duration") != std::string::npos);

    error.clear();
    ComputerCpp::LuaRunOptions missingVar;
    assert(!ComputerCpp::Cli::ParseLuaRunOptions({"run", "--var"}, options, "/tmp/computer.cpp", missingVar, error));
    assert(error.find("--var requires a value") != std::string::npos);
}

void TestConfigCliCanonicalFile() {
    auto init = RunConfigCommand({"config", "init", "--force"});
    assert(init.exitCode == 0);

    auto provider = RunConfigCommand({
        "config",
        "set-provider",
        "router",
        "--type",
        "openrouter",
        "--no-api-key"
    });
    assert(provider.exitCode == 0);

    auto profile = RunConfigCommand({
        "config",
        "set-profile",
        "vision",
        "--provider",
        "router",
        "--model",
        "openai/gpt-4.1-mini",
        "--temperature",
        "0.2",
        "--top-p",
        "0.8",
        "--max-output-tokens",
        "512",
        "--timeout-ms",
        "120000",
        "--param",
        "presence_penalty=0.1",
        "--param",
        "parallel_tool_calls=true",
        "--openrouter-provider-json",
        "{\"allow_fallbacks\":false,\"order\":[\"openai\"]}",
        "--default"
    });
    assert(profile.exitCode == 0);

    std::string error;
    auto config = ComputerCpp::LoadAppConfig(&error);
    assert(error.empty());
    assert(config.defaultProfile == "vision");
    assert(config.providers["router"].type == "openrouter");
    assert(config.providers["router"].baseUrl == "https://openrouter.ai/api/v1");
    assert(config.profiles["vision"].provider == "router");
    assert(config.profiles["vision"].model == "openai/gpt-4.1-mini");
    assert(config.profiles["vision"].params["temperature"] == 0.2);
    assert(config.profiles["vision"].params["top_p"] == 0.8);
    assert(config.profiles["vision"].params["max_output_tokens"] == 512);
    assert(config.profiles["vision"].params["presence_penalty"] == 0.1);
    assert(config.profiles["vision"].params["parallel_tool_calls"] == true);
    assert(config.profiles["vision"].openRouterProvider["allow_fallbacks"] == false);

    auto keyProvider = RunConfigCommand({
        "config",
        "set-provider",
        "router",
        "--api-key",
        "or-test-secret"
    });
    assert(keyProvider.exitCode == 0);
    config = ComputerCpp::LoadAppConfig(&error);
    assert(error.empty());
    assert(config.providers["router"].apiKey == "or-test-secret");

    std::ifstream configFile(ComputerCpp::ConfigPath());
    std::string toml((std::istreambuf_iterator<char>(configFile)), std::istreambuf_iterator<char>());
    assert(toml.find("api_key = \"or-test-secret\"") != std::string::npos);

    auto show = RunConfigCommand({"config", "show"});
    assert(show.exitCode == 0);
    assert(show.stdoutText.find("or-test-secret") == std::string::npos);
    assert(show.stdoutText.find("<redacted>") != std::string::npos);

#if defined(__unix__) || defined(__APPLE__)
    struct stat st {};
    assert(::stat(ComputerCpp::ConfigPath().c_str(), &st) == 0);
    assert((st.st_mode & 0777) == 0600);
#endif

    auto test = RunConfigCommand({"config", "test"});
    assert(test.exitCode == 0);
    auto payload = nlohmann::json::parse(test.stdoutText);
    assert(payload["ok"] == true);
    assert(payload["data"]["profile"] == "vision");
    assert(payload["data"]["provider"] == "openrouter");
    assert(payload["data"]["model"] == "openai/gpt-4.1-mini");
    assert(payload["data"]["hasApiKey"] == true);
    assert(payload["data"]["defaultParams"]["max_output_tokens"] == 512);
    assert(payload["data"]["defaultParams"]["presence_penalty"] == 0.1);
    assert(payload["data"]["defaultParams"]["parallel_tool_calls"] == true);
    assert(payload["data"]["openRouterProvider"]["allow_fallbacks"] == false);

    auto path = RunConfigCommand({"config", "path"}, false);
    assert(path.exitCode == 0);
    assert(path.stdoutText.find(ComputerCpp::ConfigPath().string()) != std::string::npos);
}

void TestMicroAgentLuaDryRun() {
    ComputerCpp::LuaRunOptions options;
    options.scriptPath = RepoRoot() / "tests/lua/micro-agent-dry-run.lua";
    options.dryRun = true;
    options.jsonOutput = true;

    auto result = ComputerCpp::RunLuaScriptCapture(options);
    assert(result.exitCode == 0);
    assert(result.stderrText.find("agent     done") != std::string::npos);

    auto payload = nlohmann::json::parse(result.stdoutText);
    assert(payload["ok"] == true);
    const auto& data = payload["data"]["result"];
    assert(data["ok"] == true);
    assert(data["rows"] == 1);
    assert(data["request_count"] == 1);
    assert(data["first_tool_choice"] == "required");
    assert(data["first_tool_type"] == "function");
    assert(data["first_tool_name"] == "report_rows");
    assert(data["first_message_image_type"] == "image_path");
    assert(data["first_message_image_path"] == "/tmp/rows.png");
    assert(data["trace_count"] >= 3);
}

void TestMicroAgentStrictToolCallsLuaDryRun() {
    ComputerCpp::LuaRunOptions options;
    options.scriptPath = RepoRoot() / "tests/lua/micro-agent-strict-tool-calls-dry-run.lua";
    options.dryRun = true;
    options.jsonOutput = true;

    auto result = ComputerCpp::RunLuaScriptCapture(options);
    assert(result.exitCode == 0);

    auto payload = nlohmann::json::parse(result.stdoutText);
    assert(payload["ok"] == true);
    const auto& data = payload["data"]["result"];

    assert(data["missing"]["ok"] == true);
    assert(data["missing"]["reason"] == "recovered");
    assert(data["missing"]["request_count"] == 2);
    assert(data["missing"]["retry_message"].get<std::string>().find("exactly one available tool") != std::string::npos);
    assert(data["missing"]["preserved_assistant"] == "I should inspect the screen before continuing.");
    assert(data["missing"]["preserved_reasoning"] == "I can see the available tool and should use it next.");
    assert(data["missing"]["preserved_empty_tool_calls"] == false);
    assert(data["missing"]["trace_count"] >= 3);

    assert(data["reasoning_only"]["ok"] == true);
    assert(data["reasoning_only"]["count"] == 1);
    assert(data["reasoning_only"]["request_count"] == 2);
    assert(data["reasoning_only"]["first_preserve_thinking"] == true);
    assert(data["reasoning_only"]["second_preserve_thinking"] == true);
    assert(data["reasoning_only"]["second_request_reasoning"].get<std::string>().find("Ada Lovelace") != std::string::npos);
    assert(data["reasoning_only"]["second_request_retry_prompt"].get<std::string>().find("exactly one available tool") != std::string::npos);
    assert(data["reasoning_only"]["reported_author"] == "Ada Lovelace");
    assert(data["reasoning_only"]["trace_count"] >= 3);

    assert(data["pseudo"]["ok"] == false);
    assert(data["pseudo"]["code"] == "missing_tool_call");
    assert(data["pseudo"]["request_count"] == 3);

    assert(data["invalid_args"]["ok"] == false);
    assert(data["invalid_args"]["code"] == "invalid_input");
    assert(data["invalid_args"]["message"].get<std::string>().find("value is required") != std::string::npos);
    assert(data["invalid_args"]["request_count"] == 1);
    assert(data["invalid_args"]["reported"] == false);
}

void TestLuaDesktopToolPixelRects() {
    ComputerCpp::LuaRunOptions options;
    options.scriptPath = RepoRoot() / "tests/lua/desktop-app-tools-dry-run.lua";
    options.dryRun = true;
    options.jsonOutput = true;

    auto result = ComputerCpp::RunLuaScriptCapture(options);
    assert(result.exitCode == 0);

    auto payload = nlohmann::json::parse(result.stdoutText);
    assert(payload["ok"] == true);
    const auto& data = payload["data"]["result"];
    assert(data["click_ok"] == true);
    assert(data["rect_left"] == 150);
    assert(data["rect_top"] == 75);
    assert(data["rect_right"] == 200);
    assert(data["rect_bottom"] == 100);
    assert(data["rect_click_x"] == 0.5);
    assert(data["rect_click_y"] == 0.5);
    assert(data["model_grid_click_ok"] == true);
    assert(data["model_grid_rect_left"] == 150);
    assert(data["model_grid_rect_top"] == 75);
    assert(data["model_grid_rect_right"] == 200);
    assert(data["model_grid_rect_bottom"] == 100);
    assert(data["array_click_ok"] == true);
    assert(data["array_click_rect_left"] == 150);
    assert(data["array_click_rect_top"] == 75);
    assert(data["array_click_rect_right"] == 200);
    assert(data["array_click_rect_bottom"] == 100);
    assert(data["array_rect_center_x"] == 20);
    assert(data["rect_schema_left_min"] == 0);
    assert(data["rect_schema_left_max_missing"] == true);
    assert(data["text_trimmed"] == "hello");
    assert(data["text_normalized"] == "hello world");
    assert(data["text_present"] == true);
    assert(data["text_equals"] == true);
    assert(data["response_data_value"] == "ok");
    assert(data["rect_center_x"] == 20);
    assert(data["rect_center_y"] == 35);
    assert(data["rect_center_width"] == 20);
    assert(data["rect_center_height"] == 30);
    assert(data["rect_invalid_nil"] == true);
    assert(data["rect_valid"] == true);
    assert(data["rect_vertical_distance"] == 20);
    assert(data["rect_horizontal_gap"] == 15);
    assert(data["screenshot_state_space"] == "model_1000");
    assert(data["screenshot_state_width"] == 100);
    assert(data["inputless_done"] == true);
    assert(data["inputless_value"] == "done");
    assert(data["type_screenshot_ok"] == true);
    assert(data["type_screenshot_image"] == "/tmp/computer.cpp-dry-run-screenshot.png");
    assert(data["press_screenshot_ok"] == true);
    assert(data["press_screenshot_image"] == "/tmp/computer.cpp-dry-run-screenshot.png");
    assert(data["current_screenshot_ok"] == true);
    assert(data["current_screenshot_image"] == "/tmp/computer.cpp-dry-run-screenshot.png");
    assert(data["current_screenshot_ignored"] == true);
    assert(data["current_screenshot_reason"] == "already handled");
    assert(data["vision_progress_control"] == "desktop-control");
    assert(data["vision_progress_purpose"] == "unit vision");
    assert(data["vision_progress_launch"] == "launch-unit");
    assert(data["vision_progress_screenshot"] == "screenshot-unit");
    assert(data["vision_image"] == "/tmp/computer.cpp-dry-run-screenshot.png");
    assert(data["vision_width"] == 500);
    assert(data["vision_store_width"] == 500);
    assert(data["vision_data_value"] == "seen");
    assert(data["vision_data_space"] == "model_1000");
    assert(data["vision_content_image"] == "/tmp/computer.cpp-dry-run-screenshot.png");
}

} // namespace

namespace ComputerCpp::Tests {

void RunCliTests() {
    TestGlobalOptionParsing();
    TestCliCommandBuilders();
    TestCliDurationParsing();
    TestSessionChildCommandParsing();
    TestLuaRunCommandParsing();
    TestConfigCliCanonicalFile();
    TestMicroAgentLuaDryRun();
    TestMicroAgentStrictToolCallsLuaDryRun();
    TestLuaDesktopToolPixelRects();
}

}
