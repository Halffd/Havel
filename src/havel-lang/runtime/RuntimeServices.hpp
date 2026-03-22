/*
 * RuntimeServices.hpp - Stubbed (interpreter removed)
 */
#pragma once

#include <memory>
#include "modules/process/ShellExecutor.hpp"
#include "modules/io/InputModule.hpp"
#include "modules/config/ConfigProcessor.hpp"

namespace havel {

// Stubbed - interpreter removed
struct RuntimeServices {
    ShellExecutor shell;
    InputModule* input = nullptr;
    ConfigProcessor config;
    
    RuntimeServices() = default;
};

} // namespace havel
