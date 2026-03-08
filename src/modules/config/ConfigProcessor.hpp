/*
 * ConfigProcessor.hpp
 *
 * Configuration DSL processor for Havel language.
 * Separates config logic from evaluator.
 */
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace havel {

// Forward declarations
namespace ast {
    struct ConfigBlock;
    struct ConfigSection;
    struct DevicesBlock;
    struct ModesBlock;
}
class Environment;

/**
 * ConfigProcessor - Process configuration DSL blocks
 * 
 * Handles:
 * - config { } blocks
 * - devices { } blocks
 * - modes { } blocks
 * - Nested configuration sections
 * 
 * Config values are stored in the ConfigManager singleton.
 */
class ConfigProcessor {
public:
    ConfigProcessor() = default;
    ~ConfigProcessor() = default;

    /**
     * Process config block
     * @param node Config block AST node
     */
    void processConfigBlock(const ast::ConfigBlock& node);

    /**
     * Process config section
     * @param node Config section AST node
     */
    void processConfigSection(const ast::ConfigSection& node);

    /**
     * Process devices block
     * @param node Devices block AST node
     */
    void processDevicesBlock(const ast::DevicesBlock& node);

    /**
     * Process modes block
     * @param node Modes block AST node
     */
    void processModesBlock(const ast::ModesBlock& node);

private:
    // Internal helpers
    void processKeyValue(const std::string& key, const std::string& value);
    void processNestedBlock(const std::string& prefix, 
                            const std::vector<std::pair<std::string, std::unique_ptr<ast::Expression>>>& pairs);
};

} // namespace havel
