#pragma once

#include <string>

namespace ComputerCpp {

std::string JsonEscape(const std::string& value);
std::string JsonString(const std::string& value);
std::string JsonBool(bool value);

}
