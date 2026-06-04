#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace ComputerCpp {

std::vector<std::string> KeyChordFromParams(const nlohmann::json& params);
nlohmann::json RunWaitCommand(const nlohmann::json& params);
nlohmann::json RunPressCommand(const nlohmann::json& params);
nlohmann::json RunTypeCommand(const std::string& session, const nlohmann::json& params);
nlohmann::json RunClipboardReadCommand();
nlohmann::json RunClipboardWriteCommand(const nlohmann::json& params);
nlohmann::json RunClipboardPasteCommand();

} // namespace ComputerCpp
