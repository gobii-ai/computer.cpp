#pragma once

#include "computer_cpp/ScreenRecording.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace ComputerCpp {

using ScreenRecordingFactory = std::function<std::unique_ptr<Platform::ScreenRecordingSession>(
    const Platform::ScreenRecordingOptions&,
    int,
    std::string*)>;

struct CommandRecordingOptions {
    bool enabled = false;
    int retentionDays = 14;
    std::string appId;
    std::string command;
    std::string surface;
    std::string recordingId;
    std::filesystem::path statusMirrorPath;
    ScreenRecordingFactory factory;
};

class CommandRecording {
public:
    explicit CommandRecording(CommandRecordingOptions options);
    ~CommandRecording();

    CommandRecording(const CommandRecording&) = delete;
    CommandRecording& operator=(const CommandRecording&) = delete;

    bool enabled() const;
    void Finish(const std::string& commandStatus);
    const nlohmann::json& metadata() const;

private:
    bool WriteMetadata();
    void RemoveActiveMarker();
    void FinishInternal(const std::string& commandStatus, bool interrupted);

    CommandRecordingOptions options_;
    nlohmann::json metadata_;
    std::filesystem::path finalPath_;
    std::filesystem::path partialPath_;
    std::filesystem::path sidecarPath_;
    std::filesystem::path activeMarkerPath_;
    std::unique_ptr<Platform::ScreenRecordingSession> session_;
    bool enabled_ = false;
    bool finished_ = false;
    int64_t startedAtMs_ = 0;
};

void CleanupExpiredRecordings(int retentionDays);
size_t ActiveRecordingCount();
std::string NewCommandRecordingId();
void SetScreenRecordingFactoryForTesting(ScreenRecordingFactory factory);
void ResetRecordingCleanupForTesting();

} // namespace ComputerCpp
