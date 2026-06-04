#pragma once

#include <string>
#include <vector>

namespace ComputerCpp {

std::string Lowercase(std::string value);
std::string Trim(const std::string& value);
bool IsBlank(const std::string& value);
bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle);
std::vector<std::string> SplitKeyChord(const std::string& chord);
std::string Join(const std::vector<std::string>& values, const std::string& sep);

}
