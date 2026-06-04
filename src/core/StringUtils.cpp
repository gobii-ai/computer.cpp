#include "computer_cpp/StringUtils.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace ComputerCpp {

std::string Lowercase(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

std::string Trim(const std::string& value) {
    auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    if (first == value.end()) {
        return "";
    }
    auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    return std::string(first, last);
}

bool IsBlank(const std::string& value) {
    return Trim(value).empty();
}

bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle) {
    return Lowercase(haystack).find(Lowercase(needle)) != std::string::npos;
}

std::vector<std::string> SplitKeyChord(const std::string& chord) {
    std::vector<std::string> keys;
    std::string current;
    std::istringstream stream(chord);
    while (std::getline(stream, current, '+')) {
        std::string key = Trim(current);
        if (!key.empty()) {
            keys.push_back(key);
        }
    }
    if (keys.empty() && !IsBlank(chord)) {
        keys.push_back(Trim(chord));
    }
    return keys;
}

std::string Join(const std::vector<std::string>& values, const std::string& sep) {
    std::string result;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            result += sep;
        }
        result += values[i];
    }
    return result;
}

}
