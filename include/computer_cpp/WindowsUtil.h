#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ComputerCpp::Windows {

std::wstring Utf8ToWide(std::string_view value);
std::string WideToUtf8(std::wstring_view value);
std::wstring CommandLineForArgs(const std::vector<std::string>& args);

}
