#include "computer_cpp/CommandRecording.h"

#include "computer_cpp/AppPaths.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ComputerCpp {
namespace {

constexpr int kFramesPerSecond = 15;
constexpr int kMaxDimension = 1920;
constexpr int kStartTimeoutMs = 5000;
constexpr int kStopTimeoutMs = 10000;
constexpr auto kCleanupInterval = std::chrono::hours(1);
constexpr auto kMaximumActiveAge = std::chrono::hours(24);

std::mutex gCleanupMutex;
std::chrono::steady_clock::time_point gLastCleanup;
std::mutex gFactoryMutex;
ScreenRecordingFactory gTestingFactory;
std::atomic<uint64_t> gTempFileSequence{0};

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::tm UtcNow() {
    const std::time_t value = std::time(nullptr);
    std::tm result{};
#if defined(_WIN32)
    gmtime_s(&result, &value);
#else
    gmtime_r(&value, &result);
#endif
    return result;
}

std::string NowIsoUtc() {
    const std::tm now = UtcNow();
    std::ostringstream out;
    out << std::put_time(&now, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string DatePart() {
    const std::tm now = UtcNow();
    std::ostringstream out;
    out << std::put_time(&now, "%Y-%m-%d");
    return out.str();
}

std::string TimestampPart() {
    const std::tm now = UtcNow();
    std::ostringstream out;
    out << std::put_time(&now, "%Y%m%dT%H%M%SZ");
    return out.str();
}

std::string SanitizePart(const std::string& value) {
    std::string out;
    out.reserve(std::min<size_t>(value.size(), 80));
    for (unsigned char ch : value) {
        const bool asciiAlphaNumeric =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9');
        if (asciiAlphaNumeric || ch == '-' || ch == '_') {
            out.push_back(static_cast<char>(ch));
        } else if (ch == '.' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            out.push_back('-');
        }
        if (out.size() >= 80) {
            break;
        }
    }
    return out.empty() ? "command" : out;
}

std::string GenerateRecordingId() {
    std::random_device rd;
    std::mt19937_64 random(rd());
    std::ostringstream out;
    out << "rec_" << std::hex << NowMs() << "_" << (random() & 0xffffffu);
    return out.str();
}

long long CurrentPid() {
#if defined(_WIN32)
    return static_cast<long long>(GetCurrentProcessId());
#else
    return static_cast<long long>(getpid());
#endif
}

bool ProcessAlive(long long pid) {
    if (pid <= 0) {
        return false;
    }
#if defined(_WIN32)
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process) {
        return false;
    }
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
#else
    if (::kill(static_cast<pid_t>(pid), 0) == 0) {
        return true;
    }
    return errno == EPERM;
#endif
}

bool WriteJsonAtomic(const fs::path& path, const json& value) {
    fs::path temp;
    try {
        EnsureDirectory(path.parent_path());
        temp = path;
        temp += "." + std::to_string(CurrentPid()) + "." +
            std::to_string(gTempFileSequence.fetch_add(1, std::memory_order_relaxed)) +
            ".tmp";
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            if (!file) {
                return false;
            }
            file << value.dump(2) << "\n";
            if (!file.good()) {
                file.close();
                std::error_code removeError;
                fs::remove(temp, removeError);
                return false;
            }
        }
        std::error_code ec;
#if defined(_WIN32)
        const std::wstring source = temp.wstring();
        const std::wstring destination = path.wstring();
        if (!MoveFileExW(
                source.c_str(),
                destination.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            fs::remove(temp, ec);
            return false;
        }
#else
        fs::rename(temp, path, ec);
        if (ec) {
            fs::remove(temp, ec);
            return false;
        }
#endif
        return true;
    } catch (...) {
        std::error_code ec;
        if (!temp.empty()) {
            fs::remove(temp, ec);
        }
        return false;
    }
}

std::optional<json> ReadJson(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    json value = json::parse(file, nullptr, false);
    if (value.is_discarded() || !value.is_object()) {
        return std::nullopt;
    }
    return value;
}

bool OlderThan(const fs::path& path, std::chrono::hours age) {
    std::error_code ec;
    const auto written = fs::last_write_time(path, ec);
    return !ec && fs::file_time_type::clock::now() - written > age;
}

bool MarkerIsStale(const json& marker) {
    const int64_t startedAtMs = marker.value("startedAtMs", 0LL);
    const bool implausiblyOld =
        startedAtMs > 0 && NowMs() - startedAtMs >
            std::chrono::duration_cast<std::chrono::milliseconds>(
                kMaximumActiveAge).count();
    return implausiblyOld || !ProcessAlive(marker.value("pid", 0LL));
}

void MarkInterruptedFromMarker(const fs::path& markerPath, const json& marker) {
    const std::string sidecar = marker.value("sidecarPath", "");
    if (!sidecar.empty()) {
        auto metadata = ReadJson(sidecar);
        if (metadata && ((*metadata).value("status", "") == "starting" ||
                         (*metadata).value("status", "") == "recording")) {
            (*metadata)["status"] = "interrupted";
            (*metadata)["finishedAt"] = NowIsoUtc();
            (*metadata)["durationMs"] = std::max<int64_t>(
                0, NowMs() - marker.value("startedAtMs", NowMs()));
            (*metadata)["error"] = "recording process exited before finalization";
            (*metadata)["commandStatus"] = "interrupted";
            metadata->erase("startedAtMs");
            WriteJsonAtomic(sidecar, *metadata);
            const std::string mirror = marker.value("statusMirrorPath", "");
            if (!mirror.empty()) {
                WriteJsonAtomic(mirror, *metadata);
            }
        }
    }
    std::error_code ec;
    fs::remove(markerPath, ec);
}

void CleanupStaleMarkers() {
    const fs::path activeDir = RecordingDir() / ".active";
    std::error_code ec;
    if (!fs::exists(activeDir, ec)) {
        return;
    }
    fs::directory_iterator iterator(activeDir, ec);
    const fs::directory_iterator end;
    while (!ec && iterator != end) {
        const fs::directory_entry entry = *iterator;
        iterator.increment(ec);
        std::error_code typeError;
        if (!entry.is_regular_file(typeError)) {
            continue;
        }
        auto marker = ReadJson(entry.path());
        if (!marker || MarkerIsStale(*marker)) {
            MarkInterruptedFromMarker(entry.path(), marker.value_or(json::object()));
        }
    }
}

void CleanupExpiredRecordingsImpl(int retentionDays) {
    CleanupStaleMarkers();
    const fs::path root = RecordingDir();
    std::set<std::string> activeArtifacts;
    const fs::path activeDir = root / ".active";
    std::error_code ec;
    fs::directory_iterator activeIterator(activeDir, ec);
    const fs::directory_iterator directoryEnd;
    while (!ec && activeIterator != directoryEnd) {
        const fs::directory_entry entry = *activeIterator;
        activeIterator.increment(ec);
        std::error_code typeError;
        if (!entry.is_regular_file(typeError)) {
            continue;
        }
        auto marker = ReadJson(entry.path());
        if (marker) {
            for (const char* key : {"path", "partialPath", "sidecarPath"}) {
                const std::string artifactPath = marker->value(key, "");
                if (!artifactPath.empty()) {
                    activeArtifacts.insert(
                        fs::path(artifactPath).lexically_normal().string());
                }
            }
        }
    }
    const auto retention = std::chrono::hours(24LL * std::max(0, retentionDays));
    const auto partialRetention = std::chrono::hours(24);
    std::vector<fs::path> expiredPaths;
    ec.clear();
    fs::recursive_directory_iterator iterator(root, ec);
    const fs::recursive_directory_iterator recursiveEnd;
    while (!ec && iterator != recursiveEnd) {
        const fs::directory_entry entry = *iterator;
        iterator.increment(ec);
        std::error_code typeError;
        if (!entry.is_regular_file(typeError)) {
            continue;
        }
        const fs::path path = entry.path();
        if (path.parent_path().filename() == ".active") {
            continue;
        }
        const std::string filename = path.filename().string();
        const bool partial = filename.find(".partial.mp4") != std::string::npos;
        const bool active =
            activeArtifacts.contains(path.lexically_normal().string());
        const bool inactivePartial = partial && !active;
        if ((inactivePartial && OlderThan(path, partialRetention)) ||
            (!partial && !active && retentionDays >= 0 && OlderThan(path, retention))) {
            expiredPaths.push_back(path);
        }
    }
    for (const fs::path& path : expiredPaths) {
        fs::remove(path, ec);
        ec.clear();
    }
}

bool CleanupStampIsRecent(const fs::path& stampPath) {
    std::error_code ec;
    const auto written = fs::last_write_time(stampPath, ec);
    return !ec && fs::file_time_type::clock::now() - written < kCleanupInterval;
}

void TouchCleanupStamp(const fs::path& stampPath) {
    std::ofstream stamp(stampPath, std::ios::binary | std::ios::trunc);
    if (stamp) {
        stamp << CurrentPid() << "\n";
    }
}

} // namespace

std::string NewCommandRecordingId() {
    return GenerateRecordingId();
}

void SetScreenRecordingFactoryForTesting(ScreenRecordingFactory factory) {
    std::lock_guard<std::mutex> lock(gFactoryMutex);
    gTestingFactory = std::move(factory);
}

void CleanupExpiredRecordings(int retentionDays) {
    std::lock_guard<std::mutex> lock(gCleanupMutex);
    const auto now = std::chrono::steady_clock::now();
    if (gLastCleanup.time_since_epoch().count() != 0 &&
        now - gLastCleanup < kCleanupInterval) {
        return;
    }

    const fs::path root = RecordingDir();
    EnsureDirectory(root);
    const fs::path stampPath = root / ".last-cleanup";
    if (CleanupStampIsRecent(stampPath)) {
        gLastCleanup = now;
        return;
    }

    const fs::path lockPath = root / ".cleanup-lock";
    std::error_code ec;
    if (!fs::create_directory(lockPath, ec)) {
        if (!OlderThan(lockPath, kCleanupInterval)) {
            return;
        }
        fs::remove(lockPath, ec);
        ec.clear();
        if (!fs::create_directory(lockPath, ec)) {
            return;
        }
    }
    if (CleanupStampIsRecent(stampPath)) {
        fs::remove(lockPath, ec);
        gLastCleanup = now;
        return;
    }

    CleanupExpiredRecordingsImpl(retentionDays);
    TouchCleanupStamp(stampPath);
    fs::remove(lockPath, ec);
    gLastCleanup = now;
}

size_t ActiveRecordingCount() {
    const fs::path activeDir = RecordingDir() / ".active";
    std::error_code ec;
    if (!fs::exists(activeDir, ec)) {
        return 0;
    }
    size_t count = 0;
    fs::directory_iterator iterator(activeDir, ec);
    const fs::directory_iterator end;
    while (!ec && iterator != end) {
        const fs::directory_entry entry = *iterator;
        iterator.increment(ec);
        std::error_code typeError;
        if (entry.is_regular_file(typeError)) {
            const auto marker = ReadJson(entry.path());
            if (!marker || MarkerIsStale(*marker)) {
                continue;
            }
            ++count;
        }
    }
    return count;
}

void ResetRecordingCleanupForTesting() {
    std::lock_guard<std::mutex> lock(gCleanupMutex);
    gLastCleanup = {};
    std::error_code ec;
    fs::remove(RecordingDir() / ".last-cleanup", ec);
    fs::remove(RecordingDir() / ".cleanup-lock", ec);
}

CommandRecording::CommandRecording(CommandRecordingOptions options)
    : options_(std::move(options)), enabled_(options_.enabled) {
    metadata_ = json::object();
    if (!enabled_) {
        return;
    }

    CleanupExpiredRecordings(options_.retentionDays);
    startedAtMs_ = NowMs();
    const std::string recordingId = options_.recordingId.empty()
        ? GenerateRecordingId()
        : SanitizePart(options_.recordingId);
    const std::string appId = SanitizePart(options_.appId);
    const std::string command = SanitizePart(options_.command);
    const fs::path directory = RecordingDir() / appId / DatePart();
    EnsureDirectory(directory);

    const std::string baseName =
        TimestampPart() + "-" + command + "-" + recordingId;
    finalPath_ = directory / (baseName + ".mp4");
    partialPath_ = directory / (baseName + ".partial.mp4");
    sidecarPath_ = directory / (baseName + ".json");
    activeMarkerPath_ = RecordingDir() / ".active" / (recordingId + ".json");

    metadata_ = {
        {"recordingId", recordingId},
        {"status", "starting"},
        {"path", finalPath_.string()},
        {"startedAt", NowIsoUtc()},
        {"finishedAt", nullptr},
        {"durationMs", nullptr},
        {"error", nullptr},
        {"appId", options_.appId},
        {"command", options_.command},
        {"surface", options_.surface},
        {"commandStatus", "running"},
    };
    if (!WriteMetadata()) {
        metadata_["status"] = "failed";
        metadata_["finishedAt"] = NowIsoUtc();
        metadata_["durationMs"] = std::max<int64_t>(0, NowMs() - startedAtMs_);
        metadata_["error"] = "could not write recording metadata";
        metadata_["commandStatus"] = "failed";
        finished_ = true;
        return;
    }
    if (!WriteJsonAtomic(activeMarkerPath_, {
        {"recordingId", recordingId},
        {"pid", CurrentPid()},
        {"sidecarPath", sidecarPath_.string()},
        {"path", finalPath_.string()},
        {"partialPath", partialPath_.string()},
        {"statusMirrorPath", options_.statusMirrorPath.string()},
        {"startedAt", metadata_["startedAt"]},
        {"startedAtMs", startedAtMs_},
    })) {
        metadata_["status"] = "failed";
        metadata_["finishedAt"] = NowIsoUtc();
        metadata_["durationMs"] = std::max<int64_t>(0, NowMs() - startedAtMs_);
        metadata_["error"] = "could not create recording activity marker";
        metadata_["commandStatus"] = "failed";
        WriteMetadata();
        finished_ = true;
        return;
    }

    Platform::ScreenRecordingOptions platformOptions;
    platformOptions.outputPath = partialPath_;
    platformOptions.framesPerSecond = kFramesPerSecond;
    platformOptions.maxDimension = kMaxDimension;
    platformOptions.includeCursor = true;
    ScreenRecordingFactory factory = options_.factory;
    if (!factory) {
        std::lock_guard<std::mutex> lock(gFactoryMutex);
        factory = gTestingFactory;
    }
    if (!factory) {
        factory = Platform::StartScreenRecording;
    }
    std::string error;
    session_ = factory(platformOptions, kStartTimeoutMs, &error);
    if (!session_) {
        metadata_["status"] = "failed";
        metadata_["finishedAt"] = NowIsoUtc();
        metadata_["durationMs"] = std::max<int64_t>(0, NowMs() - startedAtMs_);
        metadata_["error"] = error.empty() ? "native screen recording could not start" : error;
        WriteMetadata();
        RemoveActiveMarker();
        finished_ = true;
        return;
    }
    metadata_["status"] = "recording";
    WriteMetadata();
}

CommandRecording::~CommandRecording() {
    if (enabled_ && !finished_) {
        FinishInternal("interrupted", true);
    }
}

bool CommandRecording::enabled() const {
    return enabled_;
}

void CommandRecording::Finish(const std::string& commandStatus) {
    if (enabled_ && finished_) {
        metadata_["commandStatus"] = commandStatus;
        WriteMetadata();
        return;
    }
    FinishInternal(commandStatus, false);
}

const json& CommandRecording::metadata() const {
    return metadata_;
}

bool CommandRecording::WriteMetadata() {
    if (!enabled_) {
        return true;
    }
    json publicMetadata = metadata_;
    bool ok = WriteJsonAtomic(sidecarPath_, publicMetadata);
    if (!options_.statusMirrorPath.empty()) {
        ok = WriteJsonAtomic(options_.statusMirrorPath, publicMetadata) && ok;
    }
    if (!ok) {
        std::cerr << "computer.cpp: could not write recording metadata for "
                  << metadata_.value("recordingId", "unknown") << "\n";
    }
    return ok;
}

void CommandRecording::RemoveActiveMarker() {
    if (activeMarkerPath_.empty()) {
        return;
    }
    std::error_code ec;
    fs::remove(activeMarkerPath_, ec);
}

void CommandRecording::FinishInternal(const std::string& commandStatus, bool interrupted) {
    if (!enabled_ || finished_) {
        return;
    }
    finished_ = true;
    metadata_["commandStatus"] = commandStatus;
    std::string stopError;
    const bool stopped = session_ && session_->Stop(kStopTimeoutMs, &stopError);
    session_.reset();

    bool moved = false;
    if (stopped && !interrupted) {
        std::error_code ec;
        fs::rename(partialPath_, finalPath_, ec);
        moved = !ec;
        if (ec && stopError.empty()) {
            stopError = "could not finalize recording file: " + ec.message();
        }
    }

    metadata_["finishedAt"] = NowIsoUtc();
    metadata_["durationMs"] = std::max<int64_t>(0, NowMs() - startedAtMs_);
    if (interrupted) {
        metadata_["status"] = "interrupted";
        metadata_["error"] = "recording was interrupted";
    } else if (stopped && moved) {
        metadata_["status"] = "recorded";
        metadata_["error"] = nullptr;
    } else {
        metadata_["status"] = "failed";
        metadata_["error"] = stopError.empty() ? "native screen recording could not finalize" : stopError;
    }
    WriteMetadata();
    RemoveActiveMarker();
}

} // namespace ComputerCpp
