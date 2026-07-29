#include "computer_cpp/ScreenRecording.h"

namespace ComputerCpp::Platform {

std::unique_ptr<ScreenRecordingSession> StartScreenRecording(
    const ScreenRecordingOptions&,
    int,
    std::string* error) {
    if (error) {
        *error = "native app-command recording is not supported on Linux";
    }
    return nullptr;
}

} // namespace ComputerCpp::Platform
