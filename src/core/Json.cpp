#include "computer_cpp/Json.h"

#include <sstream>

namespace ComputerCpp {

std::string JsonEscape(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u";
                    const char* digits = "0123456789abcdef";
                    out << '0' << '0' << digits[(c >> 4) & 0x0f] << digits[c & 0x0f];
                } else {
                    out << c;
                }
        }
    }
    return out.str();
}

std::string JsonString(const std::string& value) {
    return "\"" + JsonEscape(value) + "\"";
}

std::string JsonBool(bool value) {
    return value ? "true" : "false";
}

}
