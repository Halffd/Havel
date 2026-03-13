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

namespace havel {

/**
 * Register all standard library modules
 * These modules have NO host dependencies
 */
void registerStdLibModules(ModuleLoader& loader) {
    loader.add("array", registerArrayModule);
    loader.add("math", registerMathModule);
    loader.add("string", registerStringModule);
    loader.add("object", registerObjectModule);
    loader.add("file", registerFileModule);
    loader.add("regex", registerRegexModule);
    loader.add("process", registerProcessModule);
    loader.add("utility", registerUtilityModule);
    loader.add("type", registerTypeModule);
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
}

} // namespace havel
