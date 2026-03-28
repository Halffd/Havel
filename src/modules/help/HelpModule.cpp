/*
 * HelpModule.cpp - Help system for bytecode VM
 * Provides help() function with documentation for all modules
 */
#include "HelpModule.hpp"
#include "havel-lang/compiler/bytecode/VMApi.hpp"
#include "utils/Logger.hpp"

#include <sstream>

namespace havel::modules {

using compiler::BytecodeValue;
using compiler::VMApi;

// Module documentation structure
struct ModuleDoc {
  const char* name;
  const char* description;
  std::vector<std::pair<const char*, const char*>> functions;
};

// Complete module documentation
static const ModuleDoc moduleDocs[] = {
  // Array module
  {
    "array",
    "Array operations and utilities",
    {
      {"len(arr)", "Get array length"},
      {"push(arr, value)", "Add value to end of array"},
      {"pop(arr)", "Remove and return last element"},
      {"has(arr, value)", "Check if array contains value"},
      {"find(arr, value)", "Find index of value in array"},
      {"map(arr, fn)", "Apply function to each element"},
      {"filter(arr, fn)", "Filter array by predicate function"},
      {"reduce(arr, fn, initial)", "Reduce array to single value"},
      {"foreach(arr, fn)", "Execute function for each element"},
      {"sort(arr)", "Sort array in ascending order"},
      {"join(arr, sep)", "Join array elements with separator"},
    }
  },
  
  // String module
  {
    "string",
    "String operations and utilities",
    {
      {"len(str)", "Get string length"},
      {"trim(str)", "Remove leading/trailing whitespace"},
      {"upper(str)", "Convert to uppercase"},
      {"lower(str)", "Convert to lowercase"},
      {"includes(str, substr)", "Check if string contains substring"},
      {"startswith(str, prefix)", "Check if string starts with prefix"},
      {"endswith(str, suffix)", "Check if string ends with suffix"},
      {"split(str, delim)", "Split string by delimiter"},
      {"join(arr, sep)", "Join array elements with separator"},
      {"sub(str, start, len)", "Extract substring"},
      {"replace(str, from, to)", "Replace all occurrences"},
    }
  },
  
  // Config module
  {
    "config",
    "Configuration file management",
    {
      {"get(key, default?)", "Get config value by key"},
      {"set(key, value)", "Set config value"},
      {"save()", "Save config to file"},
      {"load()", "Reload config from file"},
      {"getAll()", "Get all config as object"},
    }
  },
  
  // Window module
  {
    "window",
    "Window management and information",
    {
      {"activeTitle()", "Get active window title"},
      {"activeClass()", "Get active window class"},
      {"activeExe()", "Get active window executable"},
      {"activePid()", "Get active window PID"},
      {"active()", "Get all active window info as object"},
    }
  },
  
  // Type module
  {
    "type",
    "Type inspection and conversion",
    {
      {"of(value)", "Get type name of value"},
      {"is(value, type)", "Check if value is of type"},
    }
  },
  
  // Any module (generic operations)
  {
    "any",
    "Generic operations for any type",
    {
      {"len(value)", "Get length (string, array, object)"},
      {"has(value, key)", "Check if has key/property"},
      {"find(value, item)", "Find item in collection"},
      {"in(item, collection)", "Check membership"},
      {"not_in(item, collection)", "Check non-membership"},
    }
  },
  
  // System module
  {
    "system",
    "System operations",
    {
      {"gc()", "Run garbage collector"},
      {"gcStats()", "Get GC statistics"},
    }
  },
  
  // Struct module
  {
    "struct",
    "Struct/record operations",
    {
      {"define(name, fields)", "Define a new struct type"},
      {"new(name, ...)", "Create new struct instance"},
      {"get(struct, field)", "Get struct field value"},
      {"set(struct, field, value)", "Set struct field value"},
    }
  },
  
  // Extension module
  {
    "extension",
    "Dynamic extension loading",
    {
      {"load(name)", "Load extension by name"},
      {"isLoaded(name)", "Check if extension is loaded"},
      {"list()", "Get list of loaded extensions"},
      {"addSearchPath(path)", "Add extension search path"},
    }
  },
  
  // Global functions
  {
    "globals",
    "Global utility functions",
    {
      {"print(...)", "Print values to console"},
      {"sleep(ms)", "Sleep for milliseconds"},
      {"help()", "Show this help"},
      {"help(\"module\")", "Show module documentation"},
      {"help(\"module.func\")", "Show function documentation"},
    }
  },
};

// help() - Show general help or specific module/function help
static BytecodeValue helpFunc(VMApi &api, const std::vector<BytecodeValue> &args) {
  std::ostringstream oss;
  
  if (args.empty()) {
    // Show general help
    oss << "Havel Language Help\n";
    oss << "===================\n\n";
    oss << "Usage:\n";
    oss << "  help()                    - Show this help\n";
    oss << "  help(\"module\")           - Show module documentation\n";
    oss << "  help(\"module.function\")  - Show function documentation\n\n";
    oss << "Available Modules:\n";
    
    for (const auto& mod : moduleDocs) {
      oss << "  " << mod.name << " - " << mod.description << "\n";
    }
    
    oss << "\nUse help(\"module\") for detailed module documentation.\n";
    
  } else {
    // Get requested topic
    std::string topic;
    if (std::holds_alternative<std::string>(args[0])) {
      topic = std::get<std::string>(args[0]);
    } else {
      return BytecodeValue("help() requires a string argument");
    }
    
    // Check if it's a module.function request
    size_t dotPos = topic.find('.');
    if (dotPos != std::string::npos) {
      std::string moduleName = topic.substr(0, dotPos);
      std::string funcName = topic.substr(dotPos + 1);
      
      // Find module
      for (const auto& mod : moduleDocs) {
        if (moduleName == mod.name) {
          // Find function
          for (const auto& func : mod.functions) {
            std::string funcSig = func.first;
            size_t parenPos = funcSig.find('(');
            if (parenPos != std::string::npos) {
              std::string name = funcSig.substr(0, parenPos);
              if (name == funcName) {
                oss << mod.name << "." << func.second << "\n";
                oss << "Usage: " << func.first << "\n";
                oss << "Description: " << func.second << "\n";
                return BytecodeValue(oss.str());
              }
            }
          }
          oss << "Function '" << funcName << "' not found in module '" << moduleName << "'\n";
          return BytecodeValue(oss.str());
        }
      }
      oss << "Module '" << moduleName << "' not found\n";
      return BytecodeValue(oss.str());
    }
    
    // Show module documentation
    for (const auto& mod : moduleDocs) {
      if (topic == mod.name) {
        oss << mod.name << " - " << mod.description << "\n\n";
        oss << "Functions:\n";
        for (const auto& func : mod.functions) {
          oss << "  " << func.first << " - " << func.second << "\n";
        }
        return BytecodeValue(oss.str());
      }
    }
    
    oss << "Module '" << topic << "' not found\n";
    oss << "Available modules: ";
    bool first = true;
    for (const auto& mod : moduleDocs) {
      if (!first) oss << ", ";
      oss << mod.name;
      first = false;
    }
    oss << "\n";
  }
  
  return BytecodeValue(oss.str());
}

// Register help module with VM
void registerHelpModule(VMApi &api) {
  // help() function
  api.registerFunction("help", [&api](const std::vector<BytecodeValue> &args) {
    return helpFunc(api, args);
  });
  
  info("Help module registered");
}

} // namespace havel::modules
