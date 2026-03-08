/*
 * HelpModule.cpp
 *
 * Help documentation module for Havel language.
 */
#include "HelpModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include <iostream>
#include <sstream>

namespace havel::modules {

void registerHelpModule(Environment& env, HostContext& ctx) {
    (void)ctx;  // Help doesn't need host context

    env.Define("help", HavelValue(BuiltinFunction(
        [](const std::vector<HavelValue>& args) -> HavelResult {
            std::ostringstream help;
            
            if (args.empty()) {
                // Show general help
                help << "\n=== Havel Language Help ===\n\n";
                help << "Usage: help()          - Show this help\n";
                help << "       help(\"module\")  - Show help for specific module\n\n";
                help << "Available modules:\n";
                help << "  - system      : System functions (print, sleep, exit, etc.)\n";
                help << "  - window      : Window management functions\n";
                help << "  - clipboard   : Clipboard operations\n";
                help << "  - text        : Text manipulation (upper, lower, trim, etc.)\n";
                help << "  - math        : Mathematical functions\n";
                help << "  - array       : Array operations\n";
                help << "  - filemanager : File operations\n";
                help << "  - process     : Process management\n";
                help << "  - http        : HTTP client\n";
                help << "  - browser     : Browser automation\n";
                help << "  - audio       : Audio control\n";
                help << "  - media       : Media playback\n";
                help << "  - screenshot  : Screenshot operations\n";
                help << "  - gui         : GUI dialogs\n";
                std::cout << help.str() << std::endl;
            } else if (args[0].isString()) {
                std::string module = args[0].asString();
                help << "\n=== Help for module: " << module << " ===\n";
                help << "(Detailed help for '" << module << "' not yet implemented)\n";
                std::cout << help.str() << std::endl;
            } else {
                return HavelRuntimeError("help() expects a string argument");
            }
            
            return HavelValue(nullptr);
        }
    )));
}

} // namespace havel::modules
