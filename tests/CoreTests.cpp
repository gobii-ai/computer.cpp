#include "computer_cpp/AppConfig.h"
#include "computer_cpp/AppPaths.h"
#include "computer_cpp/HumanInput.h"
#include "computer_cpp/Image.h"
#include "computer_cpp/LuaRunner.h"
#include "computer_cpp/NativeDeps.h"
#include "computer_cpp/RefStore.h"
#include "computer_cpp/StringUtils.h"
#include "computer_cpp/Timeline.h"
#include "computer_cpp/TrayServerState.h"
#include "computer_cpp/Updater.h"

#include "LinuxPng.h"
#include "TestSupport.h"
#include "UpdaterInternal.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sqlite3.h>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;
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

void TestAppConfigServerRoundTrip() {
    std::string missingError;
    ComputerCpp::AppConfig missing = ComputerCpp::LoadAppConfig(&missingError);
    assert(missingError.empty());
    assert(missing.server.host == "127.0.0.1");
    assert(missing.server.basePort == 8787);
    assert(missing.server.apps.empty());

    ComputerCpp::AppConfig defaults = ComputerCpp::DefaultAppConfig();
    assert(defaults.server.host == "127.0.0.1");
    assert(defaults.server.basePort == 8787);
    assert(defaults.server.authToken.empty());

    ComputerCpp::AppConfig config = defaults;
    config.server.host = "0.0.0.0";
    config.server.basePort = 8790;
    config.server.authToken = "test-token";
    config.server.allowedOrigins = {"https://mcp.example.com", "http://127.0.0.1:3000"};

    ComputerCpp::ServerAppConfig linkedin;
    linkedin.name = "linkedin";
    linkedin.displayName = "LinkedIn Recruiter";
    linkedin.path = "/tmp/linkedin-recruiter.lua";
    linkedin.port = 8788;
    config.server.apps[linkedin.name] = linkedin;

    std::string toml = ComputerCpp::AppConfigToToml(config);
    assert(toml.find("[server]") != std::string::npos);
    assert(toml.find("[server.apps.linkedin]") != std::string::npos);
    assert(toml.find("auth_token = \"test-token\"") != std::string::npos);

    std::string error;
    assert(ComputerCpp::SaveAppConfig(config, &error));
    ComputerCpp::AppConfig loaded = ComputerCpp::LoadAppConfig(&error);
    assert(error.empty());
    assert(loaded.server.host == "0.0.0.0");
    assert(loaded.server.basePort == 8790);
    assert(loaded.server.authToken == "test-token");
    assert(loaded.server.allowedOrigins.size() == 2);
    assert(loaded.server.apps.contains("linkedin"));
    assert(loaded.server.apps["linkedin"].displayName == "LinkedIn Recruiter");
    assert(loaded.server.apps["linkedin"].path == "/tmp/linkedin-recruiter.lua");
    assert(loaded.server.apps["linkedin"].port == 8788);

    auto redacted = ComputerCpp::AppConfigToJson(loaded);
    assert(redacted["server"]["authToken"] == "<redacted>");
    auto visible = ComputerCpp::AppConfigToJson(loaded, false);
    assert(visible["server"]["authToken"] == "test-token");

    ComputerCpp::AppConfig tokenConfig = ComputerCpp::DefaultAppConfig();
    assert(ComputerCpp::EnsureServerAuthToken(tokenConfig));
    assert(tokenConfig.server.authToken.size() >= 32);
    std::string generated = tokenConfig.server.authToken;
    assert(!ComputerCpp::EnsureServerAuthToken(tokenConfig));
    assert(tokenConfig.server.authToken == generated);
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
    state.displayName = "Test App";
    state.startedAt = "2026-06-22T00:00:00Z";

    std::string error;
    assert(ComputerCpp::SaveTrayAppServerState(state, path, &error));
    auto loaded = ComputerCpp::LoadTrayAppServerState(path, &error);
    assert(loaded.has_value());
    assert(loaded->pid == state.pid);
    assert(loaded->host == state.host);
    assert(loaded->port == state.port);
    assert(loaded->url == state.url);
    assert(loaded->appPath == state.appPath);
    assert(loaded->appId == state.appId);
    assert(loaded->displayName == state.displayName);
    assert(loaded->startedAt == state.startedAt);

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
    assert(ComputerCpp::Updater::CompatibleWindowsAssetName("0.3.0") == "computer.cpp-0.3.0-windows-x64.msi");

    nlohmann::json release = {
        {"tag_name", "v0.3.0"},
        {"html_url", "https://github.com/gobii-ai/computer.cpp/releases/tag/0.3.0"},
        {"body", "notes"},
        {"assets", nlohmann::json::array({
            {
                {"name", "computer.cpp-0.3.0-macos-arm64.zip"},
                {"browser_download_url", "https://example.test/computer.cpp-0.3.0-macos-arm64.zip"},
                {"size", 1234}
            }
        })}
    };

    auto available = ComputerCpp::Updater::ParseGitHubLatestRelease(release, "0.2.1");
    assert(available.status == ComputerCpp::Updater::CheckStatus::UpdateAvailable);
    assert(available.latestVersion == "0.3.0");
    assert(available.release.hasCompatibleAsset);
    assert(available.release.asset.name == "computer.cpp-0.3.0-macos-arm64.zip");
    assert(available.release.asset.browserDownloadUrl == "https://example.test/computer.cpp-0.3.0-macos-arm64.zip");

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

void TestLuaInterpreterResolution() {
    auto originalLua = CurrentEnvValue("COMPUTER_CPP_LUA");
    auto originalPath = CurrentEnvValue("PATH");

    fs::path root = MakeTempHome() / "lua-resolution";
    fs::path envLua = root / "env" / "custom-lua";
    fs::path pathBin = root / "path-bin";
    fs::path pathLua = pathBin / "lua";
    fs::path appExe = root / "ComputerCpp.app" / "Contents" / "MacOS" / "computer.cpp";
    fs::path bundledLua = root / "ComputerCpp.app" / "Contents" / "Resources" / "lua" / "bin" / "lua";
    fs::path siblingCli = root / "computer.cpp";
    fs::path cliBin = root / "cli-bin";
    fs::path pathCli = cliBin / "computer.cpp";
    fs::path pathBundledLua = cliBin / "ComputerCpp.app" / "Contents" / "Resources" / "lua" / "bin" / "lua";

    WriteExecutableFile(envLua);
    SetEnvValue("COMPUTER_CPP_LUA", envLua.string());
    SetEnvValue("PATH", (root / "empty-path").string());
    assert(ComputerCpp::FindLuaInterpreter(appExe) == envLua);

    RestoreEnvValue("COMPUTER_CPP_LUA", std::nullopt);
    WriteExecutableFile(bundledLua);
    assert(ComputerCpp::FindLuaInterpreter(appExe) == bundledLua);
    assert(ComputerCpp::FindLuaInterpreter(siblingCli) == bundledLua);
    WriteExecutableFile(pathCli);
    WriteExecutableFile(pathBundledLua);
    SetEnvValue("PATH", cliBin.string());
    assert(ComputerCpp::FindLuaInterpreter("computer.cpp") == pathBundledLua);

    fs::remove(bundledLua);
    WriteExecutableFile(pathLua);
    SetEnvValue("PATH", pathBin.string());
    assert(ComputerCpp::FindLuaInterpreter(appExe) == pathLua);

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
    release.asset.name = ComputerCpp::Updater::CompatibleMacAssetName(version);
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

}

int main() {
    fs::path tempHome = MakeTempHome();
    SetEnvValue("COMPUTER_CPP_HOME", tempHome.string());

    TestStringUtils();
    TestAppConfigServerRoundTrip();
    TestTrayServerState();
    TestRefStore();
    TestNativeDependencies();
    TestUpdaterVersionParsing();
    TestUpdaterReleaseParsing();
    TestLuaInterpreterResolution();
    TestUpdaterInstallHelperScript();
    TestUpdaterStagingValidation();
    TestLinuxPngUtilities();
    TestImageUtilities();
    TestHumanInputPlans();
    ComputerCpp::Tests::RunInferenceTests();
    ComputerCpp::Tests::RunControlSessionTests();
    ComputerCpp::Tests::RunDaemonTests();
    ComputerCpp::Tests::RunDaemonDispatchTests();
    ComputerCpp::Tests::RunCliTests();
    TestTimelineStorage();

    std::cout << "computer.cpp core tests passed." << std::endl;
    return 0;
}
