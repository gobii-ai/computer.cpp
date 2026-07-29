#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace ComputerCpp::Platform {

struct ScreenRecordingOptions {
    std::filesystem::path outputPath;
    int framesPerSecond = 15;
    int maxDimension = 1920;
    bool includeCursor = true;
};

class ScreenRecordingSession {
public:
    virtual ~ScreenRecordingSession() = default;
    virtual bool Stop(int timeoutMs, std::string* error = nullptr) = 0;
};

std::unique_ptr<ScreenRecordingSession> StartScreenRecording(
    const ScreenRecordingOptions& options,
    int timeoutMs,
    std::string* error = nullptr);

} // namespace ComputerCpp::Platform
