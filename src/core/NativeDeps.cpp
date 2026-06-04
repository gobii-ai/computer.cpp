#include "computer_cpp/NativeDeps.h"

#include <curl/curl.h>

namespace ComputerCpp::NativeDeps {

Versions GetVersions() {
    Versions versions;
    if (auto* curlInfo = curl_version_info(CURLVERSION_NOW)) {
        if (curlInfo->version != nullptr) {
            versions.curl = curlInfo->version;
        }
    }
    return versions;
}

}
