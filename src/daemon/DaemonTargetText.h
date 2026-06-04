#pragma once

#include <string>

namespace ComputerCpp {

struct RoleTarget {
    bool valid = false;
    bool malformed = false;
    std::string role;
    std::string name;
};

bool HasRemovedVisualTargetPrefix(const std::string& target);
std::string TargetQueryString(const std::string& target);
std::string NormalizeRole(std::string role);
RoleTarget ParseRoleTarget(const std::string& raw);
std::string NormalizeRef(std::string ref);

} // namespace ComputerCpp
