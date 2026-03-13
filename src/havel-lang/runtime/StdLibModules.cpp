// StdLibModules.cpp
// Register all standard library modules
// Simple, explicit, no magic

#include "../runtime/ModuleLoader.hpp"
#include "stdlib/ArrayModule.hpp"
#include "stdlib/MathModule.hpp"
#include "stdlib/StringModule.hpp"
#include "stdlib/ObjectModule.hpp"
#include "stdlib/FileModule.hpp"
#include "stdlib/RegexModule.hpp"
#include "stdlib/ProcessModule.hpp"
#include "stdlib/UtilityModule.hpp"
#include "stdlib/TypeModule.hpp"
#include "stdlib/TimeModule.hpp"

namespace havel {

/**
 * Register all standard library modules
 * These modules have NO host dependencies
 */
void registerStdLibModules(ModuleLoader& loader) {
    loader.add("array", stdlib::registerArrayModule);
    loader.add("math", stdlib::registerMathModule);
    loader.add("string", stdlib::registerStringModule);
    loader.add("object", stdlib::registerObjectModule);
    loader.add("file", stdlib::registerFileModule);
    loader.add("regex", stdlib::registerRegexModule);
    loader.add("process", stdlib::registerProcessModule);
    loader.add("utility", stdlib::registerUtilityModule);
    loader.add("type", stdlib::registerTypeModule);
    loader.add("time", stdlib::registerTimeModule);
}

/**
 * Load all standard library modules into environment
 */
void loadStdLibModules(Environment& env, ModuleLoader& loader) {
    loader.load(env, "array");
    loader.load(env, "math");
    loader.load(env, "string");
    loader.load(env, "object");
    loader.load(env, "file");
    loader.load(env, "regex");
    loader.load(env, "process");
    loader.load(env, "utility");
    loader.load(env, "type");
    loader.load(env, "time");
}

} // namespace havel
