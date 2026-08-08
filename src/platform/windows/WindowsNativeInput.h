#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>

namespace ComputerCpp::Platform::WindowsInput {

using SendInputFunction = std::function<UINT(UINT, LPINPUT, int)>;

// Test seam for exercising partial and failed native input delivery without
// sending real input to the interactive desktop.
void SetSendInputFunctionForTesting(SendInputFunction sender);
void ResetSendInputFunctionForTesting();

}
