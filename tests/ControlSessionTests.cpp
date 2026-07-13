#include "TestSupport.h"

#include "computer_cpp/ControlSession.h"

#include <cassert>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

size_t CountOccurrences(const std::string& text, const std::string& needle) {
    size_t count = 0;
    size_t offset = 0;
    while ((offset = text.find(needle, offset)) != std::string::npos) {
        ++count;
        offset += needle.size();
    }
    return count;
}

void TestControlSessionLeases() {
    ComputerCpp::ControlSessionAcquireOptions options;
    options.scope = "desktop:test";
    options.daemonSession = "unit";
    options.owner = "core-test";
    options.purpose = "lease-test";
    options.ttlMs = 5000;
    auto first = ComputerCpp::AcquireControlSession(options);
    assert(first.ok);
    assert(first.record.scope == "desktop:test");
    assert(first.record.owner == "core-test");
    assert(!first.record.token.empty());

    auto busy = ComputerCpp::AcquireControlSession(options);
    assert(!busy.ok);
    assert(busy.code == "control_session_busy");
    assert(busy.holder.has_value());

    auto valid = ComputerCpp::ValidateControlSession(first.record.token, "desktop:test");
    assert(valid.ok);

    auto renewed = ComputerCpp::RenewControlSession(first.record.token, 7000);
    assert(renewed.ok);
    assert(renewed.record.expiresAtMs >= first.record.expiresAtMs);

    auto mismatch = ComputerCpp::ValidateControlSession(first.record.token, "desktop:other");
    assert(!mismatch.ok);
    assert(mismatch.code == "control_session_scope_mismatch");

    auto released = ComputerCpp::ReleaseControlSession(first.record.token);
    assert(released.ok);
    assert(released.released);

    auto afterRelease = ComputerCpp::ValidateControlSession(first.record.token, "desktop:test");
    assert(!afterRelease.ok);
    assert(afterRelease.code == "control_session_not_active");
}

void TestControlSessionAcquireOrResumeReusesMatchingLease() {
    ComputerCpp::ControlSessionAcquireOptions options;
    options.scope = "desktop:test-resume";
    options.daemonSession = "unit";
    options.owner = "resume-owner";
    options.purpose = "resume-purpose";
    options.ttlMs = 2000;

    auto acquired = ComputerCpp::AcquireOrResumeControlSession(options);
    assert(acquired.ok);

    options.ttlMs = 5000;
    auto resumed = ComputerCpp::AcquireOrResumeControlSession(options);
    assert(resumed.ok);
    assert(resumed.record.token == acquired.record.token);
    assert(resumed.record.expiresAtMs >= acquired.record.expiresAtMs);

    ComputerCpp::ControlSessionAcquireOptions contender = options;
    contender.owner = "other-owner";
    auto busy = ComputerCpp::AcquireOrResumeControlSession(contender);
    assert(!busy.ok);
    assert(busy.code == "control_session_busy");
    assert(busy.holder.has_value());
    assert(busy.holder->token == acquired.record.token);

    auto events = ComputerCpp::RecentControlSessionEvents(options.scope, 10);
    bool sawResumed = false;
    for (const auto& event : events) {
        sawResumed = sawResumed || event.event == "resumed";
    }
    assert(sawResumed);

    auto released = ComputerCpp::ReleaseControlSession(acquired.record.token);
    assert(released.ok);
    assert(released.released);
}

void TestControlSessionExpiryAndWait() {
    ComputerCpp::ControlSessionAcquireOptions options;
    options.scope = "desktop:test-expiry";
    options.daemonSession = "unit";
    options.owner = "core-test";
    options.purpose = "expiry-test";
    options.ttlMs = 1000;
    auto first = ComputerCpp::AcquireControlSession(options);
    assert(first.ok);

    ComputerCpp::ControlSessionAcquireOptions contender = options;
    contender.owner = "contender";
    contender.waitMs = 150;
    auto busy = ComputerCpp::AcquireControlSession(contender);
    assert(!busy.ok);
    assert(busy.code == "control_session_busy");
    assert(busy.holder.has_value());
    assert(busy.holder->token == first.record.token);

    std::this_thread::sleep_for(std::chrono::milliseconds(1150));
    contender.waitMs = 1000;
    auto second = ComputerCpp::AcquireControlSession(contender);
    assert(second.ok);
    assert(second.record.owner == "contender");
    assert(second.record.token != first.record.token);

    auto stale = ComputerCpp::ValidateControlSession(first.record.token, "desktop:test-expiry");
    assert(!stale.ok);
    assert(stale.code == "control_session_not_active");

    auto renewed = ComputerCpp::RenewControlSession(second.record.token, 1000);
    assert(renewed.ok);
    assert(renewed.record.expiresAtMs >= second.record.expiresAtMs);

    auto released = ComputerCpp::ReleaseControlSession(second.record.token);
    assert(released.ok);
    assert(released.released);
}

void TestControlSessionValidationFailures() {
    ComputerCpp::ControlSessionAcquireOptions invalid;
    invalid.scope = "bad scope";
    auto badScope = ComputerCpp::AcquireControlSession(invalid);
    assert(!badScope.ok);
    assert(badScope.code == "invalid_control_scope");

    auto renewMissing = ComputerCpp::RenewControlSession("", 1000);
    assert(!renewMissing.ok);
    assert(renewMissing.code == "control_session_required");

    auto releaseMissing = ComputerCpp::ReleaseControlSession("");
    assert(!releaseMissing.ok);
    assert(releaseMissing.code == "control_session_required");

    auto validateMissing = ComputerCpp::ValidateControlSession("", "desktop:local");
    assert(!validateMissing.ok);
    assert(validateMissing.code == "control_session_required");
}

void TestControlSessionJsonFallsBackForMalformedMetadata() {
    ComputerCpp::ControlSessionEvent event;
    event.token = "1234567890";
    event.scope = "desktop:test-json";
    event.metadataJson = "{not-json";
    auto eventJson = ComputerCpp::ControlSessionEventToJson(event);
    assert(eventJson["token"] == "12345678");
    assert(eventJson["metadata"]["raw"] == "{not-json");

    ComputerCpp::ControlSessionResource resource;
    resource.token = "abcdef1234";
    resource.scope = "desktop:test-json";
    resource.metadataJson = "[unterminated";
    auto resourceJson = ComputerCpp::ControlSessionResourceToJson(resource);
    assert(resourceJson["token"] == "abcdef12");
    assert(resourceJson["metadata"]["raw"] == "[unterminated");
}

void TestControlSessionPrometheusEscapesLabels() {
    ComputerCpp::ControlSessionAcquireOptions options;
    options.scope = "desktop:test-prometheus-escape";
    options.daemonSession = "unit";
    options.owner = R"(owner "quoted" \ path)";
    options.purpose = "line one\nline two";
    options.ttlMs = 5000;

    auto acquired = ComputerCpp::AcquireControlSession(options);
    assert(acquired.ok);

    auto prometheus = ComputerCpp::ControlSessionPrometheus(options.scope, 60000, 60000);
    assert(prometheus.find("owner=\"owner \\\"quoted\\\" \\\\ path\"") != std::string::npos);
    assert(prometheus.find("purpose=\"line one line two\"") != std::string::npos);
    assert(prometheus.find("computer_cpp_control_session_seconds_until_expiry") != std::string::npos);

    auto released = ComputerCpp::ReleaseControlSession(acquired.record.token);
    assert(released.ok);
    assert(released.released);
}

void TestControlSessionMaxRuntimeMetricsAndEvents() {
    ComputerCpp::ControlSessionAcquireOptions options;
    options.scope = "desktop:test-metrics";
    options.daemonSession = "unit";
    options.owner = "metrics-owner";
    options.purpose = "metrics-test";
    options.ttlMs = 5000;
    options.maxRuntimeMs = 3000;
    auto acquired = ComputerCpp::AcquireControlSession(options);
    assert(acquired.ok);
    assert(acquired.record.maxRuntimeMs == 3000);
    assert(acquired.record.maxExpiresAtMs > 0);
    assert(acquired.record.expiresAtMs <= acquired.record.maxExpiresAtMs);

    ComputerCpp::RegisterControlSessionResource(acquired.record.token, "window", "fixture-window-1", "Fixture Window", {
        {"title", "Fixture"}
    });
    auto metrics = ComputerCpp::ControlSessionMetricsJson("desktop:test-metrics", 60000, 60000, 20);
    assert(metrics["available"] == false);
    assert(metrics["active"]["owner"] == "metrics-owner");
    assert(metrics["active"]["purpose"] == "metrics-test");
    assert(metrics["active"]["maxRuntimeMs"] == 3000);
    assert(metrics["active"]["activeResourceCount"] == 1);
    assert(metrics["eventCounts"]["acquired"].get<int>() >= 1);
    assert(metrics["eventCounts"]["resource_acquired"].get<int>() >= 1);

    auto prometheus = ComputerCpp::ControlSessionPrometheus("desktop:test-metrics", 60000, 60000);
    assert(prometheus.find("computer_cpp_control_session_active") != std::string::npos);
    assert(prometheus.find("owner=\"metrics-owner\"") != std::string::npos);
    assert(prometheus.find("computer_cpp_control_session_active_resources") != std::string::npos);
    assert(CountOccurrences(prometheus, "# HELP computer_cpp_control_session_events_total") == 1);

    auto renewed = ComputerCpp::RenewControlSession(acquired.record.token, 5000);
    assert(renewed.ok);
    assert(renewed.record.expiresAtMs <= renewed.record.maxExpiresAtMs);

    ComputerCpp::ReleaseControlSessionResource(acquired.record.token, "window", "fixture-window-1");
    auto released = ComputerCpp::ReleaseControlSession(acquired.record.token);
    assert(released.ok);
    assert(released.released);

    auto events = ComputerCpp::RecentControlSessionEvents("desktop:test-metrics", 20);
    bool sawRenewed = false;
    bool sawReleased = false;
    bool sawResourceReleased = false;
    for (const auto& event : events) {
        sawRenewed = sawRenewed || event.event == "renewed";
        sawReleased = sawReleased || event.event == "released";
        sawResourceReleased = sawResourceReleased || event.event == "resource_released";
    }
    assert(sawRenewed);
    assert(sawReleased);
    assert(sawResourceReleased);

    auto afterRelease = ComputerCpp::ControlSessionMetricsJson("desktop:test-metrics", 60000, 60000, 20);
    assert(afterRelease["available"] == true);
}

void TestControlSessionMaxRuntimeExpiresLease() {
    ComputerCpp::ControlSessionAcquireOptions options;
    options.scope = "desktop:test-max-runtime";
    options.daemonSession = "unit";
    options.owner = "max-runtime-owner";
    options.purpose = "max-runtime-test";
    options.ttlMs = 5000;
    options.maxRuntimeMs = 1000;
    auto acquired = ComputerCpp::AcquireControlSession(options);
    assert(acquired.ok);
    assert(acquired.record.maxRuntimeMs == 1000);
    ComputerCpp::RegisterControlSessionResource(acquired.record.token, "window", "expired-window-1", "Expired Fixture", {
        {"title", "Expired Fixture"}
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(1150));
    auto expired = ComputerCpp::ValidateControlSession(acquired.record.token, "desktop:test-max-runtime");
    assert(!expired.ok);
    assert(expired.code == "control_session_not_active" || expired.code == "control_session_expired");
    auto expiredResources = ComputerCpp::ExpiredControlSessionResources("desktop:test-max-runtime");
    assert(expiredResources.size() == 1);
    assert(expiredResources[0].resourceId == "expired-window-1");
    ComputerCpp::AbandonControlSessionResource(expiredResources[0].token, "window", expiredResources[0].resourceId, "unit cleanup failed");
    assert(ComputerCpp::ExpiredControlSessionResources("desktop:test-max-runtime").empty());
    auto events = ComputerCpp::RecentControlSessionEvents("desktop:test-max-runtime", 5);
    bool sawMaxExpiry = false;
    bool sawResourceAbandoned = false;
    for (const auto& event : events) {
        sawMaxExpiry = sawMaxExpiry || (event.event == "expired" && event.code == "max_runtime");
        sawResourceAbandoned = sawResourceAbandoned || event.event == "resource_abandoned";
    }
    assert(sawMaxExpiry);
    assert(sawResourceAbandoned);
}

void TestControlSessionConcurrentAcquireSingleWinner() {
    std::atomic<int> winners = 0;
    std::atomic<int> busy = 0;
    std::string winningToken;
    std::mutex tokenMutex;
    std::vector<std::thread> threads;

    for (int i = 0; i < 12; ++i) {
        threads.emplace_back([i, &winners, &busy, &winningToken, &tokenMutex]() {
            ComputerCpp::ControlSessionAcquireOptions options;
            options.scope = "desktop:test-race";
            options.daemonSession = "unit";
            options.owner = "racer-" + std::to_string(i);
            options.purpose = "concurrent-acquire";
            options.ttlMs = 5000;
            options.waitMs = 0;
            auto result = ComputerCpp::AcquireControlSession(options);
            if (result.ok) {
                winners.fetch_add(1);
                std::lock_guard<std::mutex> lock(tokenMutex);
                winningToken = result.record.token;
            } else if (result.code == "control_session_busy") {
                busy.fetch_add(1);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    assert(winners.load() == 1);
    assert(busy.load() == 11);
    assert(!winningToken.empty());
    auto valid = ComputerCpp::ValidateControlSession(winningToken, "desktop:test-race");
    assert(valid.ok);
    auto released = ComputerCpp::ReleaseControlSession(winningToken);
    assert(released.ok);
    assert(released.released);
}

} // namespace

namespace ComputerCpp::Tests {

void RunControlSessionTests() {
    TestControlSessionLeases();
    TestControlSessionAcquireOrResumeReusesMatchingLease();
    TestControlSessionExpiryAndWait();
    TestControlSessionValidationFailures();
    TestControlSessionJsonFallsBackForMalformedMetadata();
    TestControlSessionPrometheusEscapesLabels();
    TestControlSessionMaxRuntimeMetricsAndEvents();
    TestControlSessionMaxRuntimeExpiresLease();
    TestControlSessionConcurrentAcquireSingleWinner();
}

}
