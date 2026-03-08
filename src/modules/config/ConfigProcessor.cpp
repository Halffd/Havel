/*
 * ConfigProcessor.cpp
 *
 * Configuration DSL processor for Havel language.
 * Separates config logic from evaluator.
 */
#include "ConfigProcessor.hpp"
#include "core/ConfigManager.hpp"
#include "havel-lang/runtime/Environment.hpp"
#include "utils/Logger.hpp"

namespace havel {

void ConfigProcessor::processConfigBlock(const ast::ConfigBlock& node) {
    auto& config = Configs::Get();

    // Special handling for "file" key - load config file
    for (const auto& [key, valueExpr] : node.pairs) {
        if (key == "file") {
            // File loading handled by evaluator - skip here
            continue;
        }
    }

    // Process all config key-value pairs (including nested blocks)
    processNestedBlock("", node.pairs);
}

void ConfigProcessor::processConfigSection(const ast::ConfigSection& node) {
    auto& config = Configs::Get();

    // Process key-value pairs
    for (const auto& [key, valueExpr] : node.pairs) {
        // Value evaluation handled by evaluator - just store string here
        // This is a simplified version; full implementation needs evaluator access
        debug("Config section key: {}", key);
    }
}

void ConfigProcessor::processDevicesBlock(const ast::DevicesBlock& node) {
    auto& config = Configs::Get();

    // Device configuration mappings
    std::unordered_map<std::string, std::string> deviceKeyMap = {
        {"keyboard", "Device.Keyboard"},
        {"mouse", "Device.Mouse"},
        {"joystick", "Device.Joystick"},
        {"mouseSensitivity", "Mouse.Sensitivity"},
        {"ignoreMouse", "Device.IgnoreMouse"}
    };

    for (const auto& [key, valueExpr] : node.pairs) {
        debug("Device config key: {}", key);
        // Value evaluation handled by evaluator
    }
}

void ConfigProcessor::processModesBlock(const ast::ModesBlock& node) {
    auto& config = Configs::Get();

    // Mode configuration mappings
    std::unordered_map<std::string, std::string> modeKeyMap = {
        {"default", "Mode.Default"},
        {"current", "Mode.Current"}
    };

    for (const auto& [key, valueExpr] : node.pairs) {
        debug("Mode config key: {}", key);
        // Value evaluation handled by evaluator
    }
}

void ConfigProcessor::processKeyValue(const std::string& key, const std::string& value) {
    auto& config = Configs::Get();
    std::string configKey = "Havel." + key;
    config.Set(configKey, value);
    debug("Config set: {} = {}", configKey, value);
}

void ConfigProcessor::processNestedBlock(const std::string& prefix,
                                          const std::vector<std::pair<std::string, std::unique_ptr<ast::Expression>>>& pairs) {
    auto& config = Configs::Get();

    for (const auto& [key, valueExpr] : pairs) {
        // Skip "file" key (already processed)
        if (key == "file") continue;

        // Full implementation needs evaluator to get actual values
        // This is a placeholder for the architecture
        std::string configKey = "Havel." + prefix + key;
        debug("Config nested key: {}", configKey);
    }
}

} // namespace havel
