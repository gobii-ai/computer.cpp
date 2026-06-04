#include "computer_cpp/AppPaths.h"
#include "computer_cpp/Daemon.h"
#include "computer_cpp/RefStore.h"

#include "DaemonDesktop.h"
#include "DaemonInput.h"
#include "DaemonObservation.h"
#include "DaemonTargetCommand.h"
#include "DaemonTargetGeometry.h"
#include "DaemonTargetRefs.h"
#include "DaemonTargetResolve.h"
#include "DaemonTargetText.h"
#include "TestSupport.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace {

void TestDaemonSocketPathStaysShort() {
    const char* previous = std::getenv("COMPUTER_CPP_HOME");
    std::string previousValue = previous ? previous : "";
    fs::path longHome = fs::temp_directory_path() /
        "computer.cpp-tests-very-long-home-path-for-control-session-daemon-socket-fallback" /
        "nested-directory-with-enough-characters-to-exceed-unix-domain-socket-limits";
    setenv("COMPUTER_CPP_HOME", longHome.c_str(), 1);
    auto socketPath = ComputerCpp::SocketPathForSession("default");
#if defined(__unix__) || defined(__APPLE__)
    assert(socketPath.string().size() < 100);
#endif
    if (previous) {
        setenv("COMPUTER_CPP_HOME", previousValue.c_str(), 1);
    } else {
        unsetenv("COMPUTER_CPP_HOME");
    }
}

void TestDaemonSessionValidation() {
    assert(ComputerCpp::IsSessionNameValid("default"));
    assert(ComputerCpp::IsSessionNameValid("bookkeeping-1"));
    assert(!ComputerCpp::IsSessionNameValid(""));
    assert(!ComputerCpp::IsSessionNameValid("../x"));
    assert(!ComputerCpp::IsSessionNameValid("bad/name"));
}

void TestDaemonTargetGeometry() {
    auto reversed = ComputerCpp::RectFromTargetString("rect:40,30,10,5");
    assert(reversed.has_value());
    assert(reversed->available);
    assert(reversed->x == 10.0);
    assert(reversed->y == 5.0);
    assert(reversed->width == 30.0);
    assert(reversed->height == 25.0);

    auto objectRect = ComputerCpp::RectFromJson({
        {"x", "12.5"},
        {"y", 20},
        {"width", 80},
        {"height", 40}
    });
    assert(objectRect.has_value());
    assert(objectRect->x == 12.5);
    assert(objectRect->width == 80.0);

    auto clickRect = ComputerCpp::RectFromClickParams({
        {"target", "box:0,0,100,50"},
        {"rectClickXFraction", 0.99},
        {"rectClickYFraction", 0.01}
    });
    assert(clickRect.has_value());
    auto point = ComputerCpp::StablePointInRect(*clickRect, {
        {"rectClickXFraction", 0.99},
        {"rectClickYFraction", 0.01}
    });
    assert(point.first == 92.0);
    assert(point.second == 6.0);
    assert(ComputerCpp::PointInsideBounds(point.first, point.second, *clickRect));
    assert(!ComputerCpp::RectFromTargetString("point:1,2").has_value());
    assert(!ComputerCpp::RectFromTargetString("rect:1,2,3oops,4").has_value());
    assert(!ComputerCpp::RectFromTargetString("rect:1,2,inf,4").has_value());
    assert(!ComputerCpp::NumberFromJson("12px").has_value());
    assert(!ComputerCpp::RectFromClickParams({{"target", true}}).has_value());
    auto fallbackPoint = ComputerCpp::StablePointInRect(*clickRect, {
        {"rectClickXFraction", "right"},
        {"rectClickYFraction", true}
    });
    assert(ComputerCpp::PointInsideBounds(fallbackPoint.first, fallbackPoint.second, *clickRect));
}

void TestDaemonObservationHelpers() {
    ComputerCpp::Platform::Bounds container;
    container.available = true;
    container.x = 10;
    container.y = 20;
    container.width = 200;
    container.height = 100;

    ComputerCpp::Platform::Bounds inside;
    inside.available = true;
    inside.x = 50;
    inside.y = 40;
    inside.width = 20;
    inside.height = 20;
    assert(ComputerCpp::BoundsCenterInside(inside, container));

    ComputerCpp::Platform::Bounds outside = inside;
    outside.x = 300;
    assert(!ComputerCpp::BoundsCenterInside(outside, container));

    ComputerCpp::Platform::Bounds unavailable = outside;
    unavailable.available = false;
    assert(ComputerCpp::BoundsCenterInside(unavailable, container));

    assert(ComputerCpp::ParseEventId("@ev42").value() == 42);
    assert(ComputerCpp::ParseEventId("ev7").value() == 7);
    assert(ComputerCpp::ParseEventId("5").value() == 5);
    assert(!ComputerCpp::ParseEventId("@ev12x").has_value());
    assert(ComputerCpp::IsObservationCommand("observe_events"));
    assert(!ComputerCpp::IsObservationCommand("observe_summary"));
    assert(!ComputerCpp::IsObservationCommand("observe_missing"));
}

void TestDaemonTargetResolveCoordinates() {
    auto numeric = ComputerCpp::PointFromTarget("unit", {
        {"x", "45.5"},
        {"y", 72}
    });
    assert(numeric.has_value());
    assert(std::abs(numeric->first - 45.5) < 0.001);
    assert(std::abs(numeric->second - 72.0) < 0.001);
    assert(!ComputerCpp::PointFromTarget("unit", {{"x", "45px"}, {"y", 72}}).has_value());
    assert(!ComputerCpp::PointFromTarget("unit", {{"target", true}}).has_value());

    auto point = ComputerCpp::PointFromTarget("unit", {
        {"target", "point:12.5,34.25"}
    });
    assert(point.has_value());
    assert(std::abs(point->first - 12.5) < 0.001);
    assert(std::abs(point->second - 34.25) < 0.001);
    assert(!ComputerCpp::PointFromTarget("unit", {{"target", "point:12.5,34px"}}).has_value());

    auto rect = ComputerCpp::ClickTargetFromParams("unit", {
        {"target", "rect:10,20,50,80"},
        {"stablePoint", "center"}
    });
    assert(rect.has_value());
    assert(rect->kind == "rect");
    assert(rect->rect.has_value());
    assert(std::abs(rect->x - 30.0) < 0.001);
    assert(std::abs(rect->y - 50.0) < 0.001);

    auto anchor = ComputerCpp::ScrollAnchorPoint("unit", {
        {"at", "point:10,20"},
        {"atOffsetX", 3},
        {"atOffsetY", -4}
    });
    assert(anchor.has_value());
    assert(std::abs(anchor->first - 13.0) < 0.001);
    assert(std::abs(anchor->second - 16.0) < 0.001);

    auto stringOffsetAnchor = ComputerCpp::ScrollAnchorPoint("unit", {
        {"at", "point:10,20"},
        {"atOffsetX", "2.5"},
        {"atOffsetY", "-1.5"}
    });
    assert(stringOffsetAnchor.has_value());
    assert(std::abs(stringOffsetAnchor->first - 12.5) < 0.001);
    assert(std::abs(stringOffsetAnchor->second - 18.5) < 0.001);
    assert(!ComputerCpp::ScrollAnchorPoint("unit", {
        {"at", "point:10,20"},
        {"atOffsetX", "2px"},
        {"atOffsetY", 0}
    }).has_value());
    assert(!ComputerCpp::ScrollAnchorPoint("unit", {{"anchor", "yes"}}).has_value());
    assert(!ComputerCpp::ScrollAnchorPoint("unit", {{"centerAnchor", "yes"}}).has_value());
}

void TestDaemonInputKeyChordParsing() {
    auto splitKeys = ComputerCpp::KeyChordFromParams({{"keys", "Cmd+Shift+G"}});
    assert(splitKeys.size() == 3);
    assert(splitKeys[0] == "Cmd");
    assert(splitKeys[1] == "Shift");
    assert(splitKeys[2] == "G");

    auto arrayKeys = ComputerCpp::KeyChordFromParams({{"keys", {"control", "a"}}});
    assert(arrayKeys.size() == 2);
    assert(arrayKeys[0] == "control");
    assert(arrayKeys[1] == "a");
}

void TestDaemonDesktopVisibleWindowIds() {
    ComputerCpp::Platform::WindowInfo hidden;
    hidden.available = false;
    hidden.id = "hidden";

    ComputerCpp::Platform::WindowInfo unnamed;
    unnamed.available = true;

    ComputerCpp::Platform::WindowInfo visible;
    visible.available = true;
    visible.id = "window-1";

    auto ids = ComputerCpp::VisibleWindowIds({hidden, unnamed, visible});
    assert(ids.size() == 1);
    assert(ids.count("window-1") == 1);
}

void TestDaemonTargetTextScoring() {
    assert(ComputerCpp::HasRemovedVisualTargetPrefix("text:\"Open Settings\""));
    assert(ComputerCpp::HasRemovedVisualTargetPrefix("field: Search "));
    assert(!ComputerCpp::HasRemovedVisualTargetPrefix("role:button[name=\"Open\"]"));
    assert(ComputerCpp::TargetQueryString("\"Open Settings\"") == "Open Settings");
    assert(ComputerCpp::TargetQueryString(" Search ") == "Search");
    assert(ComputerCpp::NormalizeRole("AXTextField") == "textarea");
    auto role = ComputerCpp::ParseRoleTarget("role:AXButton[name=\"Continue\"]");
    assert(role.valid);
    assert(role.role == "button");
    assert(role.name == "Continue");
    auto emptyNameRole = ComputerCpp::ParseRoleTarget("role:button[name=\"\"]");
    assert(!emptyNameRole.valid);
    assert(emptyNameRole.malformed);
    assert(ComputerCpp::NormalizeRef(" @e42 ") == "e42");
}

void TestDaemonTargetRefCandidates() {
    std::vector<ComputerCpp::Platform::RefRecord> refs;
    ComputerCpp::Platform::RefRecord button;
    button.ref = "btn1";
    button.source = "accessibility";
    button.role = "AXButton";
    button.name = "Continue";
    button.bounds = {true, 10, 20, 100, 30};
    button.confidence = 0.91;
    refs.push_back(button);

    ComputerCpp::Platform::RefRecord field;
    field.ref = "field1";
    field.source = "accessibility";
    field.role = "AXTextField";
    field.name = "Search";
    field.value = "query";
    field.bounds = {true, 20, 70, 160, 34};
    field.confidence = 0.84;
    refs.push_back(field);

    ComputerCpp::SaveRefs(ComputerCpp::RefStorePath("target-ref-test"), refs);

    auto role = ComputerCpp::ParseRoleTarget("role:button[name=continue]");
    auto roleCandidates = ComputerCpp::RoleTargetCandidates("target-ref-test", role, 5);
    assert(roleCandidates.size() == 1);
    assert(roleCandidates[0]["target"] == "@btn1");
    assert(roleCandidates[0]["text"] == "Continue");

    auto snapshotCandidates = ComputerCpp::SnapshotTargetCandidates("target-ref-test", "search", 5);
    assert(snapshotCandidates.size() == 1);
    assert(snapshotCandidates[0]["target"] == "@field1");

    auto limited = ComputerCpp::SnapshotTargetCandidates("target-ref-test", "a", 1);
    assert(limited.size() == 1);
}

void TestDaemonRemovedVisualTargets() {
    auto resolve = ComputerCpp::RunTargetCommand(
        "target-ref-test",
        "target_resolve",
        {{"target", "text:\"Continue\""}}
    );
    assert(resolve["ok"] == false);
    assert(resolve["code"] == "unsupported_visual_target");

    auto find = ComputerCpp::RunTargetCommand(
        "target-ref-test",
        "target_find",
        {{"query", "field: Search"}}
    );
    assert(find["ok"] == true);
    assert(find["data"]["candidates"].empty());
}

}

namespace ComputerCpp::Tests {

void RunDaemonTests() {
    TestDaemonSocketPathStaysShort();
    TestDaemonSessionValidation();
    TestDaemonTargetGeometry();
    TestDaemonObservationHelpers();
    TestDaemonTargetResolveCoordinates();
    TestDaemonInputKeyChordParsing();
    TestDaemonDesktopVisibleWindowIds();
    TestDaemonTargetTextScoring();
    TestDaemonTargetRefCandidates();
    TestDaemonRemovedVisualTargets();
}

}
