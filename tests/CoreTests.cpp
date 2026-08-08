#include "computer_cpp/AppConfig.h"
#include "computer_cpp/AppPaths.h"
#include "computer_cpp/Browser.h"
#include "computer_cpp/CommandRecording.h"
#include "computer_cpp/HumanInput.h"
#include "computer_cpp/Image.h"
#include "computer_cpp/LuaRunner.h"
#include "computer_cpp/NativeDeps.h"
#include "computer_cpp/Platform.h"
#include "computer_cpp/RefStore.h"
#include "computer_cpp/StringUtils.h"
#include "computer_cpp/Timeline.h"
#include "computer_cpp/TrayServerState.h"
#include "computer_cpp/Updater.h"

#include "LinuxPng.h"
#include "TestSupport.h"

#if defined(_WIN32)
#include "WindowsAppResolver.h"
#include "WindowsNativeInput.h"
#include "DaemonTextInput.h"
#endif
#include "UpdaterInternal.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sqlite3.h>
#include <stdexcept>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <crtdbg.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;
using ComputerCpp::Tests::MakeTempHome;

namespace {

void ExecSql(sqlite3* db, const std::string& sql) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = error ? error : "unknown sqlite error";
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}

void TestStringUtils() {
    assert(ComputerCpp::Trim("  hello \n") == "hello");
    assert(ComputerCpp::IsBlank(" \t\n"));
    assert(!ComputerCpp::IsBlank(" hello "));
    assert(ComputerCpp::Lowercase("HeLLo") == "hello");
    assert(ComputerCpp::ContainsCaseInsensitive("Hello World", "world"));
    auto keys = ComputerCpp::SplitKeyChord("Cmd+Shift+G");
    assert(keys.size() == 3);
    assert(keys[0] == "Cmd");
    assert(ComputerCpp::Join(keys, ",") == "Cmd,Shift,G");
}

void TestPlatformKeyResolution() {
    assert(ComputerCpp::Platform::ResolveKeycode("primary") >= 0);
    assert(ComputerCpp::Platform::ResolveKeycode("enter") >= 0);
    assert(ComputerCpp::Platform::ResolveKeycode("not-a-real-key") < 0);
}

#if defined(_WIN32)
class ScopedWindowsInputSender {
public:
    explicit ScopedWindowsInputSender(
        ComputerCpp::Platform::WindowsInput::SendInputFunction sender) {
        ComputerCpp::Platform::WindowsInput::SetSendInputFunctionForTesting(
            std::move(sender));
    }
    ~ScopedWindowsInputSender() {
        ComputerCpp::Platform::WindowsInput::ResetSendInputFunctionForTesting();
    }
};

void TestWindowsNativeInputDelivery() {
    using ComputerCpp::Platform::SendHotkey;
    using ComputerCpp::Platform::TypeText;

    {
        int calls = 0;
        ScopedWindowsInputSender sender([&](UINT, LPINPUT, int) {
            ++calls;
            return static_cast<UINT>(0);
        });
        assert(!SendHotkey({"primary", "l"}, 1));
        assert(calls == 1);

        const auto invalid = ComputerCpp::RunPressCommand({
            {"keys", nlohmann::json::array({"not-a-real-key"})},
            {"holdMs", 1},
        });
        assert(invalid["ok"] == false);
        assert(invalid["code"] == "invalid_key");

        const auto failed = ComputerCpp::RunPressCommand({
            {"keys", nlohmann::json::array({"primary", "l"})},
            {"holdMs", 1},
        });
        assert(failed["ok"] == false);
        assert(failed["code"] == "input_failed");
    }

    {
        std::vector<INPUT> events;
        int calls = 0;
        ScopedWindowsInputSender sender([&](UINT count, LPINPUT inputs, int) {
            events.insert(events.end(), inputs, inputs + count);
            ++calls;
            return calls == 2 ? static_cast<UINT>(0) : count;
        });
        assert(!SendHotkey({"primary", "l"}, 1));
        assert(events.size() == 3);
        assert(events[0].ki.wVk == VK_CONTROL);
        assert((events[0].ki.dwFlags & KEYEVENTF_KEYUP) == 0);
        assert(events[1].ki.wVk == 'L');
        assert((events[1].ki.dwFlags & KEYEVENTF_KEYUP) == 0);
        assert(events[2].ki.wVk == VK_CONTROL);
        assert((events[2].ki.dwFlags & KEYEVENTF_KEYUP) != 0);
    }

    {
        int calls = 0;
        ScopedWindowsInputSender sender([&](UINT count, LPINPUT, int) {
            ++calls;
            return calls == 2 ? static_cast<UINT>(0) : count;
        });
        assert(!TypeText("x", 1));
        assert(calls == 2);
    }

    {
        std::vector<INPUT> events;
        ScopedWindowsInputSender sender([&](UINT count, LPINPUT inputs, int) {
            events.insert(events.end(), inputs, inputs + count);
            return count;
        });
        assert(SendHotkey({"primary", "l"}, 1));
        assert(TypeText("x", 1));
        assert(events.size() == 6);
        assert((events[4].ki.dwFlags & KEYEVENTF_UNICODE) != 0);
        assert((events[4].ki.dwFlags & KEYEVENTF_KEYUP) == 0);
        assert((events[5].ki.dwFlags & KEYEVENTF_KEYUP) != 0);
    }
}

void TestWindowsAppCatalogMatching() {
    using ComputerCpp::Platform::WindowsApps::CatalogEntry;
    using ComputerCpp::Platform::WindowsApps::MatchCatalog;
    using ComputerCpp::Platform::WindowsApps::NormalizeLookupName;

    const std::vector<CatalogEntry> entries = {
        {
            "Calculator",
            "Microsoft.WindowsCalculator_8wekyb3d8bbwe!App",
            "",
            "shell:AppsFolder\\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App",
        },
        {
            "Visual Studio Code",
            "Microsoft.VisualStudioCode",
            "C:\\Program Files\\Microsoft VS Code\\Code.exe",
            "",
        },
        {"Calculator Preview", "Example.CalculatorPreview!App", "", ""},
    };

    assert(NormalizeLookupName(" Visual-Studio Code.exe ") ==
        "visualstudiocode");

    auto calculator = MatchCatalog(entries, "Calculator");
    assert(calculator.entry.has_value());
    assert(calculator.entry->appUserModelId ==
        "Microsoft.WindowsCalculator_8wekyb3d8bbwe!App");
    assert(!calculator.ambiguous);

    auto calculatorId = MatchCatalog(
        entries,
        "microsoft.windowscalculator_8wekyb3d8bbwe!app");
    assert(calculatorId.entry.has_value());
    assert(calculatorId.entry->displayName == "Calculator");

    auto code = MatchCatalog(entries, "code.exe");
    assert(code.entry.has_value());
    assert(code.entry->displayName == "Visual Studio Code");

    auto ambiguous = MatchCatalog(entries, "calc");
    assert(!ambiguous.entry.has_value());
    assert(ambiguous.ambiguous);
    assert(ambiguous.candidates.size() == 2);

    auto missing = MatchCatalog(entries, "Definitely Missing");
    assert(!missing.entry.has_value());
    assert(!missing.ambiguous);
    assert(missing.candidates.empty());
}
#endif

void TestBrowserRegistry() {
    assert(ComputerCpp::NormalizeBrowserId("Google Chrome") == "chrome");
    assert(ComputerCpp::NormalizeBrowserId("msedge.exe") == "edge");
    assert(ComputerCpp::NormalizeBrowserId("Brave Browser") == "brave");
    assert(ComputerCpp::NormalizeBrowserId("../../not-a-browser").empty());
    const auto catalog = ComputerCpp::BrowserCatalog();
    assert(catalog.size() == 4);
    assert(catalog.front().id == "chrome");
    assert(catalog.front().displayName == "Google Chrome");
    assert(catalog.front().recommended);
    assert(!catalog[1].recommended);
#if defined(__linux__)
    assert(catalog.front().windowQuery == "google-chrome");
#else
    assert(!catalog.front().windowQuery.empty());
#endif

    assert(ComputerCpp::ManagedBrowserDataDir("chrome", "default") ==
        ComputerCpp::AppDataDir() / "chrome-cdp");
    assert(ComputerCpp::ManagedBrowserDataDir("brave", "work") ==
        ComputerCpp::AppDataDir() / "browser-profiles" / "brave" / "work");
    assert(ComputerCpp::ManagedBrowserDataDir("../../bad", "work").empty());
    assert(ComputerCpp::ManagedBrowserDataDir("chrome", "../bad").empty());

    const fs::path privateDir = ComputerCpp::AppDataDir() / "browser-mode-test";
    ComputerCpp::PrepareManagedBrowserDataDir(privateDir);
    assert(fs::is_directory(privateDir));
#if !defined(_WIN32)
    const auto permissions = fs::status(privateDir).permissions();
    assert((permissions & fs::perms::owner_all) == fs::perms::owner_all);
    assert((permissions & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
#endif
}

void TestAppConfigServerRoundTrip() {
    std::string missingError;
    ComputerCpp::AppConfig missing = ComputerCpp::LoadAppConfig(&missingError);
    assert(missingError.empty());
    assert(missing.server.host == "127.0.0.1");
    assert(missing.server.port == 8787);
    assert(missing.server.apps.empty());
    assert(!missing.recording.enabled);
    assert(missing.recording.retentionDays == 14);
    assert(missing.browser.defaultBrowser == "chrome");
    assert(missing.browser.profile == "default");
    assert(missing.browser.userDataDir.empty());
    assert(missing.browser.proxyServer.empty());

    ComputerCpp::AppConfig defaults = ComputerCpp::DefaultAppConfig();
    assert(defaults.server.host == "127.0.0.1");
    assert(defaults.server.port == 8787);
    assert(defaults.server.authToken.empty());

    ComputerCpp::AppConfig config = defaults;
    config.server.host = "0.0.0.0";
    config.server.port = 8790;
    config.server.authToken = "test-token";
    config.server.allowedOrigins = {"https://mcp.example.com", "http://127.0.0.1:3000"};
    config.recording.enabled = true;
    config.browser.defaultBrowser = "edge";
    config.browser.profile = "work_1";
    config.browser.userDataDir =
        (ComputerCpp::AppDataDir() / "custom-browser-data").string();
    config.browser.proxyServer = "https://proxy.example:8001";

    ComputerCpp::ServerAppConfig linkedin;
    linkedin.name = "linkedin";
    linkedin.displayName = "LinkedIn Recruiter";
    linkedin.path = "/tmp/linkedin-recruiter.lua";
    config.server.apps[linkedin.name] = linkedin;

    std::string toml = ComputerCpp::AppConfigToToml(config);
    assert(toml.find("[server]") != std::string::npos);
    assert(toml.find("port = 8790") != std::string::npos);
    assert(toml.find("base_port") == std::string::npos);
    assert(toml.find("[server.apps.linkedin]") != std::string::npos);
    assert(toml.find("auth_token = \"test-token\"") != std::string::npos);
    assert(toml.find("[recording]") != std::string::npos);
    assert(toml.find("enabled = true") != std::string::npos);
    assert(toml.find("retention_days = 14") != std::string::npos);
    assert(toml.find("[browser]") != std::string::npos);
    assert(toml.find("default = \"edge\"") != std::string::npos);
    assert(toml.find("profile = \"work_1\"") != std::string::npos);
    assert(toml.find("user_data_dir = ") != std::string::npos);
    assert(toml.find("proxy = \"https://proxy.example:8001\"") !=
        std::string::npos);

    std::string error;
    assert(ComputerCpp::SaveAppConfig(config, &error));
    ComputerCpp::AppConfig loaded = ComputerCpp::LoadAppConfig(&error);
    assert(error.empty());
    assert(loaded.server.host == "0.0.0.0");
    assert(loaded.server.port == 8790);
    assert(loaded.server.authToken == "test-token");
    assert(loaded.server.allowedOrigins.size() == 2);
    assert(loaded.server.apps.contains("linkedin"));
    assert(loaded.server.apps["linkedin"].displayName == "LinkedIn Recruiter");
    assert(loaded.server.apps["linkedin"].path == "/tmp/linkedin-recruiter.lua");
    assert(loaded.recording.enabled);
    assert(loaded.recording.retentionDays == 14);
    assert(loaded.browser.defaultBrowser == "edge");
    assert(loaded.browser.profile == "work_1");
    assert(loaded.browser.userDataDir == config.browser.userDataDir);
    assert(loaded.browser.proxyServer == config.browser.proxyServer);

    auto redacted = ComputerCpp::AppConfigToJson(loaded);
    assert(redacted["server"]["authToken"] == "<redacted>");
    auto visible = ComputerCpp::AppConfigToJson(loaded, false);
    assert(visible["server"]["authToken"] == "test-token");
    assert(visible["server"]["port"] == 8790);
    assert(!visible["server"].contains("basePort"));
    assert(visible["browser"]["default"] == "edge");
    assert(visible["browser"]["profile"] == "work_1");
    assert(visible["browser"]["userDataDir"] == config.browser.userDataDir);
    assert(visible["browser"]["proxy"] == config.browser.proxyServer);
    assert(ComputerCpp::IsSupportedBrowserId("chrome"));
    assert(ComputerCpp::IsSupportedBrowserId("brave"));
    assert(!ComputerCpp::IsSupportedBrowserId("firefox"));
    assert(ComputerCpp::IsValidBrowserProfileName("qa.profile-1"));
    assert(!ComputerCpp::IsValidBrowserProfileName("../profile"));
    assert(!visible["server"]["apps"]["linkedin"].contains("port"));
    assert(visible["recording"]["enabled"] == true);
    assert(visible["recording"]["retentionDays"] == 14);
    assert(visible["recording"]["directory"] == ComputerCpp::RecordingDir().string());
    assert(ComputerCpp::RecordingDir().parent_path() ==
        ComputerCpp::DefaultArtifactDir().parent_path());

    loaded.recording.retentionDays = -1;
    assert(ComputerCpp::SaveAppConfig(loaded, &error));
    ComputerCpp::AppConfig keepForever = ComputerCpp::LoadAppConfig(&error);
    assert(error.empty());
    assert(keepForever.recording.retentionDays == -1);

    ComputerCpp::AppConfig tokenConfig = ComputerCpp::DefaultAppConfig();
    assert(ComputerCpp::EnsureServerAuthToken(tokenConfig));
    assert(tokenConfig.server.authToken.size() >= 32);
    std::string generated = tokenConfig.server.authToken;
    assert(!ComputerCpp::EnsureServerAuthToken(tokenConfig));
    assert(tokenConfig.server.authToken == generated);

    loaded.recording.enabled = false;
    assert(ComputerCpp::SaveAppConfig(loaded, &error));
}

void TestServerAppNameValidation() {
    assert(ComputerCpp::IsValidServerAppName("notes"));
    assert(ComputerCpp::IsValidServerAppName("Notes.v2_test-app"));
    assert(ComputerCpp::IsValidServerAppName("1-app"));
    assert(!ComputerCpp::IsValidServerAppName(""));
    assert(!ComputerCpp::IsValidServerAppName("-notes"));
    assert(!ComputerCpp::IsValidServerAppName("notes app"));
    assert(!ComputerCpp::IsValidServerAppName("notes/app"));
}

void TestServerPortConfigMigration() {
    std::string error;
    const ComputerCpp::AppConfig original = ComputerCpp::LoadAppConfig(&error);
    assert(error.empty());

    {
        std::ofstream config(ComputerCpp::ConfigPath(), std::ios::trunc);
        config << "version = 1\n\n"
               << "[server]\n"
               << "host = \"127.0.0.1\"\n"
               << "base_port = 8891\n\n"
               << "[server.apps.legacy]\n"
               << "path = \"/tmp/legacy.lua\"\n"
               << "port = 8899\n";
    }
    std::vector<std::string> warnings;
    auto legacy = ComputerCpp::LoadAppConfig(&error, &warnings);
    assert(error.empty());
    assert(legacy.server.port == 8891);
    assert(legacy.server.apps.contains("legacy"));
    assert(warnings.size() == 1);
    assert(warnings.front().find("legacy per-app port 8899") !=
        std::string::npos);
    std::string migrated = ComputerCpp::AppConfigToToml(legacy);
    assert(migrated.find("port = 8891") != std::string::npos);
    assert(migrated.find("base_port") == std::string::npos);
    assert(migrated.find("port = 8899") == std::string::npos);

    {
        std::ofstream config(ComputerCpp::ConfigPath(), std::ios::trunc);
        config << "version = 1\n\n"
               << "[server]\n"
               << "port = 8892\n"
               << "base_port = 8891\n";
    }
    auto preferred = ComputerCpp::LoadAppConfig(&error, &warnings);
    assert(error.empty());
    assert(preferred.server.port == 8892);
    assert(warnings.empty());
    assert(ComputerCpp::SaveAppConfig(original, &error));
}

void TestBrowserConfigValidation() {
    std::string error;
    const ComputerCpp::AppConfig original = ComputerCpp::LoadAppConfig(&error);
    assert(error.empty());
    {
        std::ofstream config(ComputerCpp::ConfigPath(), std::ios::trunc);
        config << "version = 1\n\n"
               << "[browser]\n"
               << "default = \"firefox\"\n"
               << "profile = \"default\"\n";
    }
    ComputerCpp::LoadAppConfig(&error);
    assert(error.find("browser.default") != std::string::npos);
    {
        std::ofstream config(ComputerCpp::ConfigPath(), std::ios::trunc);
        config << "version = 1\n\n"
               << "[browser]\n"
               << "default = \"chrome\"\n"
               << "profile = \"../personal\"\n";
    }
    ComputerCpp::LoadAppConfig(&error);
    assert(error.find("browser.profile") != std::string::npos);
    {
        std::ofstream config(ComputerCpp::ConfigPath(), std::ios::trunc);
        config << "version = 1\n\n"
               << "[browser]\n"
               << "default = \"chrome\"\n"
               << "profile = \"default\"\n"
               << "user_data_dir = \"relative/path\"\n";
    }
    ComputerCpp::LoadAppConfig(&error);
    assert(error.find("browser.user_data_dir") != std::string::npos);
    {
        std::ofstream config(ComputerCpp::ConfigPath(), std::ios::trunc);
        config << "version = 1\n\n"
               << "[browser]\n"
               << "default = \"chrome\"\n"
               << "profile = \"default\"\n"
               << "proxy = \"https://proxy.example:80 invalid\"\n";
    }
    ComputerCpp::LoadAppConfig(&error);
    assert(error.find("browser.proxy") != std::string::npos);
    {
        std::ofstream config(ComputerCpp::ConfigPath(), std::ios::trunc);
        config << "version = 1\n";
    }
    const auto legacy = ComputerCpp::LoadAppConfig(&error);
    assert(error.empty());
    assert(legacy.browser.defaultBrowser == "chrome");
    assert(legacy.browser.profile == "default");
    assert(legacy.browser.userDataDir.empty());
    assert(legacy.browser.proxyServer.empty());
    assert(ComputerCpp::SaveAppConfig(original, &error));
}

class FakeScreenRecordingSession final : public ComputerCpp::Platform::ScreenRecordingSession {
public:
    explicit FakeScreenRecordingSession(bool stopSucceeds)
        : stopSucceeds_(stopSucceeds) {}

    bool Stop(int, std::string* error) override {
        if (!stopSucceeds_ && error) {
            *error = "fake finalization failure";
        }
        return stopSucceeds_;
    }

private:
    bool stopSucceeds_;
};

ComputerCpp::ScreenRecordingFactory FakeRecordingFactory(bool startSucceeds, bool stopSucceeds) {
    return [startSucceeds, stopSucceeds](
        const ComputerCpp::Platform::ScreenRecordingOptions& options,
        int startTimeoutMs,
        std::string* error
    ) -> std::unique_ptr<ComputerCpp::Platform::ScreenRecordingSession> {
        assert(startTimeoutMs == 5000);
        assert(options.framesPerSecond == 15);
        assert(options.maxDimension == 1920);
        assert(options.includeCursor);
        if (!startSucceeds) {
            if (error) {
                *error = "fake startup failure";
            }
            return nullptr;
        }
        fs::create_directories(options.outputPath.parent_path());
        std::ofstream file(options.outputPath, std::ios::binary);
        file << "fake-mp4";
        file.close();
        return std::make_unique<FakeScreenRecordingSession>(stopSucceeds);
    };
}

void TestCommandRecordingLifecycle() {
    ComputerCpp::ResetRecordingCleanupForTesting();
    ComputerCpp::CommandRecordingOptions disabledOptions;
    disabledOptions.enabled = false;
    disabledOptions.factory = [](const auto&, int, std::string*) {
        assert(false && "disabled recording must not start a backend");
        return std::unique_ptr<ComputerCpp::Platform::ScreenRecordingSession>();
    };
    ComputerCpp::CommandRecording disabled(std::move(disabledOptions));
    assert(!disabled.enabled());
    assert(disabled.metadata().empty());

    const fs::path cleanupDir = ComputerCpp::RecordingDir() / "cleanup-test";
    fs::create_directories(cleanupDir);
    const fs::path expiredVideo = cleanupDir / "expired.mp4";
    const fs::path expiredSidecar = cleanupDir / "expired.json";
    const fs::path abandonedPartial = cleanupDir / "abandoned.partial.mp4";
    const fs::path activePartial = cleanupDir / "active.partial.mp4";
    for (const auto& path : {expiredVideo, expiredSidecar, abandonedPartial, activePartial}) {
        std::ofstream file(path);
        file << "old";
    }
    fs::last_write_time(
        expiredVideo,
        fs::file_time_type::clock::now() - std::chrono::hours(24 * 15));
    fs::last_write_time(
        expiredSidecar,
        fs::file_time_type::clock::now() - std::chrono::hours(24 * 15));
    fs::last_write_time(
        abandonedPartial,
        fs::file_time_type::clock::now() - std::chrono::hours(25));
    fs::last_write_time(
        activePartial,
        fs::file_time_type::clock::now() - std::chrono::hours(25));
    const fs::path liveMarker =
        ComputerCpp::RecordingDir() / ".active" / "rec_cleanup_live.json";
    fs::create_directories(liveMarker.parent_path());
    {
        std::ofstream file(liveMarker);
#if defined(_WIN32)
        const long long processId = static_cast<long long>(GetCurrentProcessId());
#else
        const long long processId = static_cast<long long>(getpid());
#endif
        file << json({
            {"recordingId", "rec_cleanup_live"},
            {"pid", processId},
            {"partialPath", activePartial.string()},
        }).dump(2);
    }

    ComputerCpp::CommandRecordingOptions successOptions;
    successOptions.enabled = true;
    successOptions.appId = "test app";
    successOptions.command = "open private record";
    successOptions.surface = "cli";
    successOptions.recordingId = "rec_test_success";
    successOptions.factory = FakeRecordingFactory(true, true);
    ComputerCpp::CommandRecording success(std::move(successOptions));
    assert(!fs::exists(expiredVideo));
    assert(!fs::exists(expiredSidecar));
    assert(!fs::exists(abandonedPartial));
    assert(fs::exists(activePartial));
    fs::remove(liveMarker);
    fs::remove(activePartial);
    assert(success.metadata()["status"] == "recording");
    assert(success.metadata()["recordingId"] == "rec_test_success");
    success.Finish("succeeded");
    const json successMetadata = success.metadata();
    assert(successMetadata["status"] == "recorded");
    assert(successMetadata["commandStatus"] == "succeeded");
    assert(successMetadata["appId"] == "test app");
    assert(successMetadata["command"] == "open private record");
    assert(successMetadata["surface"] == "cli");
    assert(successMetadata["error"].is_null());
    const fs::path finalPath = successMetadata["path"].get<std::string>();
    assert(fs::exists(finalPath));
    assert(finalPath.extension() == ".mp4");
    assert(finalPath.string().find("test-app") != std::string::npos);
    fs::path sidecarPath = finalPath;
    sidecarPath.replace_extension(".json");
    assert(fs::exists(sidecarPath));
    {
        std::ifstream sidecarFile(sidecarPath);
        json sidecar = json::parse(sidecarFile);
        assert(sidecar["status"] == "recorded");
        assert(!sidecar.contains("startedAtMs"));
        assert(!sidecar.contains("arguments"));
    }

#if defined(__unix__) || defined(__APPLE__)
    const auto permissions = fs::status(ComputerCpp::RecordingDir()).permissions();
    assert((permissions & fs::perms::group_all) == fs::perms::none);
    assert((permissions & fs::perms::others_all) == fs::perms::none);
#endif

    ComputerCpp::CommandRecordingOptions startFailureOptions;
    startFailureOptions.enabled = true;
    startFailureOptions.appId = "test";
    startFailureOptions.command = "failure";
    startFailureOptions.surface = "mcp";
    startFailureOptions.factory = FakeRecordingFactory(false, true);
    ComputerCpp::CommandRecording startFailure(std::move(startFailureOptions));
    assert(startFailure.metadata()["status"] == "failed");
    assert(startFailure.metadata()["error"] == "fake startup failure");

    ComputerCpp::CommandRecordingOptions stopFailureOptions;
    stopFailureOptions.enabled = true;
    stopFailureOptions.appId = "test";
    stopFailureOptions.command = "failure";
    stopFailureOptions.surface = "http";
    stopFailureOptions.factory = FakeRecordingFactory(true, false);
    ComputerCpp::CommandRecording stopFailure(std::move(stopFailureOptions));
    stopFailure.Finish("failed");
    assert(stopFailure.metadata()["status"] == "failed");
    assert(stopFailure.metadata()["commandStatus"] == "failed");
    assert(stopFailure.metadata()["error"] == "fake finalization failure");

    ComputerCpp::CommandRecordingOptions commandErrorOptions;
    commandErrorOptions.enabled = true;
    commandErrorOptions.appId = "test";
    commandErrorOptions.command = "command-error";
    commandErrorOptions.surface = "cli";
    commandErrorOptions.factory = FakeRecordingFactory(true, true);
    ComputerCpp::CommandRecording commandError(std::move(commandErrorOptions));
    commandError.Finish("failed");
    assert(commandError.metadata()["status"] == "recorded");
    assert(commandError.metadata()["commandStatus"] == "failed");

    ComputerCpp::CommandRecordingOptions cancelledOptions;
    cancelledOptions.enabled = true;
    cancelledOptions.appId = "test";
    cancelledOptions.command = "cancelled";
    cancelledOptions.surface = "http";
    cancelledOptions.factory = FakeRecordingFactory(true, true);
    ComputerCpp::CommandRecording cancelled(std::move(cancelledOptions));
    cancelled.Finish("cancelled");
    assert(cancelled.metadata()["status"] == "recorded");
    assert(cancelled.metadata()["commandStatus"] == "cancelled");

    const fs::path interruptedMirror =
        ComputerCpp::RecordingDir() / "interrupted-mirror.json";
    {
        ComputerCpp::CommandRecordingOptions interruptedOptions;
        interruptedOptions.enabled = true;
        interruptedOptions.appId = "test";
        interruptedOptions.command = "interrupted";
        interruptedOptions.surface = "async";
        interruptedOptions.statusMirrorPath = interruptedMirror;
        interruptedOptions.factory = FakeRecordingFactory(true, true);
        ComputerCpp::CommandRecording interrupted(std::move(interruptedOptions));
        assert(interrupted.metadata()["status"] == "recording");
    }
    {
        std::ifstream file(interruptedMirror);
        json interrupted = json::parse(file);
        assert(interrupted["status"] == "interrupted");
        assert(interrupted["commandStatus"] == "interrupted");
    }

    const fs::path staleSidecar = ComputerCpp::RecordingDir() / "stale.json";
    const fs::path staleMarker = ComputerCpp::RecordingDir() / ".active" / "stale.json";
    fs::create_directories(staleMarker.parent_path());
    {
        std::ofstream file(staleSidecar);
        file << json({
            {"recordingId", "rec_stale"},
            {"status", "recording"},
            {"startedAt", "2026-01-01T00:00:00Z"},
            {"finishedAt", nullptr},
            {"durationMs", nullptr},
            {"error", nullptr},
            {"commandStatus", "running"},
        }).dump(2);
    }
    {
        std::ofstream file(staleMarker);
        file << json({
            {"recordingId", "rec_stale"},
            {"pid", 999999999},
            {"sidecarPath", staleSidecar.string()},
        }).dump(2);
    }
    ComputerCpp::ResetRecordingCleanupForTesting();
    ComputerCpp::CleanupExpiredRecordings(14);
    assert(!fs::exists(staleMarker));
    {
        std::ifstream file(staleSidecar);
        json recovered = json::parse(file);
        assert(recovered["status"] == "interrupted");
        assert(recovered["commandStatus"] == "interrupted");
    }

    const fs::path reusedPidSidecar =
        ComputerCpp::RecordingDir() / "reused-pid.json";
    const fs::path reusedPidMarker =
        ComputerCpp::RecordingDir() / ".active" / "reused-pid.json";
    {
        std::ofstream file(reusedPidSidecar);
        file << json({
            {"recordingId", "rec_reused_pid"},
            {"status", "recording"},
            {"startedAt", "2026-01-01T00:00:00Z"},
            {"finishedAt", nullptr},
            {"durationMs", nullptr},
            {"error", nullptr},
            {"commandStatus", "running"},
        }).dump(2);
    }
    {
#if defined(_WIN32)
        const long long processId = static_cast<long long>(GetCurrentProcessId());
#else
        const long long processId = static_cast<long long>(getpid());
#endif
        const int64_t oldStart = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch() -
            std::chrono::hours(25)).count();
        std::ofstream file(reusedPidMarker);
        file << json({
            {"recordingId", "rec_reused_pid"},
            {"pid", processId},
            {"sidecarPath", reusedPidSidecar.string()},
            {"startedAtMs", oldStart},
        }).dump(2);
    }
    assert(ComputerCpp::ActiveRecordingCount() == 0);
    ComputerCpp::ResetRecordingCleanupForTesting();
    ComputerCpp::CleanupExpiredRecordings(14);
    assert(!fs::exists(reusedPidMarker));
    {
        std::ifstream file(reusedPidSidecar);
        const json recovered = json::parse(file);
        assert(recovered["status"] == "interrupted");
    }
}

void TestNativeCommandRecordingSmoke() {
    const char* enabled = std::getenv("COMPUTER_CPP_NATIVE_RECORDING_SMOKE");
    const std::string mode = enabled ? enabled : "";
    if (mode != "1" && mode != "required") {
        std::cout << "[skip] native recording smoke "
                     "(set COMPUTER_CPP_NATIVE_RECORDING_SMOKE=1 or required)"
                  << std::endl;
        return;
    }
    ComputerCpp::CommandRecordingOptions options;
    options.enabled = true;
    options.appId = "native-smoke";
    options.command = "record";
    options.surface = "test";
    ComputerCpp::CommandRecording recording(std::move(options));
    if (recording.metadata().value("status", "") != "recording") {
        std::cout << "[native-recording-unavailable] " << recording.metadata().dump()
                  << std::endl;
        assert(mode != "required");
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    recording.Finish("succeeded");
    assert(recording.metadata()["status"] == "recorded");
    const fs::path path = recording.metadata()["path"].get<std::string>();
    assert(fs::exists(path));
    assert(fs::file_size(path) > 0);
    std::cout << "[native-recording] " << path.string() << std::endl;

#if defined(_WIN32)
    const fs::path unicodeDirectory =
        ComputerCpp::RecordingDir() / fs::path(L"unicode-\u5F55\u5236");
    ComputerCpp::EnsureDirectory(unicodeDirectory);
    const fs::path unicodePath = unicodeDirectory / L"native-recording.mp4";
    ComputerCpp::Platform::ScreenRecordingOptions unicodeOptions;
    unicodeOptions.outputPath = unicodePath;
    std::string unicodeError;
    auto unicodeRecording = ComputerCpp::Platform::StartScreenRecording(
        unicodeOptions,
        5000,
        &unicodeError);
    if (!unicodeRecording) {
        std::cout << "[native-recording-unicode-path-unavailable] "
                  << unicodeError << std::endl;
        assert(mode != "required");
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    assert(unicodeRecording->Stop(10000, &unicodeError));
    assert(fs::exists(unicodePath));
    assert(fs::file_size(unicodePath) > 0);
    std::cout << "[native-recording-unicode-path] "
              << fs::file_size(unicodePath) << " bytes" << std::endl;
#endif
}

void TestTrayServerState() {
    fs::path path = ComputerCpp::SessionDir("unit") / "tray-server-state.json";
    ComputerCpp::TrayAppServerState state;
    state.pid = 12345;
    state.host = "127.0.0.1";
    state.port = 8787;
    state.url = "http://127.0.0.1:8787";
    state.appPath = "/tmp/app.lua";
    state.appId = "app-id";
    state.configName = "configured-app";
    state.displayName = "Test App";
    state.startedAt = "2026-06-22T00:00:00Z";

    std::string error;
    assert(ComputerCpp::SaveTrayAppServerState(state, path, &error));
    auto loaded = ComputerCpp::LoadTrayAppServerState(path, &error);
    assert(loaded.has_value());
    assert(loaded->version == 1);
    assert(!loaded->configured);
    assert(loaded->pid == state.pid);
    assert(loaded->host == state.host);
    assert(loaded->port == state.port);
    assert(loaded->url == state.url);
    assert(loaded->appPath == state.appPath);
    assert(loaded->appId == state.appId);
    assert(loaded->configName == state.configName);
    assert(loaded->displayName == state.displayName);
    assert(loaded->startedAt == state.startedAt);

    ComputerCpp::TrayAppServerState configuredState;
    configuredState.version = 2;
    configuredState.configured = true;
    configuredState.pid = 54321;
    configuredState.host = "127.0.0.1";
    configuredState.port = 8790;
    configuredState.url = "http://127.0.0.1:8790";
    configuredState.startedAt = "2026-07-30T00:00:00Z";
    configuredState.internalControlToken = "relay-only-token";
    assert(ComputerCpp::SaveTrayAppServerState(configuredState, path, &error));
    auto configuredLoaded =
        ComputerCpp::LoadTrayAppServerState(path, &error);
    assert(configuredLoaded.has_value());
    assert(configuredLoaded->version == 2);
    assert(configuredLoaded->configured);
    assert(configuredLoaded->appPath.empty());
    assert(configuredLoaded->internalControlToken ==
        "relay-only-token");
    assert(ComputerCpp::RemoveTrayAppServerState(path, &error));

    assert(ComputerCpp::SaveTrayAppServerState(state, path, &error));

    assert(ComputerCpp::RemoveTrayAppServerStateForPid(path, 999, &error));
    assert(fs::exists(path));
    assert(ComputerCpp::RemoveTrayAppServerStateForPid(path, state.pid, &error));
    assert(!fs::exists(path));

    {
        std::ofstream out(path);
        out << "{\"pid\": -1}";
    }
    auto invalid = ComputerCpp::LoadTrayAppServerState(path, &error);
    assert(!invalid.has_value());
    assert(ComputerCpp::RemoveTrayAppServerState(path, &error));
    assert(!ComputerCpp::IsProcessAlive(-1));

    ComputerCpp::TrayAppServerState first = state;
    first.pid = 111;
    first.configName = "first/app";
    ComputerCpp::TrayAppServerState second = state;
    second.pid = 222;
    second.configName = "../second app";
    const fs::path firstPath = ComputerCpp::TrayAppServerStatePath(first.configName);
    const fs::path secondPath = ComputerCpp::TrayAppServerStatePath(second.configName);
    assert(firstPath.parent_path() == ComputerCpp::TrayAppServerStateDirectory());
    assert(secondPath.parent_path() == ComputerCpp::TrayAppServerStateDirectory());
    assert(firstPath != secondPath);
    assert(firstPath == ComputerCpp::TrayAppServerStatePath(first.configName));
    assert(firstPath.filename().string().find('/') == std::string::npos);
    assert(secondPath.filename().string().find("..") == std::string::npos);
    const std::string sharedPrefix(48, 'a');
    const fs::path collidingPrefixA =
        ComputerCpp::TrayAppServerStatePath(sharedPrefix + "-first");
    const fs::path collidingPrefixB =
        ComputerCpp::TrayAppServerStatePath(sharedPrefix + "-second");
    assert(collidingPrefixA != collidingPrefixB);
    assert(collidingPrefixA.filename().string().substr(0, sharedPrefix.size()) ==
        sharedPrefix);
    assert(collidingPrefixB.filename().string().substr(0, sharedPrefix.size()) ==
        sharedPrefix);
    assert(ComputerCpp::SaveTrayAppServerState(first, firstPath, &error));
    assert(ComputerCpp::SaveTrayAppServerState(second, secondPath, &error));
    const auto paths = ComputerCpp::ListTrayAppServerStatePaths(&error);
    assert(paths.size() == 2);
    assert(paths[0] != paths[1]);
    assert(ComputerCpp::RemoveTrayAppServerState(firstPath, &error));
    assert(ComputerCpp::RemoveTrayAppServerState(secondPath, &error));
}

void TestRefStore() {
    ComputerCpp::Platform::RefRecord ref;
    ref.ref = "e1";
    ref.kind = "element";
    ref.source = "accessibility";
    ref.role = "AXButton";
    ref.name = "Continue";
    ref.bounds.available = true;
    ref.bounds.x = 10;
    ref.bounds.y = 20;
    ref.bounds.width = 100;
    ref.bounds.height = 40;

    fs::path path = ComputerCpp::SessionDir("unit") / "refs-test.json";
    ComputerCpp::SaveRefs(path, {ref});
    auto refs = ComputerCpp::LoadRefs(path);
    assert(refs.size() == 1);
    assert(refs[0].ref == "e1");
    assert(refs[0].name == "Continue");
    assert(refs[0].bounds.available);
    assert(ComputerCpp::FindRef(refs, "@e1").has_value());
    assert(ComputerCpp::FindRef(refs, " @e1 ").has_value());
    assert(!ComputerCpp::FindRef(refs, "@e2").has_value());
}

void TestNativeDependencies() {
    auto versions = ComputerCpp::NativeDeps::GetVersions();
    assert(!versions.curl.empty());
}

void TestUpdaterVersionParsing() {
    auto current = ComputerCpp::Updater::ParseSemVersion(ComputerCpp::Updater::CurrentVersion());
    assert(current.has_value());

    auto version = ComputerCpp::Updater::ParseSemVersion("v1.2.3");
    assert(version.has_value());
    assert(version->major == 1);
    assert(version->minor == 2);
    assert(version->patch == 3);
    assert(version->normalized == "1.2.3");

    assert(!ComputerCpp::Updater::ParseSemVersion("1.2").has_value());
    assert(!ComputerCpp::Updater::ParseSemVersion("1.2.x").has_value());
    assert(!ComputerCpp::Updater::CompareVersionStrings("bad", "1.2.3").has_value());
    assert(ComputerCpp::Updater::CompareVersionStrings("1.2.3", "1.2.3").value() == 0);
    assert(ComputerCpp::Updater::CompareVersionStrings("1.2.4", "1.2.3").value() > 0);
    assert(ComputerCpp::Updater::CompareVersionStrings("1.3.0", "1.2.99").value() > 0);
    assert(ComputerCpp::Updater::CompareVersionStrings("2.0.0", "1.99.99").value() > 0);
    assert(ComputerCpp::Updater::CompareVersionStrings("1.2.2", "1.2.3").value() < 0);
}

void TestUpdaterReleaseParsing() {
    assert(ComputerCpp::Updater::CompatibleMacAssetName() == "computer.cpp-macos-arm64.zip");
    assert(ComputerCpp::Updater::CompatibleWindowsAssetName() == "computer.cpp-windows-x64.msi");
    std::string compatibleAssetName = ComputerCpp::Updater::CompatibleAssetName();

    nlohmann::json release = {
        {"tag_name", "v0.3.0"},
        {"html_url", "https://github.com/gobii-ai/computer.cpp/releases/tag/0.3.0"},
        {"body", "notes"},
        {"assets", nlohmann::json::array({
            {
                {"name", compatibleAssetName},
                {"browser_download_url", "https://example.test/" + compatibleAssetName},
                {"size", 1234}
            }
        })}
    };

    auto available = ComputerCpp::Updater::ParseGitHubLatestRelease(release, "0.2.1");
    assert(available.status == ComputerCpp::Updater::CheckStatus::UpdateAvailable);
    assert(available.latestVersion == "0.3.0");
    assert(available.release.hasCompatibleAsset);
    assert(available.release.asset.name == compatibleAssetName);
    assert(available.release.asset.browserDownloadUrl == "https://example.test/" + compatibleAssetName);

    auto current = ComputerCpp::Updater::ParseGitHubLatestRelease(release, "0.3.0");
    assert(current.status == ComputerCpp::Updater::CheckStatus::UpToDate);

    release["assets"] = nlohmann::json::array({
        {{"name", "computer.cpp-0.3.0-linux-x86_64.zip"}, {"browser_download_url", "https://example.test/linux.zip"}}
    });
    auto missingAsset = ComputerCpp::Updater::ParseGitHubLatestRelease(release, "0.2.1");
    assert(missingAsset.status == ComputerCpp::Updater::CheckStatus::NoCompatibleAsset);

    release["tag_name"] = "release-candidate";
    auto invalid = ComputerCpp::Updater::ParseGitHubLatestRelease(release, "0.2.1");
    assert(invalid.status == ComputerCpp::Updater::CheckStatus::InvalidResponse);
}

void WriteTextFile(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << text;
}

void WriteExecutableFile(const fs::path& path) {
    WriteTextFile(path, "#!/bin/sh\nexit 0\n");
    std::error_code ec;
    fs::permissions(
        path,
        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
        fs::perm_options::add,
        ec);
}

void SetEnvValue(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

void RestoreEnvValue(const char* name, const std::optional<std::string>& value) {
    if (value.has_value()) {
        SetEnvValue(name, *value);
    } else {
#if defined(_WIN32)
        _putenv_s(name, "");
#else
        unsetenv(name);
#endif
    }
}

std::optional<std::string> CurrentEnvValue(const char* name) {
    if (const char* value = std::getenv(name)) {
        return std::string(value);
    }
    return std::nullopt;
}

bool SameExistingPath(const fs::path& lhs, const fs::path& rhs) {
    std::error_code ec;
    return fs::equivalent(lhs, rhs, ec) && !ec;
}

void TestLuaInterpreterResolution() {
    auto originalLua = CurrentEnvValue("COMPUTER_CPP_LUA");
    auto originalPath = CurrentEnvValue("PATH");

    fs::path root = MakeTempHome() / "lua-resolution";
#if defined(_WIN32)
    fs::path envLua = root / "env" / "custom-lua.exe";
    fs::path pathBin = root / "path-bin";
    fs::path pathLua = pathBin / "lua.exe";
    fs::path appExe = root / "bin" / "computer.cpp.exe";
    fs::path bundledLua = root / "bin" / "lua" / "bin" / "lua.exe";
    fs::path parentBundledLua = root / "lua" / "bin" / "lua.exe";
    fs::path cliBin = root / "cli-bin";
    fs::path pathCli = cliBin / "computer.cpp.exe";
    fs::path pathBundledLua = cliBin / "lua" / "bin" / "lua.exe";
#else
    fs::path envLua = root / "env" / "custom-lua";
    fs::path pathBin = root / "path-bin";
    fs::path pathLua = pathBin / "lua";
    fs::path appExe = root / "ComputerCpp.app" / "Contents" / "MacOS" / "computer.cpp";
    fs::path bundledLua = root / "ComputerCpp.app" / "Contents" / "Resources" / "lua" / "bin" / "lua";
    fs::path siblingCli = root / "computer.cpp";
    fs::path cliBin = root / "cli-bin";
    fs::path pathCli = cliBin / "computer.cpp";
    fs::path pathBundledLua = cliBin / "ComputerCpp.app" / "Contents" / "Resources" / "lua" / "bin" / "lua";
#endif

    WriteExecutableFile(envLua);
    SetEnvValue("COMPUTER_CPP_LUA", envLua.string());
    SetEnvValue("PATH", (root / "empty-path").string());
    assert(SameExistingPath(ComputerCpp::FindLuaInterpreter(appExe), envLua));

    RestoreEnvValue("COMPUTER_CPP_LUA", std::nullopt);
    WriteExecutableFile(bundledLua);
    assert(SameExistingPath(ComputerCpp::FindLuaInterpreter(appExe), bundledLua));
#if defined(_WIN32)
    fs::remove(bundledLua);
    WriteExecutableFile(parentBundledLua);
    assert(SameExistingPath(ComputerCpp::FindLuaInterpreter(appExe), parentBundledLua));
#else
    assert(SameExistingPath(ComputerCpp::FindLuaInterpreter(siblingCli), bundledLua));
#endif
    WriteExecutableFile(pathCli);
    WriteExecutableFile(pathBundledLua);
    SetEnvValue("PATH", cliBin.string());
    assert(SameExistingPath(ComputerCpp::FindLuaInterpreter("computer.cpp"), pathBundledLua));

#if defined(_WIN32)
    fs::remove(parentBundledLua);
#else
    fs::remove(bundledLua);
#endif
    WriteExecutableFile(pathLua);
    SetEnvValue("PATH", pathBin.string());
    assert(SameExistingPath(ComputerCpp::FindLuaInterpreter(appExe), pathLua));

    RestoreEnvValue("COMPUTER_CPP_LUA", originalLua);
    RestoreEnvValue("PATH", originalPath);
}

std::string TestInfoPlist(const std::string& bundleId, const std::string& version) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<!DOCTYPE plist PUBLIC \"-//Apple Computer//DTD PLIST 1.0//EN\" "
           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
           "<plist version=\"1.0\">\n"
           "<dict>\n"
           "  <key>CFBundleIdentifier</key>\n"
           "  <string>" + bundleId + "</string>\n"
           "  <key>CFBundleShortVersionString</key>\n"
           "  <string>" + version + "</string>\n"
           "</dict>\n"
           "</plist>\n";
}

ComputerCpp::Updater::ReleaseInfo TestReleaseInfo(const std::string& version) {
    ComputerCpp::Updater::ReleaseInfo release;
    release.tagName = version;
    release.version = version;
    release.hasCompatibleAsset = true;
    release.asset.name = ComputerCpp::Updater::CompatibleMacAssetName();
    release.asset.browserDownloadUrl = "https://example.test/" + release.asset.name;
    return release;
}

fs::path CreateUpdaterZip(const fs::path& tempRoot, const std::string& version) {
    fs::path zipPath = tempRoot / ("fixture-" + version + ".zip");
    fs::path sourceParent = tempRoot / "source";
    std::string rootName = "computer.cpp-" + version + "-macos-arm64";
    fs::path root = sourceParent / rootName;
    fs::create_directories(root);
    std::string command = "cd " + ComputerCpp::Updater::ShellQuote(sourceParent.string()) +
        " && /usr/bin/ditto -c -k --sequesterRsrc --keepParent " +
        ComputerCpp::Updater::ShellQuote(rootName) + " " + ComputerCpp::Updater::ShellQuote(zipPath.string());
    int status = std::system(command.c_str());
    assert(status == 0);
    return zipPath;
}

void TestUpdaterInstallHelperScript() {
    std::string script = ComputerCpp::Updater::BuildInstallHelperScript(
        123,
        "/tmp/staged dir/ComputerCpp.app",
        "/tmp/staged dir/computer.cpp",
        "/Applications/Computer Cpp.app",
        "/Applications/computer cpp");
    assert(script.find("pid=123") != std::string::npos);
    assert(script.find("staged_app='/tmp/staged dir/ComputerCpp.app'") != std::string::npos);
    assert(script.find("target_app='/Applications/Computer Cpp.app'") != std::string::npos);
    assert(script.find("target_cli='/Applications/computer cpp'") != std::string::npos);
    assert(script.find("if [ \"$pid\" -le 0 ]") != std::string::npos);
    assert(script.find("wait_count=0") != std::string::npos);
    assert(script.find("kill -9 \"$pid\"") != std::string::npos);
    assert(script.find("echo \"relaunching $target_app\"") != std::string::npos);
}

void TestUpdaterStagingValidation() {
    if (!ComputerCpp::Updater::IsMacArm64Supported()) {
        return;
    }

    fs::path temp = MakeTempHome() / "updater";
    std::string version = "9.8.7";
    fs::path root = temp / "source" / ("computer.cpp-" + version + "-macos-arm64");
    WriteTextFile(root / "ComputerCpp.app" / "Contents" / "Info.plist", TestInfoPlist("org.computercpp.app", version));
    WriteTextFile(root / "computer.cpp", "#!/bin/sh\n");
    auto okZip = CreateUpdaterZip(temp, version);
    auto staged = ComputerCpp::Updater::StageDownloadedUpdate(TestReleaseInfo(version), okZip, false);
    assert(staged.ok);
    assert(staged.appBundlePath.filename() == "ComputerCpp.app");
    assert(staged.cliPath.filename() == "computer.cpp");

    fs::path wrongBundleTemp = MakeTempHome() / "updater-wrong-bundle";
    fs::path wrongBundleRoot = wrongBundleTemp / "source" / ("computer.cpp-" + version + "-macos-arm64");
    WriteTextFile(wrongBundleRoot / "ComputerCpp.app" / "Contents" / "Info.plist", TestInfoPlist("example.bad", version));
    WriteTextFile(wrongBundleRoot / "computer.cpp", "#!/bin/sh\n");
    auto wrongBundle = ComputerCpp::Updater::StageDownloadedUpdate(TestReleaseInfo(version), CreateUpdaterZip(wrongBundleTemp, version), false);
    assert(!wrongBundle.ok);
    assert(wrongBundle.error.find("bundle id") != std::string::npos);

    fs::path wrongVersionTemp = MakeTempHome() / "updater-wrong-version";
    fs::path wrongVersionRoot = wrongVersionTemp / "source" / ("computer.cpp-" + version + "-macos-arm64");
    WriteTextFile(wrongVersionRoot / "ComputerCpp.app" / "Contents" / "Info.plist", TestInfoPlist("org.computercpp.app", "9.8.6"));
    WriteTextFile(wrongVersionRoot / "computer.cpp", "#!/bin/sh\n");
    auto wrongVersion = ComputerCpp::Updater::StageDownloadedUpdate(TestReleaseInfo(version), CreateUpdaterZip(wrongVersionTemp, version), false);
    assert(!wrongVersion.ok);
    assert(wrongVersion.error.find("does not match release") != std::string::npos);

    fs::path missingCliTemp = MakeTempHome() / "updater-missing-cli";
    fs::path missingCliRoot = missingCliTemp / "source" / ("computer.cpp-" + version + "-macos-arm64");
    WriteTextFile(missingCliRoot / "ComputerCpp.app" / "Contents" / "Info.plist", TestInfoPlist("org.computercpp.app", version));
    auto missingCli = ComputerCpp::Updater::StageDownloadedUpdate(TestReleaseInfo(version), CreateUpdaterZip(missingCliTemp, version), false);
    assert(!missingCli.ok);
    assert(missingCli.error.find("computer.cpp") != std::string::npos);
}

void TestLinuxPngUtilities() {
    const fs::path fullPath = ComputerCpp::SessionDir("unit") / "linux-png-full.png";
    const fs::path scaledPath = ComputerCpp::SessionDir("unit") / "linux-png-scaled.png";
    const std::vector<uint8_t> rgb = {
        255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0,
        20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130,
    };

    assert(ComputerCpp::Platform::LinuxPng::WritePngRgb(fullPath.string(), 4, 2, rgb));
    int width = 0;
    int height = 0;
    assert(ComputerCpp::Platform::LinuxPng::ReadPngSize(fullPath.string(), width, height));
    assert(width == 4);
    assert(height == 2);

    assert(ComputerCpp::Platform::LinuxPng::WritePngRgbScaled(scaledPath.string(), 4, 2, rgb, 2));
    assert(ComputerCpp::Platform::LinuxPng::ReadPngSize(scaledPath.string(), width, height));
    assert(width == 2);
    assert(height == 1);

    assert(!ComputerCpp::Platform::LinuxPng::WritePngRgb(fullPath.string(), 4, 2, {1, 2, 3}));
}

void TestImageUtilities() {
    const fs::path imagePath = ComputerCpp::SessionDir("unit") / "image-helpers.png";
    const std::vector<uint8_t> rgb = {
        255, 0, 0, 0, 255, 0, 0, 0, 255,
        255, 255, 0, 20, 30, 40, 50, 60, 70
    };

    auto image = ComputerCpp::Image::MakeRgbImage(3, 2, rgb);
    assert(image.valid());
    assert(!ComputerCpp::Image::MakeRgbImage(3, 2, {1, 2, 3}).valid());

    auto crop = ComputerCpp::Image::CropRgb(image.rgb, image.width, image.height, 1, 0, 2, 2);
    assert(crop.valid());
    assert(crop.width == 2);
    assert(crop.height == 2);
    assert(crop.rgb[0] == 0);
    assert(crop.rgb[1] == 255);
    assert(crop.rgb[2] == 0);

    auto clamped = ComputerCpp::Image::CropRgb(image.rgb, image.width, image.height, 99, 99, 2, 2);
    assert(clamped.valid());
    assert(clamped.width == 2);
    assert(clamped.height == 2);

    assert(ComputerCpp::Image::WritePngRgb(imagePath.string(), image));
    auto loaded = ComputerCpp::Image::ReadImageRgb(imagePath.string());
    assert(loaded.has_value());
    assert(loaded->valid());
    assert(loaded->width == 3);
    assert(loaded->height == 2);
}

void TestHumanInputPlans() {
    ComputerCpp::HumanInput::Random rng;
    auto nearPlan = ComputerCpp::HumanInput::PlanPointerMove(18.0, 0, 0, rng);
    assert(nearPlan.durationMs >= 45);
    assert(nearPlan.durationMs <= 1600);
    assert(nearPlan.steps >= 2);
    assert(nearPlan.steps <= 80);

    auto requestedPlan = ComputerCpp::HumanInput::PlanPointerMove(450.0, 333, 22, rng);
    assert(requestedPlan.durationMs == 333);
    assert(requestedPlan.steps == 22);

    auto scrollPlan = ComputerCpp::HumanInput::PlanScrollGesture(-620, 0, 0, 0, rng);
    assert(scrollPlan.durationMs >= 50);
    assert(scrollPlan.durationMs <= 2500);
    assert(scrollPlan.steps >= 1);
    assert(scrollPlan.steps <= 80);

    auto requestedScrollPlan = ComputerCpp::HumanInput::PlanScrollGesture(-620, 0, 444, 17, rng);
    assert(requestedScrollPlan.durationMs == 444);
    assert(requestedScrollPlan.steps == 17);

    auto clusters = ComputerCpp::HumanInput::PlanScrollClusters(-420, 0, 700, 28, 120, rng);
    assert(clusters.size() == 4);
    int clusterTotalY = 0;
    for (size_t i = 0; i < clusters.size(); ++i) {
        clusterTotalY += clusters[i].deltaY;
        assert(std::abs(clusters[i].deltaY) <= 120);
        assert(clusters[i].durationMs >= 90);
        assert(clusters[i].steps >= 2);
        if (i + 1 < clusters.size()) {
            assert(clusters[i].pauseAfterMs >= 70);
            assert(clusters[i].pauseAfterMs <= 165);
        }
    }
    assert(clusterTotalY == -420);
    assert(clusters.back().pauseAfterMs == 0);

    auto path = ComputerCpp::HumanInput::CurvedPath({0.0, 0.0}, {100.0, 50.0}, 12, rng);
    assert(path.size() == 12);
    assert(std::abs(path.back().x - 100.0) < 0.001);
    assert(std::abs(path.back().y - 50.0) < 0.001);
}

void TestTimelineStorage() {
    const std::string session = "timeline-unit";
    int64_t eventId = ComputerCpp::BeginTimelineEvent(session, "test", {{"value", 42}});
    ComputerCpp::EndTimelineEvent(session, eventId);
    auto events = ComputerCpp::RecentTimelineEvents(session, 5);
    assert(!events.empty());
    assert(events.front().id == eventId);
    assert(events.front().type == "test");
    assert(ComputerCpp::LastTimelineEventId(session).value() == eventId);

    sqlite3* db = nullptr;
    if (sqlite3_open(ComputerCpp::TimelineDbPath(session).string().c_str(), &db) != SQLITE_OK) {
        throw std::runtime_error("failed to open timeline test database");
    }
    ExecSql(
        db,
        "INSERT INTO frames(event_id,label,path,captured_at_ms,width,height) VALUES (" +
            std::to_string(eventId) + ",'after','/tmp/frame.png',123,80,60);"
    );
    sqlite3_close(db);

    auto frames = ComputerCpp::TimelineFramesForEvent(session, eventId);
    assert(frames.size() == 1);
    assert(frames.front().eventId == eventId);
    assert(frames.front().label == "after");
    assert(frames.front().width == 80);
    assert(frames.front().height == 60);

}

template <typename Function>
void RunTest(const char* name, Function function) {
    std::cout << "[test] " << name << std::endl;
    function();
}

}

int main() {
#if defined(_WIN32)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    fs::path tempHome = MakeTempHome();
    SetEnvValue("COMPUTER_CPP_HOME", tempHome.string());

    RunTest("StringUtils", TestStringUtils);
    RunTest("PlatformKeyResolution", TestPlatformKeyResolution);
#if defined(_WIN32)
    RunTest("WindowsNativeInputDelivery", TestWindowsNativeInputDelivery);
    RunTest("WindowsAppCatalogMatching", TestWindowsAppCatalogMatching);
#endif
    RunTest("BrowserRegistry", TestBrowserRegistry);
    RunTest("AppConfigServerRoundTrip", TestAppConfigServerRoundTrip);
    RunTest("ServerAppNameValidation", TestServerAppNameValidation);
    RunTest("ServerPortConfigMigration", TestServerPortConfigMigration);
    RunTest("BrowserConfigValidation", TestBrowserConfigValidation);
    RunTest("CommandRecordingLifecycle", TestCommandRecordingLifecycle);
    RunTest("NativeCommandRecordingSmoke", TestNativeCommandRecordingSmoke);
    RunTest("TrayServerState", TestTrayServerState);
    RunTest("RefStore", TestRefStore);
    RunTest("NativeDependencies", TestNativeDependencies);
    RunTest("UpdaterVersionParsing", TestUpdaterVersionParsing);
    RunTest("UpdaterReleaseParsing", TestUpdaterReleaseParsing);
    RunTest("LuaInterpreterResolution", TestLuaInterpreterResolution);
    RunTest("UpdaterInstallHelperScript", TestUpdaterInstallHelperScript);
    RunTest("UpdaterStagingValidation", TestUpdaterStagingValidation);
    RunTest("LinuxPngUtilities", TestLinuxPngUtilities);
    RunTest("ImageUtilities", TestImageUtilities);
    RunTest("HumanInputPlans", TestHumanInputPlans);
    RunTest("InferenceTests", ComputerCpp::Tests::RunInferenceTests);
    RunTest("GobiiTests", ComputerCpp::Tests::RunGobiiTests);
    RunTest("ControlSessionTests", ComputerCpp::Tests::RunControlSessionTests);
    RunTest("DaemonTests", ComputerCpp::Tests::RunDaemonTests);
    RunTest("DaemonDispatchTests", ComputerCpp::Tests::RunDaemonDispatchTests);
    RunTest("CliTests", ComputerCpp::Tests::RunCliTests);
    RunTest("TimelineStorage", TestTimelineStorage);

    std::cout << "computer.cpp core tests passed." << std::endl;
    return 0;
}
