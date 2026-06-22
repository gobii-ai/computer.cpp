#include "TrayIcon.h"

#include <wx/menu.h>
#include <wx/osx/menu.h>

#import <AppKit/AppKit.h>

namespace ComputerCpp::App {

namespace {

constexpr CGFloat kIconSize = 22.0;

NSRect IconRect(CGFloat x, CGFloat y, CGFloat width, CGFloat height) {
    return NSMakeRect(x, kIconSize - y - height, width, height);
}

NSPoint IconPoint(CGFloat x, CGFloat y) {
    return NSMakePoint(x, kIconSize - y);
}

void FillRounded(CGFloat x, CGFloat y, CGFloat width, CGFloat height, CGFloat radius, CGFloat alpha) {
    [[NSColor colorWithCalibratedWhite:0.0 alpha:alpha] setFill];
    [[NSBezierPath bezierPathWithRoundedRect:IconRect(x, y, width, height)
                                     xRadius:radius
                                     yRadius:radius] fill];
}

void StrokeRounded(CGFloat x,
                   CGFloat y,
                   CGFloat width,
                   CGFloat height,
                   CGFloat radius,
                   CGFloat alpha,
                   CGFloat lineWidth) {
    NSBezierPath* path = [NSBezierPath bezierPathWithRoundedRect:IconRect(x, y, width, height)
                                                        xRadius:radius
                                                        yRadius:radius];
    [path setLineWidth:lineWidth];
    [[NSColor colorWithCalibratedWhite:0.0 alpha:alpha] setStroke];
    [path stroke];
}

NSBezierPath* BasePath(CGFloat topInset, CGFloat topY, CGFloat bottomInset, CGFloat bottomY) {
    NSBezierPath* path = [NSBezierPath bezierPath];
    [path moveToPoint:IconPoint(topInset, topY)];
    [path lineToPoint:IconPoint(kIconSize - topInset, topY)];
    [path lineToPoint:IconPoint(kIconSize - bottomInset, bottomY)];
    [path lineToPoint:IconPoint(bottomInset, bottomY)];
    [path closePath];
    return path;
}

NSImage* CreateComputerTemplateImage() {
    NSImage* image = [NSImage imageWithSize:NSMakeSize(kIconSize, kIconSize)
                                    flipped:NO
                             drawingHandler:^BOOL(NSRect) {
        FillRounded(4.0, 3.3, 14.0, 11.3, 2.0, 0.12);
        StrokeRounded(4.0, 3.3, 14.0, 11.3, 2.0, 0.96, 1.35);
        FillRounded(5.8, 5.3, 10.4, 6.5, 1.0, 0.16);
        StrokeRounded(5.8, 5.3, 10.4, 6.5, 1.0, 0.72, 0.65);
        FillRounded(7.1, 6.3, 7.8, 1.0, 0.45, 0.46);

        NSBezierPath* base = BasePath(3.1, 14.8, 2.0, 17.4);
        [base setLineWidth:0.9];
        [[NSColor colorWithCalibratedWhite:0.0 alpha:0.12] setFill];
        [base fill];
        [[NSColor colorWithCalibratedWhite:0.0 alpha:0.96] setStroke];
        [base stroke];

        FillRounded(8.7, 15.6, 4.6, 0.8, 0.35, 0.62);
        FillRounded(2.4, 17.0, 17.2, 1.4, 0.55, 0.96);
        return YES;
    }];
    [image setTemplate:YES];
    return image;
}

struct NativeTrayIcon {
    NSStatusItem* statusItem = nil;
    id target = nil;
};

}

}

@interface ComputerCppStatusItemTarget : NSObject
@property(nonatomic, assign) ComputerCpp::App::TrayIcon* owner;
@property(nonatomic, assign) NSStatusItem* statusItem;
- (void)clickedAction:(id)sender;
@end

@implementation ComputerCppStatusItemTarget

- (void)clickedAction:(id)sender {
    (void)sender;
    if (!_owner || !_statusItem) {
        return;
    }

    wxMenu* menu = _owner->CreatePopupMenu();
    if (!menu) {
        return;
    }
    menu->SetEventHandler(_owner);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [_statusItem popUpStatusItemMenu:(NSMenu*)menu->GetHMenu()];
#pragma clang diagnostic pop

    delete menu;
}

@end

namespace ComputerCpp::App {

void* CreateNativeTrayIcon(TrayIcon* owner) {
    NativeTrayIcon* handle = new NativeTrayIcon;
    handle->statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
    if (!handle->statusItem) {
        delete handle;
        return nullptr;
    }
    [handle->statusItem retain];

    ComputerCppStatusItemTarget* target = [[ComputerCppStatusItemTarget alloc] init];
    target.owner = owner;
    target.statusItem = handle->statusItem;
    handle->target = target;

    NSStatusBarButton* button = [handle->statusItem button];
    [button setImage:CreateComputerTemplateImage()];
    [button setImagePosition:NSImageOnly];
    [button setImageScaling:NSImageScaleProportionallyDown];
    [button setToolTip:@"ComputerCpp"];
    [button setTarget:target];
    [button setAction:@selector(clickedAction:)];
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_12
    [button sendActionOn:NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown];
#endif

    return handle;
}

void DestroyNativeTrayIcon(void* rawHandle) {
    if (!rawHandle) {
        return;
    }

    NativeTrayIcon* handle = static_cast<NativeTrayIcon*>(rawHandle);
    if (handle->statusItem) {
        [[NSStatusBar systemStatusBar] removeStatusItem:handle->statusItem];
        [handle->statusItem release];
        handle->statusItem = nil;
    }
    [handle->target release];
    handle->target = nil;
    delete handle;
}

}
