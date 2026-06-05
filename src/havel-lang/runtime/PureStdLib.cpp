#include "havel-lang/compiler/vm/VMApi.hpp"
#include "loader/Loader.hpp"
#include "loader/ModulePlugin.h"

#ifdef ENABLE_MODULE_PLUGINS
// Plugin mode: load modules via dlopen
#else
// Static mode: direct calls to registration functions
#include "havel-lang/stdlib/MathModule.hpp"
#include "havel-lang/stdlib/StringModule.hpp"
#include "havel-lang/stdlib/ObjectModule.hpp"
#include "havel-lang/stdlib/TypeModule.hpp"
#include "havel-lang/stdlib/ArrayModule.hpp"
#include "havel-lang/stdlib/RegexModule.hpp"
#include "havel-lang/stdlib/TimeModule.hpp"
#include "havel-lang/stdlib/TimerModule.hpp"
#include "havel-lang/stdlib/FsModule.hpp"
#include "havel-lang/stdlib/RandomModule.hpp"
#include "havel-lang/stdlib/BitModule.hpp"
#include "havel-lang/stdlib/PackModule.hpp"
#include "havel-lang/stdlib/FormatModule.hpp"
#include "havel-lang/stdlib/OptionModule.hpp"
#include "havel-lang/stdlib/PointerModule.hpp"
#include "havel-lang/stdlib/ShellModule.hpp"
#include "havel-lang/stdlib/SysModule.hpp"
#include "havel-lang/stdlib/LogModule.hpp"
#include "havel-lang/stdlib/BytecodeBuilderModule.hpp"
#endif

namespace havel {

namespace {

#ifdef ENABLE_MODULE_PLUGINS

static Loader &sharedLoader() {
    static Loader loader;
    static std::once_flag flag;
    std::call_once(flag, [&]() { loader.addModulePaths(); });
    return loader;
}

void registerLazyFromPlugin(compiler::VM &vm, const char *name) {
 std::string modName(name);
 vm.registerLazyModule(modName, [modName](compiler::VMApi &a) {
 auto plugin = sharedLoader().loadModulePlugin(modName);
 if (plugin) {
 plugin->register_fn(static_cast<void *>(&a));
 }
 });
}

 void registerStdLibSet(compiler::VM &vm, bool coreOnly) {
 compiler::VMApi api(vm);

    static const char *eagerModules[] = {
        "math", "string", "object", "type", "array",
    };

    for (auto name : eagerModules) {
        auto plugin = sharedLoader().loadModulePlugin(name);
        if (plugin) {
            plugin->register_fn(static_cast<void *>(&api));
        }
    }

    if (coreOnly) {
        return;
    }

    static const char *lazyModules[] = {
        "regex", "time", "timer", "fs", "random", "debug",
        "sys", "shell", "pointer", "fmt", "pack", "bit",
        "option", "bytecodebuilder", "log",
        "window", "display", "brightness", "app",
    };

    for (auto name : lazyModules) {
        registerLazyFromPlugin(vm, name);
    }
}

#else

using RegisterFn = void(*)(const compiler::VMApi &);

struct ModuleEntry {
    const char *name;
    RegisterFn fn;
};

static const ModuleEntry eagerModules[] = {
    {"math", stdlib::registerMathModule},
    {"string", stdlib::registerStringModule},
    {"object", stdlib::registerObjectModule},
    {"type", stdlib::registerTypeModule},
    {"array", stdlib::registerArrayModule},
};

static const ModuleEntry lazyModules[] = {
    {"regex", stdlib::registerRegexModule},
    {"time", stdlib::registerTimeModule},
    {"timer", stdlib::registerTimerModule},
    {"fs", stdlib::registerFsModule},
    {"random", stdlib::registerRandomModule},
    {"debug", stdlib::registerDebugModule},
    {"sys", stdlib::registerSysModule},
    {"shell", stdlib::registerShellModule},
    {"pointer", stdlib::registerPointerModule},
    {"fmt", stdlib::registerFormatModule},
    {"pack", stdlib::registerPackModule},
    {"bit", stdlib::registerBitModule},
    {"option", stdlib::registerOptionModule},
    {"bytecodebuilder", stdlib::registerBytecodeBuilderModule},
    {"log", stdlib::registerLogModule},
};

void registerStdLibSet(compiler::VM &vm, bool coreOnly) {
  compiler::VMApi api(vm);

  for (auto &e : eagerModules) {
    e.fn(api);
  }

  // Always register bytecodeBuilder as lazy — needed for self-hosted compilation
  {
    std::string modName("bytecodeBuilder");
    RegisterFn fn = stdlib::registerBytecodeBuilderModule;
    vm.registerLazyModule(modName, [fn](compiler::VMApi &a) {
      fn(a);
    });
  }

  if (coreOnly) {
    return;
  }

 for (auto &e : lazyModules) {
 std::string modName(e.name);
 RegisterFn fn = e.fn;
 vm.registerLazyModule(modName, [fn](compiler::VMApi &a) {
 fn(a);
 });
 }
}

#endif

}

void registerPureStdLib(compiler::VM &vm) {
    registerStdLibSet(vm, false);
}

void registerCoreStdLib(compiler::VM &vm) {
    registerStdLibSet(vm, true);
}

}
