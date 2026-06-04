#pragma once

#include <string>
#include <utility>
#include <vector>

namespace ComputerCpp::Cli {

class PosixArgv {
public:
    explicit PosixArgv(std::vector<std::string> args) : args_(std::move(args)) {
        argv_.reserve(args_.size() + 1);
        for (auto& arg : args_) {
            argv_.push_back(arg.data());
        }
        argv_.push_back(nullptr);
    }

    char* const* data() const {
        return argv_.data();
    }

    char* front() const {
        return argv_.empty() ? nullptr : argv_.front();
    }

private:
    std::vector<std::string> args_;
    std::vector<char*> argv_;
};

} // namespace ComputerCpp::Cli
