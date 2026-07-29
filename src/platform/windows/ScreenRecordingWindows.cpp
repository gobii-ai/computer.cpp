#include "computer_cpp/ScreenRecording.h"

#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

namespace ComputerCpp::Platform {
namespace {

template <typename T>
void ReleaseCom(T*& value) {
    if (value) {
        value->Release();
        value = nullptr;
    }
}

std::string HResultText(const std::string& action, HRESULT hr) {
    std::ostringstream out;
    out << action << " (HRESULT 0x" << std::hex << std::uppercase
        << static_cast<unsigned long>(hr) << ")";
    return out.str();
}

struct WindowsRecordingState {
    explicit WindowsRecordingState(ScreenRecordingOptions recordingOptions)
        : options(std::move(recordingOptions)) {}

    ScreenRecordingOptions options;
    std::mutex mutex;
    std::condition_variable changed;
    bool started = false;
    bool failed = false;
    bool stopRequested = false;
    bool finished = false;
    bool finalized = false;
    std::string error;
};

bool StopRequested(WindowsRecordingState& state) {
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.stopRequested;
}

void SignalStarted(WindowsRecordingState& state) {
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.started = true;
    }
    state.changed.notify_all();
}

void SignalFinished(
    WindowsRecordingState& state,
    bool finalized,
    const std::string& error
) {
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.finalized = finalized;
        state.finished = true;
        if (!error.empty()) {
            state.failed = true;
            state.error = error;
        }
    }
    state.changed.notify_all();
}

bool WaitForNextFrame(
    WindowsRecordingState& state,
    std::chrono::steady_clock::time_point deadline
) {
    std::unique_lock<std::mutex> lock(state.mutex);
    state.changed.wait_until(lock, deadline, [&state] { return state.stopRequested; });
    return !state.stopRequested;
}

void DrawCursor(
    const ScreenRecordingOptions& options,
    HDC target,
    int sourceLeft,
    int sourceTop,
    int sourceWidth,
    int sourceHeight,
    int targetWidth,
    int targetHeight
) {
    if (!options.includeCursor) {
        return;
    }
    CURSORINFO cursor{};
    cursor.cbSize = sizeof(cursor);
    if (!GetCursorInfo(&cursor) || !(cursor.flags & CURSOR_SHOWING) || !cursor.hCursor) {
        return;
    }
    ICONINFO icon{};
    if (!GetIconInfo(cursor.hCursor, &icon)) {
        return;
    }
    const double scaleX = static_cast<double>(targetWidth) / sourceWidth;
    const double scaleY = static_cast<double>(targetHeight) / sourceHeight;
    const int x = static_cast<int>(std::llround(
        (cursor.ptScreenPos.x - sourceLeft - static_cast<LONG>(icon.xHotspot)) * scaleX));
    const int y = static_cast<int>(std::llround(
        (cursor.ptScreenPos.y - sourceTop - static_cast<LONG>(icon.yHotspot)) * scaleY));
    const int width = std::max(
        1,
        static_cast<int>(std::llround(GetSystemMetrics(SM_CXCURSOR) * scaleX)));
    const int height = std::max(
        1,
        static_cast<int>(std::llround(GetSystemMetrics(SM_CYCURSOR) * scaleY)));
    DrawIconEx(target, x, y, cursor.hCursor, width, height, 0, nullptr, DI_NORMAL);
    if (icon.hbmMask) {
        DeleteObject(icon.hbmMask);
    }
    if (icon.hbmColor) {
        DeleteObject(icon.hbmColor);
    }
}

void RunRecording(const std::shared_ptr<WindowsRecordingState>& state) {
    HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitializeCom = SUCCEEDED(comResult);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        SignalFinished(
            *state,
            false,
            HResultText("could not initialize COM for recording", comResult));
        return;
    }
    HRESULT result = MFStartup(MF_VERSION);
    if (FAILED(result)) {
        if (uninitializeCom) {
            CoUninitialize();
        }
        SignalFinished(
            *state,
            false,
            HResultText("could not initialize Media Foundation", result));
        return;
    }

    IMFAttributes* attributes = nullptr;
    IMFSinkWriter* writer = nullptr;
    IMFMediaType* outputType = nullptr;
    IMFMediaType* inputType = nullptr;
    IMFSample* sample = nullptr;
    IMFMediaBuffer* buffer = nullptr;
    HDC screenDc = nullptr;
    HDC memoryDc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    void* pixels = nullptr;
    std::string failure;

    const int sourceLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int sourceTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int sourceWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int sourceHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (sourceWidth <= 0 || sourceHeight <= 0) {
        failure = "Windows virtual desktop has invalid dimensions";
    }

    int targetWidth = sourceWidth;
    int targetHeight = sourceHeight;
    const int largest = std::max(sourceWidth, sourceHeight);
    if (failure.empty() && state->options.maxDimension > 0 &&
        largest > state->options.maxDimension) {
        const double scale = static_cast<double>(state->options.maxDimension) / largest;
        targetWidth = std::max(2, static_cast<int>(std::llround(sourceWidth * scale)));
        targetHeight = std::max(2, static_cast<int>(std::llround(sourceHeight * scale)));
    }
    targetWidth = std::max(2, targetWidth & ~1);
    targetHeight = std::max(2, targetHeight & ~1);

    if (failure.empty()) {
        result = MFCreateAttributes(&attributes, 2);
        if (SUCCEEDED(result)) {
            result = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        }
        if (SUCCEEDED(result)) {
            result = attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
        }
        if (SUCCEEDED(result)) {
            const std::wstring path = state->options.outputPath.wstring();
            result = MFCreateSinkWriterFromURL(path.c_str(), nullptr, attributes, &writer);
        }
        if (FAILED(result)) {
            failure = HResultText("could not create Media Foundation MP4 writer", result);
        }
    }

    DWORD streamIndex = 0;
    const int frameRate = std::max(1, state->options.framesPerSecond);
    if (failure.empty()) {
        result = MFCreateMediaType(&outputType);
        if (SUCCEEDED(result)) result = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(result)) result = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        if (SUCCEEDED(result)) result = outputType->SetUINT32(MF_MT_AVG_BITRATE, 4000000);
        if (SUCCEEDED(result)) result = outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (SUCCEEDED(result)) result = MFSetAttributeSize(outputType, MF_MT_FRAME_SIZE, targetWidth, targetHeight);
        if (SUCCEEDED(result)) result = MFSetAttributeRatio(outputType, MF_MT_FRAME_RATE, frameRate, 1);
        if (SUCCEEDED(result)) result = MFSetAttributeRatio(outputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (SUCCEEDED(result)) result = writer->AddStream(outputType, &streamIndex);
        if (FAILED(result)) {
            failure = HResultText("could not configure Media Foundation H.264 output", result);
        }
    }

    if (failure.empty()) {
        result = MFCreateMediaType(&inputType);
        if (SUCCEEDED(result)) result = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(result)) result = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(result)) result = inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (SUCCEEDED(result)) result = MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, targetWidth, targetHeight);
        if (SUCCEEDED(result)) result = MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, frameRate, 1);
        if (SUCCEEDED(result)) result = MFSetAttributeRatio(inputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (SUCCEEDED(result)) result = writer->SetInputMediaType(streamIndex, inputType, nullptr);
        if (SUCCEEDED(result)) result = writer->BeginWriting();
        if (FAILED(result)) {
            failure = HResultText("could not start Media Foundation H.264 encoder", result);
        }
    }

    if (failure.empty()) {
        screenDc = GetDC(nullptr);
        memoryDc = screenDc ? CreateCompatibleDC(screenDc) : nullptr;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = targetWidth;
        info.bmiHeader.biHeight = -targetHeight;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap = screenDc
            ? CreateDIBSection(screenDc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0)
            : nullptr;
        if (!screenDc || !memoryDc || !bitmap || !pixels) {
            failure = "could not create Windows desktop capture surface";
        } else {
            oldBitmap = SelectObject(memoryDc, bitmap);
            SetStretchBltMode(memoryDc, HALFTONE);
        }
    }

    const LONGLONG frameDuration = 10000000LL / frameRate;
    uint64_t frameNumber = 0;
    const auto recordingStartedAt = std::chrono::steady_clock::now();
    auto nextFrame = recordingStartedAt;
    while (failure.empty() && !StopRequested(*state)) {
        SetBrushOrgEx(memoryDc, 0, 0, nullptr);
        if (!StretchBlt(
                memoryDc, 0, 0, targetWidth, targetHeight,
                screenDc, sourceLeft, sourceTop, sourceWidth, sourceHeight,
                SRCCOPY | CAPTUREBLT)) {
            failure = "Windows desktop capture failed";
            break;
        }
        DrawCursor(
            state->options,
            memoryDc,
            sourceLeft,
            sourceTop,
            sourceWidth,
            sourceHeight,
            targetWidth,
            targetHeight);

        const DWORD byteCount = static_cast<DWORD>(
            static_cast<uint64_t>(targetWidth) * targetHeight * 4);
        result = MFCreateMemoryBuffer(byteCount, &buffer);
        BYTE* destination = nullptr;
        DWORD capacity = 0;
        if (SUCCEEDED(result)) result = buffer->Lock(&destination, &capacity, nullptr);
        if (SUCCEEDED(result) && capacity >= byteCount) {
            std::memcpy(destination, pixels, byteCount);
            buffer->Unlock();
            result = buffer->SetCurrentLength(byteCount);
        } else if (SUCCEEDED(result)) {
            buffer->Unlock();
            result = E_FAIL;
        }
        if (SUCCEEDED(result)) result = MFCreateSample(&sample);
        if (SUCCEEDED(result)) result = sample->AddBuffer(buffer);
        const LONGLONG sampleTime = static_cast<LONGLONG>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - recordingStartedAt).count() / 100);
        if (SUCCEEDED(result)) result = sample->SetSampleTime(sampleTime);
        if (SUCCEEDED(result)) result = sample->SetSampleDuration(frameDuration);
        if (SUCCEEDED(result)) result = writer->WriteSample(streamIndex, sample);
        ReleaseCom(sample);
        ReleaseCom(buffer);
        if (FAILED(result)) {
            failure = HResultText(
                "Media Foundation could not encode a screen frame",
                result);
            break;
        }
        if (frameNumber++ == 0) {
            SignalStarted(*state);
        }
        nextFrame += std::chrono::microseconds(1000000 / frameRate);
        nextFrame = std::max(nextFrame, std::chrono::steady_clock::now());
        if (!WaitForNextFrame(*state, nextFrame)) {
            break;
        }
    }

    bool finalized = false;
    if (writer) {
        result = writer->Finalize();
        finalized = SUCCEEDED(result);
        if (!finalized && failure.empty()) {
            failure = HResultText("Media Foundation could not finalize the MP4", result);
        }
    }

    if (oldBitmap && memoryDc) SelectObject(memoryDc, oldBitmap);
    if (bitmap) DeleteObject(bitmap);
    if (memoryDc) DeleteDC(memoryDc);
    if (screenDc) ReleaseDC(nullptr, screenDc);
    ReleaseCom(buffer);
    ReleaseCom(sample);
    ReleaseCom(inputType);
    ReleaseCom(outputType);
    ReleaseCom(writer);
    ReleaseCom(attributes);
    MFShutdown();
    if (uninitializeCom) {
        CoUninitialize();
    }
    SignalFinished(*state, finalized && failure.empty(), failure);
}

class WindowsScreenRecordingSession final : public ScreenRecordingSession {
public:
    explicit WindowsScreenRecordingSession(ScreenRecordingOptions options)
        : state_(std::make_shared<WindowsRecordingState>(std::move(options))) {}

    ~WindowsScreenRecordingSession() override {
        std::string ignored;
        Stop(10000, &ignored);
    }

    bool Start(int timeoutMs, std::string* error) {
        const auto state = state_;
        worker_ = std::thread([state] { RunRecording(state); });
        std::unique_lock<std::mutex> lock(state_->mutex);
        if (!state_->changed.wait_for(
                lock,
                std::chrono::milliseconds(std::max(1, timeoutMs)),
                [this] { return state_->started || state_->finished; })) {
            state_->stopRequested = true;
            state_->changed.notify_all();
            lock.unlock();
            if (worker_.joinable()) {
                worker_.detach();
            }
            stopAttempted_ = true;
            stopError_ = "timed out starting Windows screen recording";
            if (error) {
                *error = stopError_;
            }
            return false;
        }
        if (state_->finished && !state_->started) {
            const std::string failure = state_->error.empty()
                ? "Windows screen recording ended before its first frame"
                : state_->error;
            lock.unlock();
            if (worker_.joinable()) {
                worker_.join();
            }
            stopAttempted_ = true;
            stopError_ = failure;
            if (error) {
                *error = failure;
            }
            return false;
        }
        return true;
    }

    bool Stop(int timeoutMs, std::string* error) override {
        if (stopAttempted_) {
            if (error && !stopError_.empty()) {
                *error = stopError_;
            }
            return stopResult_;
        }
        stopAttempted_ = true;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->stopRequested = true;
        }
        state_->changed.notify_all();

        bool timedOut = false;
        {
            std::unique_lock<std::mutex> lock(state_->mutex);
            if (!state_->finished) {
                timedOut = !state_->changed.wait_for(
                    lock,
                    std::chrono::milliseconds(std::max(1, timeoutMs)),
                    [this] { return state_->finished; });
            }
        }
        if (worker_.joinable()) {
            if (timedOut) {
                worker_.detach();
            } else {
                worker_.join();
            }
        }

        std::lock_guard<std::mutex> lock(state_->mutex);
        if (timedOut) {
            stopError_ = "timed out finalizing Windows screen recording";
        } else {
            stopError_ = state_->error;
        }
        stopResult_ =
            !timedOut && state_->started && !state_->failed && state_->finalized;
        if (error && !stopError_.empty()) {
            *error = stopError_;
        }
        return stopResult_;
    }

private:
    std::shared_ptr<WindowsRecordingState> state_;
    std::thread worker_;
    bool stopAttempted_ = false;
    bool stopResult_ = false;
    std::string stopError_;
};

} // namespace

std::unique_ptr<ScreenRecordingSession> StartScreenRecording(
    const ScreenRecordingOptions& options,
    int timeoutMs,
    std::string* error) {
    auto session = std::make_unique<WindowsScreenRecordingSession>(options);
    if (!session->Start(timeoutMs, error)) {
        return nullptr;
    }
    return session;
}

} // namespace ComputerCpp::Platform
