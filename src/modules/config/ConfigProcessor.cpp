/*
 * ConfigProcessor.cpp
 *
 * Configuration DSL processor for Havel language.
 * Separates config logic from evaluator.
 */
#include "ConfigProcessor.hpp"
#include "core/ConfigManager.hpp"
#include "havel-lang/ast/AST.h"
#include "utils/Logger.hpp"

namespace havel {

void ConfigProcessor::processConfigBlock(const ast::ConfigBlock& node) {
    // Config processing handled by evaluator - this is a placeholder
    // for future extraction when config architecture stabilizes
    debug("Config block processing (placeholder)");
}

void ConfigProcessor::processConfigSection(const ast::ConfigSection& node) {
    // Placeholder - actual processing in StatementEvaluator
    debug("Config section processing (placeholder)");
}

void ConfigProcessor::processDevicesBlock(const ast::DevicesBlock& node) {
    // Placeholder - actual processing in StatementEvaluator
    debug("Devices block processing (placeholder)");
}

void ConfigProcessor::processModesBlock(const ast::ModesBlock& node) {
    // Placeholder - actual processing in StatementEvaluator
    debug("Modes block processing (placeholder)");
}

} // namespace havel
