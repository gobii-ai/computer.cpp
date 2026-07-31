#include "computer_cpp/GobiiStartupRegistration.h"

#import <Foundation/Foundation.h>
#import <ServiceManagement/ServiceManagement.h>

namespace ComputerCpp {
namespace {

void SetError(std::string* error, NSString* message) {
    if (error) {
        *error = message
            ? std::string([message UTF8String])
            : "startup registration failed";
    }
}

} // namespace

bool GobiiStartupRegistration::IsSupported(std::string* error) {
    if (error) error->clear();
    if (@available(macOS 13.0, *)) {
        return true;
    }
    SetError(
        error,
        @"Launch at login requires macOS 13 or newer.");
    return false;
}

bool GobiiStartupRegistration::IsEnabled(std::string* error) {
    if (!IsSupported(error)) {
        return false;
    }
    if (@available(macOS 13.0, *)) {
        return [SMAppService mainAppService].status ==
            SMAppServiceStatusEnabled;
    }
    return false;
}

bool GobiiStartupRegistration::SetEnabled(
    bool enabled,
    std::string* error
) {
    if (!IsSupported(error)) {
        return false;
    }
    if (@available(macOS 13.0, *)) {
        NSError* nativeError = nil;
        const BOOL ok = enabled
            ? [[SMAppService mainAppService]
                registerAndReturnError:&nativeError]
            : [[SMAppService mainAppService]
                unregisterAndReturnError:&nativeError];
        if (!ok) {
            SetError(error, [nativeError localizedDescription]);
            return false;
        }
        if (error) error->clear();
        return true;
    }
    return false;
}

} // namespace ComputerCpp
