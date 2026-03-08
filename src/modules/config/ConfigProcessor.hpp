/*
 * ConfigProcessor.hpp
 *
 * Configuration DSL processor for Havel language.
 * Separates config logic from evaluator.
 */
#pragma once

#include <string>

namespace havel {

// Forward declarations - full types in cpp file
namespace ast {
    struct ConfigBlock;
    struct ConfigSection;
    struct DevicesBlock;
    struct ModesBlock;
}

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
};

} // namespace havel
