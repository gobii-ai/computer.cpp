#include "computer_cpp/Timeline.h"

#include "computer_cpp/AppPaths.h"
#include "computer_cpp/Platform.h"
#include "Sqlite.h"

#include <algorithm>
#include <chrono>
#include <sqlite3.h>

using json = nlohmann::json;

namespace ComputerCpp {
namespace {

class Db {
public:
    explicit Db(const std::string& session)
        : connection_(TimelineDbPath(session), "failed to open timeline db") {}

    sqlite3* get() { return connection_.get(); }

private:
    Sqlite::Connection connection_;
};

using Sqlite::ColumnText;
using Sqlite::Exec;
using Sqlite::Statement;

}

int64_t NowMs() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

void InitTimeline(const std::string& session) {
    Db db(session);
    Exec(db.get(), "PRAGMA journal_mode=WAL;");
    Exec(db.get(),
         "CREATE TABLE IF NOT EXISTS events ("
         "id INTEGER PRIMARY KEY AUTOINCREMENT,"
         "type TEXT NOT NULL,"
         "started_at_ms INTEGER NOT NULL,"
         "ended_at_ms INTEGER,"
         "app TEXT,"
         "bundle_id TEXT,"
         "title TEXT,"
         "params_json TEXT NOT NULL);");
    Exec(db.get(),
         "CREATE TABLE IF NOT EXISTS frames ("
         "id INTEGER PRIMARY KEY AUTOINCREMENT,"
         "event_id INTEGER NOT NULL,"
         "label TEXT NOT NULL,"
         "path TEXT NOT NULL,"
         "captured_at_ms INTEGER NOT NULL,"
         "width INTEGER,"
         "height INTEGER,"
         "FOREIGN KEY(event_id) REFERENCES events(id));");
}

int64_t BeginTimelineEvent(const std::string& session, const std::string& type, const json& params) {
    InitTimeline(session);
    Db db(session);
    Platform::AppInfo app = Platform::GetFrontmostApp();
    const char* sql = "INSERT INTO events(type, started_at_ms, app, bundle_id, params_json) VALUES (?, ?, ?, ?, ?);";
    Statement stmt(db.get(), sql);
    stmt.bindText(1, type);
    stmt.bindInt64(2, NowMs());
    stmt.bindText(3, app.name);
    stmt.bindText(4, app.bundleId);
    stmt.bindText(5, params.dump());
    stmt.expectDone();
    return sqlite3_last_insert_rowid(db.get());
}

void EndTimelineEvent(const std::string& session, int64_t eventId) {
    Db db(session);
    Statement stmt(db.get(), "UPDATE events SET ended_at_ms = ? WHERE id = ?;");
    stmt.bindInt64(1, NowMs());
    stmt.bindInt64(2, eventId);
    stmt.expectDone();
}

std::optional<TimelineFrame> AddTimelineFrame(const std::string& session, int64_t eventId, const std::string& label) {
    InitTimeline(session);
    auto frameDir = TimelineDir(session) / "frames";
    auto path = frameDir / ("event-" + std::to_string(eventId) + "-" + label + "-" + std::to_string(NowMs()) + ".png");
    if (!Platform::SaveScreenshot(path.string())) {
        return std::nullopt;
    }

    Db db(session);
    Statement stmt(db.get(), "INSERT INTO frames(event_id, label, path, captured_at_ms) VALUES (?, ?, ?, ?);");
    stmt.bindInt64(1, eventId);
    stmt.bindText(2, label);
    stmt.bindText(3, path.string());
    stmt.bindInt64(4, NowMs());
    try {
        stmt.expectDone();
    } catch (const std::exception&) {
        return std::nullopt;
    }

    TimelineFrame frame;
    frame.id = sqlite3_last_insert_rowid(db.get());
    frame.eventId = eventId;
    frame.label = label;
    frame.path = path;
    frame.capturedAtMs = NowMs();
    return frame;
}

std::vector<TimelineEvent> RecentTimelineEvents(const std::string& session, int limit) {
    InitTimeline(session);
    Db db(session);
    std::vector<TimelineEvent> events;
    Statement stmt(
        db.get(),
        "SELECT id,type,started_at_ms,coalesce(ended_at_ms,0),app,bundle_id,coalesce(title,''),params_json "
        "FROM events ORDER BY id DESC LIMIT ?;");
    stmt.bindInt(1, std::max(1, limit));
    while (stmt.step() == SQLITE_ROW) {
        TimelineEvent event;
        event.id = sqlite3_column_int64(stmt.get(), 0);
        event.type = ColumnText(stmt.get(), 1);
        event.startedAtMs = sqlite3_column_int64(stmt.get(), 2);
        event.endedAtMs = sqlite3_column_int64(stmt.get(), 3);
        event.app = ColumnText(stmt.get(), 4);
        event.bundleId = ColumnText(stmt.get(), 5);
        event.title = ColumnText(stmt.get(), 6);
        event.paramsJson = ColumnText(stmt.get(), 7);
        events.push_back(event);
    }
    return events;
}

std::vector<TimelineFrame> TimelineFramesForEvent(const std::string& session, int64_t eventId, int limit) {
    InitTimeline(session);
    Db db(session);
    std::vector<TimelineFrame> frames;
    Statement stmt(
        db.get(),
        "SELECT id,event_id,label,path,captured_at_ms,coalesce(width,0),coalesce(height,0) "
        "FROM frames WHERE event_id=? ORDER BY id LIMIT ?;");
    stmt.bindInt64(1, eventId);
    stmt.bindInt(2, std::max(1, limit));
    while (stmt.step() == SQLITE_ROW) {
        TimelineFrame frame;
        frame.id = sqlite3_column_int64(stmt.get(), 0);
        frame.eventId = sqlite3_column_int64(stmt.get(), 1);
        frame.label = ColumnText(stmt.get(), 2);
        frame.path = ColumnText(stmt.get(), 3);
        frame.capturedAtMs = sqlite3_column_int64(stmt.get(), 4);
        frame.width = sqlite3_column_int(stmt.get(), 5);
        frame.height = sqlite3_column_int(stmt.get(), 6);
        frames.push_back(frame);
    }
    return frames;
}

std::optional<int64_t> LastTimelineEventId(const std::string& session) {
    auto events = RecentTimelineEvents(session, 1);
    if (events.empty()) return std::nullopt;
    return events.front().id;
}

}
