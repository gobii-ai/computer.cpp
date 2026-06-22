#pragma once

#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ComputerCpp::Windows {

std::wstring Utf8ToWide(std::string_view value);
std::string WideToUtf8(std::wstring_view value);
std::wstring CommandLineForArgs(const std::vector<std::string>& args);

#if defined(_WIN32)
class ScopedEnvVar {
public:
    ScopedEnvVar(const wchar_t* name, const std::wstring& value);
    ScopedEnvVar(const wchar_t* name, const wchar_t* value);
    ~ScopedEnvVar();

    ScopedEnvVar(const ScopedEnvVar&) = delete;
    ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;

private:
    std::wstring name_;
    std::wstring previous_;
    bool hadPrevious_ = false;
};

struct ProcessOptions {
    bool inheritHandles = false;
    DWORD creationFlags = 0;
    STARTUPINFOW* startupInfo = nullptr;
};

bool StartProcess(const std::vector<std::string>& args, const ProcessOptions& options, PROCESS_INFORMATION& processInfo);
bool LaunchDetached(const std::vector<std::string>& args);
int ProcessExitCode(HANDLE process, int fallback = 1);
#endif

}
