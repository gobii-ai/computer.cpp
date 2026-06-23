#include "computer_cpp/WindowsUtil.h"

namespace ComputerCpp::Windows {

std::wstring Utf8ToWide(std::string_view value) {
#if defined(_WIN32)
    if (value.empty()) {
        return {};
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size);
    return out;
#else
    return std::wstring(value.begin(), value.end());
#endif
}

std::string WideToUtf8(std::wstring_view value) {
#if defined(_WIN32)
    if (value.empty()) {
        return {};
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
    return out;
#else
    return std::string(value.begin(), value.end());
#endif
}

namespace {

std::wstring QuoteArg(std::wstring_view arg) {
    if (arg.empty()) {
        return L"\"\"";
    }
    bool needsQuotes = false;
    for (wchar_t ch : arg) {
        if (ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\v' || ch == L'"') {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) {
        return std::wstring(arg);
    }

    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(ch);
            backslashes = 0;
            continue;
        }
        out.append(backslashes, L'\\');
        backslashes = 0;
        out.push_back(ch);
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

}

std::wstring CommandLineForArgs(const std::vector<std::string>& args) {
    std::wstring command;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            command.push_back(L' ');
        }
        command += QuoteArg(Utf8ToWide(args[i]));
    }
    return command;
}

#if defined(_WIN32)
ScopedEnvVar::ScopedEnvVar(const wchar_t* name, const std::wstring& value) : name_(name) {
    DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed > 0) {
        hadPrevious_ = true;
        previous_.resize(needed);
        DWORD written = GetEnvironmentVariableW(name, previous_.data(), needed);
        previous_.resize(written);
    }
    SetEnvironmentVariableW(name, value.c_str());
}

ScopedEnvVar::ScopedEnvVar(const wchar_t* name, const wchar_t* value)
    : ScopedEnvVar(name, std::wstring(value ? value : L"")) {}

ScopedEnvVar::~ScopedEnvVar() {
    SetEnvironmentVariableW(name_.c_str(), hadPrevious_ ? previous_.c_str() : nullptr);
}

bool StartProcess(const std::vector<std::string>& args, const ProcessOptions& options, PROCESS_INFORMATION& processInfo) {
    if (args.empty()) {
        return false;
    }
    std::wstring commandLine = CommandLineForArgs(args);
    STARTUPINFOW defaultStartup{};
    defaultStartup.cb = sizeof(defaultStartup);
    STARTUPINFOW* startupInfo = options.startupInfo ? options.startupInfo : &defaultStartup;
    return CreateProcessW(
        nullptr,
        commandLine.data(),
        nullptr,
        nullptr,
        options.inheritHandles ? TRUE : FALSE,
        options.creationFlags,
        nullptr,
        nullptr,
        startupInfo,
        &processInfo) != FALSE;
}

bool LaunchDetached(const std::vector<std::string>& args) {
    PROCESS_INFORMATION processInfo{};
    ProcessOptions options;
    options.creationFlags = CREATE_NO_WINDOW | DETACHED_PROCESS;
    if (!StartProcess(args, options, processInfo)) {
        return false;
    }
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

int ProcessExitCode(HANDLE process, int fallback) {
    DWORD exitCode = static_cast<DWORD>(fallback);
    if (!GetExitCodeProcess(process, &exitCode)) {
        return fallback;
    }
    return static_cast<int>(exitCode);
}
#endif

}
