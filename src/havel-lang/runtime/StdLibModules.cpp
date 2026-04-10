// StdLibModules.cpp
// Register all standard library modules with VM
//
// ARCHITECTURE: stdlib is PURE - no OS access, no side effects
// Host services (File, Process, Timer, Hotkey, Window, Clipboard, IO)
// are exposed through HostBridge, NOT through stdlib.

#include "../../modules/ModuleRegistry.hpp"
#include "../../modules/SingleFileMathModule.cpp"
#include "../../modules/automation/AutomationModule.hpp"
#include "../../modules/config/ConfigModule.hpp"
#include "../../modules/help/HelpModule.hpp"
#include "../../modules/hotkey/HotkeyModule.hpp"
#include "../../modules/mouse/MouseModule.hpp"
#include "../../modules/ui/UIModule.hpp"
#include "../../modules/window/WindowMonitorModule.hpp"
#include "havel-lang/compiler/runtime/HostBridge.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::stdlib {
// PURE stdlib modules only - no OS dependencies
void registerMathModule(compiler::VMApi &api);    // MathModule
void registerStringModule(compiler::VMApi &api);  // StringModule
void registerArrayModule(compiler::VMApi &api);   // ArrayModule
void registerObjectModule(compiler::VMApi &api);  // ObjectModule
void registerTypeModule(compiler::VMApi &api);    // TypeModule
void registerUtilityModule(compiler::VMApi &api); // UtilityModule
void registerRegexModule(compiler::VMApi &api);   // RegexModule
void registerPhysicsModule(
    compiler::VMApi &api); // PhysicsModule (constants only)
void registerTimeModule(
    compiler::VMApi &api); // TimeModule (timestamp ops only)
void registerHotkeyModule(compiler::VMApi &api); // HotkeyModule
void registerFsModule(compiler::VMApi &api);     // FsModule
} // namespace havel::stdlib

namespace havel {

void registerStdLibWithVM(compiler::HostBridge &bridge) {
  // Create VMApi from bridge's VM - store as shared to ensure lifetime
  static auto api = std::make_shared<compiler::VMApi>(*bridge.context().vm);

  // Register all auto-registered single-file modules first
  REGISTER_ALL_MODULES(*api);

  // Register PURE stdlib modules (no OS access)
  stdlib::registerMathModule(*api);    // MathModule
  stdlib::registerStringModule(*api);  // StringModule
  stdlib::registerArrayModule(*api);   // ArrayModule
  stdlib::registerObjectModule(*api);  // ObjectModule
  stdlib::registerTypeModule(*api);    // TypeModule
  stdlib::registerUtilityModule(*api); // UtilityModule
  stdlib::registerRegexModule(*api);   // RegexModule
  stdlib::registerPhysicsModule(*api); // PhysicsModule (constants)
  stdlib::registerTimeModule(*api);    // TimeModule (timestamps)
  stdlib::registerHotkeyModule(*api);  // HotkeyModule
  stdlib::registerFsModule(*api);      // FsModule

  // Register config module (has OS dependencies - config file access)
  modules::registerConfigModule(*api);

  // Register window monitor module (dynamic window variables)
  modules::registerWindowMonitorModule(*api);

  // Register help module (documentation system)
  modules::registerHelpModule(*api);

  // Register mouse module (mouse control)
  modules::registerMouseModule(*api);

  // Register UI module (user interface)
  modules::registerUIModule(*api);

  // Register automation module (auto clicker, runner, key presser)
  modules::registerAutomationModule(*api);

  // Note: setupDynamicWindowGlobals() is called by HostBridge after bridges are
  // initialized This ensures we use the existing WindowMonitor from
  // HotkeyManager

  // Register host global names for pure stdlib only
  bridge.options().host_global_names.insert("PI");
  bridge.options().host_global_names.insert("E");
  bridge.options().host_global_names.insert("math");
  bridge.options().host_global_names.insert("String");
  bridge.options().host_global_names.insert("string");
  bridge.options().host_global_names.insert("Array");
  bridge.options().host_global_names.insert("array");
  bridge.options().host_global_names.insert("Object");
  bridge.options().host_global_names.insert("object");
  bridge.options().host_global_names.insert("Type");
  bridge.options().host_global_names.insert("type");
  bridge.options().host_global_names.insert("type.of");
  bridge.options().host_global_names.insert("type.is");
  bridge.options().host_global_names.insert("Utility");
  bridge.options().host_global_names.insert("utility");
  bridge.options().host_global_names.insert("regex");
  bridge.options().host_global_names.insert("Regex");
  bridge.options().host_global_names.insert("config");
  bridge.options().host_global_names.insert("conf");
  bridge.options().host_global_names.insert("window");
  bridge.options().host_global_names.insert("mouse");
  bridge.options().host_global_names.insert("title");
  bridge.options().host_global_names.insert("class");
  bridge.options().host_global_names.insert("exe");
  bridge.options().host_global_names.insert("pid");
  bridge.options().host_global_names.insert("Physics");
  bridge.options().host_global_names.insert("physics");
  bridge.options().host_global_names.insert("E_CHARGE");
  bridge.options().host_global_names.insert("Time");
  bridge.options().host_global_names.insert("time");
  bridge.options().host_global_names.insert("ui");
  bridge.options().host_global_names.insert("fs");

  // Utility helpers
  bridge.options().host_global_names.insert("keys");
  bridge.options().host_global_names.insert("items");
  bridge.options().host_global_names.insert("list");
  bridge.options().host_global_names.insert("len");
  bridge.options().host_global_names.insert("isNumber");
  bridge.options().host_global_names.insert("isString");
  bridge.options().host_global_names.insert("isArray");
  bridge.options().host_global_names.insert("isObject");
  bridge.options().host_global_names.insert("isNull");
  bridge.options().host_global_names.insert("isBoolean");
  bridge.options().host_global_names.insert("toString");
  bridge.options().host_global_names.insert("toNumber");
  bridge.options().host_global_names.insert("wait");
  bridge.options().host_global_names.insert("newEnum");
  bridge.options().host_global_names.insert("getVariant");
  bridge.options().host_global_names.insert("getVariantPayload");
  bridge.options().host_global_names.insert("help");
  // Pipeline function aliases - add to both host_functions and
  // host_global_names
  bridge.options().host_functions.insert(
      {"upper", [](const std::vector<compiler::Value> &args) {
         if (args.empty() ||
             (!args[0].isStringValId() && !args[0].isStringId()))
           return compiler::Value::makeNull();
         return args[0];
       }});
  bridge.options().host_functions.insert(
      {"lower", [](const std::vector<compiler::Value> &args) {
         if (args.empty() ||
             (!args[0].isStringValId() && !args[0].isStringId()))
           return compiler::Value::makeNull();
         return args[0];
       }});
  bridge.options().host_functions.insert(
      {"trim", [](const std::vector<compiler::Value> &args) {
         if (args.empty() ||
             (!args[0].isStringValId() && !args[0].isStringId()))
           return compiler::Value::makeNull();
         return args[0];
       }});
  bridge.options().host_global_names.insert("upper");
  bridge.options().host_global_names.insert("lower");
  bridge.options().host_global_names.insert("trim");
  bridge.options().host_global_names.insert("replace");

  // HOST services are registered separately through HostBridge
  // File, Process, Timer, Hotkey, Window, Clipboard, IO -> host layer
  // Register host global names for host services

  // File system
  bridge.options().host_global_names.insert("readFile");
  bridge.options().host_global_names.insert("writeFile");
  bridge.options().host_global_names.insert("fileExists");
  bridge.options().host_global_names.insert("fileSize");
  bridge.options().host_global_names.insert("deleteFile");

  // Process
  bridge.options().host_global_names.insert("execute");
  bridge.options().host_global_names.insert("getpid");
  bridge.options().host_global_names.insert("getppid");
  bridge.options().host_global_names.insert("process");

  // Window
  bridge.options().host_global_names.insert("window");

  // Hotkey
  bridge.options().host_global_names.insert("hotkey");

  // Mode
  bridge.options().host_global_names.insert("mode");

  // Clipboard
  bridge.options().host_global_names.insert("clipboard");

  // Screenshot
  bridge.options().host_global_names.insert("screenshot");

  // Audio
  bridge.options().host_global_names.insert("audio");

  // Brightness
  bridge.options().host_global_names.insert("brightness");

  // Automation
  bridge.options().host_global_names.insert("automation");

  // Browser
  bridge.options().host_global_names.insert("browser");

  // TextChunker
  bridge.options().host_global_names.insert("textchunker");

  // MapManager (input remapping)
  bridge.options().host_global_names.insert("mapmanager");

  // AltTab (window switcher)
  bridge.options().host_global_names.insert("alttab");
  bridge.options().host_global_names.insert("input");
}

} // namespace havel
