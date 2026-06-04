#include "DaemonTargetText.h"

#include "computer_cpp/StringUtils.h"

namespace ComputerCpp {

bool HasRemovedVisualTargetPrefix(const std::string& target) {
    std::string normalized = Trim(target);
    return normalized.rfind("text:", 0) == 0 ||
        normalized.rfind("exact:", 0) == 0 ||
        normalized.rfind("field:", 0) == 0 ||
        normalized.rfind("color:", 0) == 0;
}

std::string TargetQueryString(const std::string& target) {
    std::string normalized = Trim(target);
    if (normalized.size() >= 2 && normalized.front() == '"' && normalized.back() == '"') {
        normalized = normalized.substr(1, normalized.size() - 2);
    }
    return Trim(normalized);
}

std::string NormalizeRole(std::string role) {
    role = Lowercase(Trim(role));
    if (role.rfind("ax", 0) == 0) {
        role = role.substr(2);
    }
    if (role == "textbox") {
        role = "textarea";
    }
    if (role == "textfield") {
        role = "textarea";
    }
    if (role == "link") {
        role = "link";
    }
    return role;
}

RoleTarget ParseRoleTarget(const std::string& raw) {
    std::string target = Trim(raw);
    RoleTarget parsed;
    if (target.rfind("role:", 0) != 0) {
        return parsed;
    }
    target = target.substr(5);
    size_t bracket = target.find('[');
    parsed.role = bracket == std::string::npos ? target : target.substr(0, bracket);
    parsed.role = NormalizeRole(parsed.role);
    parsed.valid = !parsed.role.empty();
    if (bracket == std::string::npos) {
        return parsed;
    }
    size_t end = target.rfind(']');
    if (end == std::string::npos || end <= bracket + 1) {
        return parsed;
    }
    std::string filter = target.substr(bracket + 1, end - bracket - 1);
    size_t eq = filter.find('=');
    if (eq == std::string::npos) {
        return parsed;
    }
    std::string key = Lowercase(Trim(filter.substr(0, eq)));
    std::string value = Trim(filter.substr(eq + 1));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    if (key == "name" || key == "label" || key == "text") {
        if (value.empty()) {
            parsed.valid = false;
            parsed.malformed = true;
            return parsed;
        }
        parsed.name = value;
    }
    return parsed;
}

std::string NormalizeRef(std::string ref) {
    ref = Trim(ref);
    if (!ref.empty() && ref.front() == '@') {
        ref.erase(ref.begin());
    }
    return ref;
}

} // namespace ComputerCpp
