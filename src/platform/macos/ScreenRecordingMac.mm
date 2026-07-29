#include "computer_cpp/ScreenRecording.h"

#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>

namespace ComputerCpp::Platform {

struct MacRecordingState {
    std::mutex mutex;
    std::condition_variable changed;
    bool started = false;
    bool finished = false;
    bool failed = false;
    std::string error;
};

void SetMacRecordingState(
    MacRecordingState* state,
    bool started,
    bool finished,
    const std::string& error) {
    if (!state) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->started = state->started || started;
        state->finished = state->finished || finished;
        if (!error.empty()) {
            state->failed = true;
            state->error = error;
        }
    }
    state->changed.notify_all();
}

} // namespace ComputerCpp::Platform

@interface ComputerCppRecordingDelegate : NSObject <SCRecordingOutputDelegate>
- (instancetype)initWithState:(ComputerCpp::Platform::MacRecordingState*)state;
@end

@implementation ComputerCppRecordingDelegate {
    ComputerCpp::Platform::MacRecordingState* _state;
}

- (instancetype)initWithState:(ComputerCpp::Platform::MacRecordingState*)state {
    self = [super init];
    if (self) {
        _state = state;
    }
    return self;
}

- (void)recordingOutputDidStartRecording:(SCRecordingOutput*)recordingOutput {
    (void)recordingOutput;
    ComputerCpp::Platform::SetMacRecordingState(_state, true, false, {});
}

- (void)recordingOutput:(SCRecordingOutput*)recordingOutput didFailWithError:(NSError*)error {
    (void)recordingOutput;
    const char* text = error.localizedDescription.UTF8String;
    ComputerCpp::Platform::SetMacRecordingState(
        _state, false, false, text ? std::string(text) : "ScreenCaptureKit recording failed");
}

- (void)recordingOutputDidFinishRecording:(SCRecordingOutput*)recordingOutput {
    (void)recordingOutput;
    ComputerCpp::Platform::SetMacRecordingState(_state, false, true, {});
}

@end

namespace ComputerCpp::Platform {
namespace {

std::string NSErrorText(NSError* error, const std::string& fallback) {
    if (!error) {
        return fallback;
    }
    const char* text = error.localizedDescription.UTF8String;
    return text ? std::string(text) : fallback;
}

class MacScreenRecordingSession final : public ScreenRecordingSession {
public:
    MacScreenRecordingSession(
        SCStream* stream,
        SCRecordingOutput* output,
        ComputerCppRecordingDelegate* delegate,
        std::shared_ptr<MacRecordingState> state)
        : stream_(stream),
          output_(output),
          delegate_(delegate),
          state_(std::move(state)) {}

    ~MacScreenRecordingSession() override {
        if (!stopped_) {
            std::string ignored;
            Stop(10000, &ignored);
        }
        [output_ release];
        [stream_ release];
        [delegate_ release];
    }

    MacRecordingState& state() {
        return *state_;
    }

    bool Stop(int timeoutMs, std::string* error) override {
        if (stopped_) {
            if (error && !stopError_.empty()) {
                *error = stopError_;
            }
            return stopOk_;
        }
        stopped_ = true;
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(std::max(1, timeoutMs));
        auto remainingMs = [&deadline]() {
            return std::max<int64_t>(
                0,
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now()).count());
        };

        NSError* removeError = nil;
        const bool removed = [stream_ removeRecordingOutput:output_ error:&removeError];
        if (!removed) {
            stopError_ = NSErrorText(removeError, "could not stop ScreenCaptureKit recording output");
        } else {
            std::unique_lock<std::mutex> lock(state_->mutex);
            const int64_t remaining = remainingMs();
            if (remaining <= 0 || !state_->changed.wait_for(
                    lock,
                    std::chrono::milliseconds(remaining),
                    [this] { return state_->finished || state_->failed; })) {
                stopError_ = "timed out finalizing ScreenCaptureKit recording";
            } else if (state_->failed && stopError_.empty()) {
                stopError_ = state_->error;
            }
        }

        dispatch_semaphore_t stopped = dispatch_semaphore_create(0);
        __block NSError* captureStopError = nil;
        [stream_ stopCaptureWithCompletionHandler:^(NSError* captureError) {
            if (captureError) {
                captureStopError = [captureError retain];
            }
            dispatch_semaphore_signal(stopped);
        }];
        const dispatch_time_t stopCaptureDeadline = dispatch_time(
            DISPATCH_TIME_NOW,
            std::max<int64_t>(0, remainingMs()) * NSEC_PER_MSEC);
        if (dispatch_semaphore_wait(stopped, stopCaptureDeadline) != 0 && stopError_.empty()) {
            stopError_ = "timed out stopping ScreenCaptureKit stream";
        } else if (captureStopError && stopError_.empty()) {
            stopError_ = NSErrorText(captureStopError, "could not stop ScreenCaptureKit stream");
        }
        [captureStopError release];

        stopOk_ = stopError_.empty() && state_->finished;
        if (error && !stopError_.empty()) {
            *error = stopError_;
        }
        return stopOk_;
    }

private:
    SCStream* stream_ = nil;
    SCRecordingOutput* output_ = nil;
    ComputerCppRecordingDelegate* delegate_ = nil;
    std::shared_ptr<MacRecordingState> state_;
    bool stopped_ = false;
    bool stopOk_ = false;
    std::string stopError_;
};

std::pair<size_t, size_t> RecordingDimensions(
    SCDisplay* display,
    int maxDimension) {
    CGFloat scale = 1.0;
    for (NSScreen* screen in NSScreen.screens) {
        NSNumber* number = screen.deviceDescription[@"NSScreenNumber"];
        if (number && number.unsignedIntValue == display.displayID) {
            scale = screen.backingScaleFactor;
            break;
        }
    }
    double width = std::max<NSInteger>(1, display.width) * scale;
    double height = std::max<NSInteger>(1, display.height) * scale;
    const double largest = std::max(width, height);
    if (maxDimension > 0 && largest > maxDimension) {
        const double ratio = static_cast<double>(maxDimension) / largest;
        width *= ratio;
        height *= ratio;
    }
    size_t evenWidth = std::max<size_t>(2, static_cast<size_t>(std::llround(width)) & ~size_t{1});
    size_t evenHeight = std::max<size_t>(2, static_cast<size_t>(std::llround(height)) & ~size_t{1});
    return {evenWidth, evenHeight};
}

} // namespace

std::unique_ptr<ScreenRecordingSession> StartScreenRecording(
    const ScreenRecordingOptions& options,
    int timeoutMs,
    std::string* error) {
    @autoreleasepool {
        if (@available(macOS 15.0, *)) {
            const auto startupDeadline = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(std::max(1, timeoutMs));
            auto remainingMs = [&startupDeadline]() {
                return std::max<int64_t>(
                    0,
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        startupDeadline - std::chrono::steady_clock::now()).count());
            };
            __block SCShareableContent* content = nil;
            __block NSError* contentError = nil;
            dispatch_semaphore_t contentReady = dispatch_semaphore_create(0);
            [SCShareableContent
                getShareableContentExcludingDesktopWindows:NO
                onScreenWindowsOnly:YES
                completionHandler:^(SCShareableContent* available, NSError* availableError) {
                    content = [available retain];
                    contentError = [availableError retain];
                    dispatch_semaphore_signal(contentReady);
                }];
            const dispatch_time_t deadline = dispatch_time(
                DISPATCH_TIME_NOW,
                remainingMs() * NSEC_PER_MSEC);
            if (dispatch_semaphore_wait(contentReady, deadline) != 0) {
                if (error) {
                    *error = "timed out enumerating ScreenCaptureKit displays";
                }
                return nullptr;
            }
            if (contentError || !content || content.displays.count == 0) {
                if (error) {
                    *error = NSErrorText(contentError, "ScreenCaptureKit found no displays");
                }
                [contentError release];
                [content release];
                return nullptr;
            }

            SCDisplay* display = nil;
            const CGDirectDisplayID mainDisplay = CGMainDisplayID();
            for (SCDisplay* candidate in content.displays) {
                if (candidate.displayID == mainDisplay) {
                    display = candidate;
                    break;
                }
            }
            if (!display) {
                display = content.displays.firstObject;
            }

            SCContentFilter* filter =
                [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
            SCStreamConfiguration* configuration = [[SCStreamConfiguration alloc] init];
            const auto [width, height] = RecordingDimensions(display, options.maxDimension);
            configuration.width = width;
            configuration.height = height;
            configuration.minimumFrameInterval =
                CMTimeMake(1, std::max(1, options.framesPerSecond));
            configuration.queueDepth = 5;
            configuration.showsCursor = options.includeCursor ? YES : NO;
            configuration.capturesAudio = NO;

            SCRecordingOutputConfiguration* outputConfiguration =
                [[SCRecordingOutputConfiguration alloc] init];
            outputConfiguration.outputURL =
                [NSURL fileURLWithPath:[NSString stringWithUTF8String:options.outputPath.string().c_str()]];
            outputConfiguration.videoCodecType = AVVideoCodecTypeH264;
            outputConfiguration.outputFileType = AVFileTypeMPEG4;

            auto state = std::make_shared<MacRecordingState>();
            MacRecordingState* statePtr = state.get();
            auto* delegate = [[ComputerCppRecordingDelegate alloc] initWithState:statePtr];
            SCRecordingOutput* output =
                [[SCRecordingOutput alloc] initWithConfiguration:outputConfiguration delegate:delegate];
            SCStream* stream =
                [[SCStream alloc] initWithFilter:filter configuration:configuration delegate:nil];
            [filter release];
            [configuration release];
            [outputConfiguration release];
            [contentError release];
            [content release];

            auto session = std::make_unique<MacScreenRecordingSession>(
                stream, output, delegate, state);

            NSError* addError = nil;
            if (![stream addRecordingOutput:output error:&addError]) {
                if (error) {
                    *error = NSErrorText(addError, "could not add ScreenCaptureKit recording output");
                }
                return nullptr;
            }
            const auto callbackState = state;
            [stream startCaptureWithCompletionHandler:^(NSError* startError) {
                if (startError) {
                    SetMacRecordingState(
                        callbackState.get(), false, false,
                        NSErrorText(startError, "could not start ScreenCaptureKit stream"));
                }
            }];

            {
                std::unique_lock<std::mutex> lock(session->state().mutex);
                const int64_t remaining = remainingMs();
                if (remaining <= 0 || !session->state().changed.wait_for(
                        lock,
                        std::chrono::milliseconds(remaining),
                        [&session] {
                            return session->state().started || session->state().failed;
                        })) {
                    if (error) {
                        *error = "timed out starting ScreenCaptureKit recording";
                    }
                    return nullptr;
                }
                if (session->state().failed) {
                    if (error) {
                        *error = session->state().error;
                    }
                    return nullptr;
                }
            }
            return session;
        }
        if (error) {
            *error = "native recording requires macOS 15 or later";
        }
        return nullptr;
    }
}

} // namespace ComputerCpp::Platform
