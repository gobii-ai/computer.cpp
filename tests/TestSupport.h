#pragma once

#include <filesystem>

namespace ComputerCpp::Tests {

std::filesystem::path MakeTempHome();
void RunCliTests();
void RunControlSessionTests();
void RunDaemonDispatchTests();
void RunDaemonTests();
void RunInferenceTests();

}
