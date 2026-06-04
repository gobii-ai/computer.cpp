#include "computer_cpp/AppPaths.h"
#include "computer_cpp/HumanInput.h"
#include "computer_cpp/Image.h"
#include "computer_cpp/NativeDeps.h"
#include "computer_cpp/RefStore.h"
#include "computer_cpp/StringUtils.h"
#include "computer_cpp/Timeline.h"

#include "LinuxPng.h"
#include "TestSupport.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
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
    setenv("COMPUTER_CPP_HOME", tempHome.c_str(), 1);

    TestStringUtils();
    TestRefStore();
    TestNativeDependencies();
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
