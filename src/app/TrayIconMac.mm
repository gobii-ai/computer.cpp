#include <wx/icon.h>

#include <dlfcn.h>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

namespace ComputerCpp::App {

bool TrayIconPrefersDarkForeground() {
    constexpr int kSampleWidth = 96;
    constexpr int kSampleHeight = 8;

    CGDirectDisplayID display = CGMainDisplayID();
    const size_t displayWidth = CGDisplayPixelsWide(display);
    if (displayWidth == 0) {
        return true;
    }

    CGFloat scale = 1.0;
    if (NSScreen* screen = [NSScreen mainScreen]) {
        scale = [screen backingScaleFactor];
    }
    const CGFloat menuHeight = [NSStatusBar systemStatusBar].thickness > 0
        ? [NSStatusBar systemStatusBar].thickness * scale
        : 24.0 * scale;
    using CGDisplayCreateImageForRectFunction = CGImageRef (*)(CGDirectDisplayID, CGRect);
    auto* createImageForRect = reinterpret_cast<CGDisplayCreateImageForRectFunction>(
        dlsym(RTLD_DEFAULT, "CGDisplayCreateImageForRect"));
    if (!createImageForRect) {
        return true;
    }

    CGRect captureRect = CGRectMake(0.0, 0.0, static_cast<CGFloat>(displayWidth), menuHeight);
    CGImageRef capture = createImageForRect(display, captureRect);
    if (!capture) {
        return true;
    }

    unsigned char pixels[kSampleWidth * kSampleHeight * 4] = {};
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(pixels,
                                                 kSampleWidth,
                                                 kSampleHeight,
                                                 8,
                                                 kSampleWidth * 4,
                                                 colorSpace,
                                                 static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) |
                                                     kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(colorSpace);
    if (!context) {
        CGImageRelease(capture);
        return true;
    }

    CGContextDrawImage(context, CGRectMake(0.0, 0.0, kSampleWidth, kSampleHeight), capture);
    CGContextRelease(context);
    CGImageRelease(capture);

    double luminance = 0.0;
    for (int i = 0; i < kSampleWidth * kSampleHeight; ++i) {
        const unsigned char* pixel = pixels + i * 4;
        luminance += 0.2126 * (static_cast<double>(pixel[0]) / 255.0) +
                     0.7152 * (static_cast<double>(pixel[1]) / 255.0) +
                     0.0722 * (static_cast<double>(pixel[2]) / 255.0);
    }
    luminance /= static_cast<double>(kSampleWidth * kSampleHeight);
    return luminance >= 0.52;
}

void ConfigureTrayIconForPlatform(wxIcon& icon) {
    (void)icon;
}

}
