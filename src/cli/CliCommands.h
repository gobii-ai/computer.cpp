#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace ComputerCpp::Cli {

struct CommandRequest {
    std::string method;
    nlohmann::json params;
    std::string error;
    int errorCode = 0;

    bool ok() const;
};

CommandRequest BuildDaemonCommand(const std::vector<std::string>& args, const std::string& batchInput = "");
CommandRequest BuildClickCommand(const std::vector<std::string>& args);
CommandRequest BuildAppCommand(const std::vector<std::string>& args);
CommandRequest BuildClipboardCommand(const std::vector<std::string>& args);
CommandRequest BuildGetCommand(const std::vector<std::string>& args);
CommandRequest BuildImageCommand(const std::vector<std::string>& args);
CommandRequest BuildMouseCommand(const std::vector<std::string>& args);
CommandRequest BuildObserveCommand(const std::vector<std::string>& args);
CommandRequest BuildOpenCommand(const std::vector<std::string>& args);
CommandRequest BuildPermissionsCommand(const std::vector<std::string>& args);
CommandRequest BuildPressCommand(const std::vector<std::string>& args);
CommandRequest BuildScrollCommand(const std::vector<std::string>& args);
CommandRequest BuildScreenshotCommand(const std::vector<std::string>& args);
CommandRequest BuildSnapshotCommand(const std::vector<std::string>& args);
CommandRequest BuildTargetCommand(const std::vector<std::string>& args);
CommandRequest BuildTypeCommand(const std::vector<std::string>& args);
CommandRequest BuildWaitCommand(const std::vector<std::string>& args);
CommandRequest BuildWindowCommand(const std::vector<std::string>& args);

} // namespace ComputerCpp::Cli
