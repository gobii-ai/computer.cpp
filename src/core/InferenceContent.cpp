#include "computer_cpp/InferenceClient.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace ComputerCpp::Inference {
namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string GuessMime(const std::filesystem::path& path) {
    std::string ext = Lower(path.extension().string());
    if (ext == ".jpg" || ext == ".jpeg") {
        return "image/jpeg";
    }
    if (ext == ".webp") {
        return "image/webp";
    }
    if (ext == ".gif") {
        return "image/gif";
    }
    return "image/png";
}

std::vector<unsigned char> ReadBinaryFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string Base64Encode(const std::vector<unsigned char>& bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= bytes.size()) {
        unsigned int value =
            (static_cast<unsigned int>(bytes[i]) << 16) |
            (static_cast<unsigned int>(bytes[i + 1]) << 8) |
            static_cast<unsigned int>(bytes[i + 2]);
        out.push_back(alphabet[(value >> 18) & 0x3f]);
        out.push_back(alphabet[(value >> 12) & 0x3f]);
        out.push_back(alphabet[(value >> 6) & 0x3f]);
        out.push_back(alphabet[value & 0x3f]);
        i += 3;
    }
    if (i < bytes.size()) {
        unsigned int value = static_cast<unsigned int>(bytes[i]) << 16;
        bool two = i + 1 < bytes.size();
        if (two) {
            value |= static_cast<unsigned int>(bytes[i + 1]) << 8;
        }
        out.push_back(alphabet[(value >> 18) & 0x3f]);
        out.push_back(alphabet[(value >> 12) & 0x3f]);
        out.push_back(two ? alphabet[(value >> 6) & 0x3f] : '=');
        out.push_back('=');
    }
    return out;
}

json ImagePathContent(const std::string& path, std::string* error) {
    std::filesystem::path imagePath(path);
    auto bytes = ReadBinaryFile(imagePath);
    if (bytes.empty()) {
        if (error) {
            *error = "could not read image path: " + path;
        }
        return json::object();
    }
    return {
        {"type", "image_url"},
        {"image_url", {{"url", "data:" + GuessMime(imagePath) + ";base64," + Base64Encode(bytes)}}}
    };
}

json NormalizeContentItem(const json& item, std::string* error) {
    if (!item.is_object()) {
        return item;
    }
    std::string type = item.value("type", "");
    if (type == "image_path") {
        return ImagePathContent(item.value("path", ""), error);
    }
    if (item.contains("image_path") && item["image_path"].is_string()) {
        return ImagePathContent(item["image_path"].get<std::string>(), error);
    }
    return item;
}

json NormalizeMessage(const json& message, std::string* error) {
    if (!message.is_object() || !message.contains("content")) {
        return message;
    }
    json out = message;
    if (out["content"].is_array()) {
        json content = json::array();
        for (const auto& item : out["content"]) {
            json normalized = NormalizeContentItem(item, error);
            if (error && !error->empty()) {
                return json::object();
            }
            content.push_back(std::move(normalized));
        }
        out["content"] = std::move(content);
    }
    return out;
}

} // namespace

json BuildMessages(const json& params, std::string* error) {
    json messages = params.value("messages", json::array());
    if (!messages.is_array()) {
        if (error) {
            *error = "llm_chat requires messages array";
        }
        return json::array();
    }
    json out = json::array();
    for (const auto& message : messages) {
        json normalized = NormalizeMessage(message, error);
        if (error && !error->empty()) {
            return json::array();
        }
        out.push_back(std::move(normalized));
    }
    if (params.contains("imagePaths") && params["imagePaths"].is_array()) {
        if (out.empty()) {
            out.push_back({{"role", "user"}, {"content", json::array()}});
        }
        json& last = out.back();
        if (!last.contains("content") || !last["content"].is_array()) {
            std::string text = last.value("content", "");
            last["content"] = json::array({{{"type", "text"}, {"text", text}}});
        }
        for (const auto& path : params["imagePaths"]) {
            json item = ImagePathContent(path.get<std::string>(), error);
            if (error && !error->empty()) {
                return json::array();
            }
            last["content"].push_back(std::move(item));
        }
    }
    return out;
}

} // namespace ComputerCpp::Inference
