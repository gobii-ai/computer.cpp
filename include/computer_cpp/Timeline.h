#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace ComputerCpp {

struct TimelineEvent {
    int64_t id = 0;
    std::string type;
    std::string app;
    std::string bundleId;
    std::string title;
    std::string paramsJson;
    int64_t startedAtMs = 0;
    int64_t endedAtMs = 0;
};

struct TimelineFrame {
    int64_t id = 0;
    int64_t eventId = 0;
    std::string label;
    std::filesystem::path path;
    int64_t capturedAtMs = 0;
    int width = 0;
    int height = 0;
};

int64_t NowMs();
void InitTimeline(const std::string& session);
int64_t BeginTimelineEvent(const std::string& session, const std::string& type, const nlohmann::json& params);
void EndTimelineEvent(const std::string& session, int64_t eventId);
std::optional<TimelineFrame> AddTimelineFrame(const std::string& session, int64_t eventId, const std::string& label);
std::vector<TimelineEvent> RecentTimelineEvents(const std::string& session, int limit);
std::vector<TimelineFrame> TimelineFramesForEvent(const std::string& session, int64_t eventId, int limit = 100);
std::optional<int64_t> LastTimelineEventId(const std::string& session);

}
