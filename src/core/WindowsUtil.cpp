#include "computer_cpp/WindowsUtil.h"

#if defined(_WIN32)
#include <windows.h>
#endif

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

}
