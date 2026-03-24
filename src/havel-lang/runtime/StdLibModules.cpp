// StdLibModules.cpp
// Register all standard library modules with VM

#include "../compiler/bytecode/HostBridge.hpp"
#include "../compiler/bytecode/VMApi.hpp"
#include "../stdlib/FileSystemModule.hpp"
#include "../stdlib/HotkeyModule.hpp"
#include "../stdlib/NewClipboardModule.hpp"
#include "../stdlib/NewTimerModule.hpp"
#include "../stdlib/WindowModule.hpp"

namespace havel::stdlib {
void registerMathModule(compiler::VMApi &api);           // MathModule
void registerStringModule(compiler::VMApi &api);         // StringModule
void registerArrayModule(compiler::VMApi &api);          // ArrayModule
void registerObjectModule(compiler::VMApi &api);         // ObjectModule
void registerTimeModule(compiler::VMApi &api);           // TimeModule
void registerUtilityModule(compiler::VMApi &api);        // UtilityModule
void registerTypeModule(compiler::VMApi &api);           // TypeModule
void registerFileModule(compiler::VMApi &api);           // FileModule
void registerPhysicsModule(compiler::VMApi &api);        // PhysicsModule
void registerProcessModule(havel::compiler::VMApi &api); // ProcessModule
void registerRegexModule(havel::compiler::VMApi &api);   // RegexModule
void registerNewClipboardModule(
    havel::compiler::VMApi &api); // NewClipboardModule
void registerFileSystemModule(havel::compiler::VMApi &api); // FileSystemModule
void registerWindowModule(havel::compiler::VMApi &api);     // WindowModule
void registerHotkeyModule(havel::compiler::VMApi &api);     // HotkeyModule
void registerNewTimerModule(havel::compiler::VMApi &api);   // NewTimerModule
} // namespace havel::stdlib

namespace havel {

void registerStdLibWithVM(compiler::HostBridge &bridge) {
  // Create VMApi from bridge's VM - store as shared to ensure lifetime
  static auto api = std::make_shared<compiler::VMApi>(*bridge.context().vm);

  // Register working stdlib modules with VMApi
  // TODO: Update remaining modules to use VMApi
  stdlib::registerMathModule(*api);    // MathModule
  stdlib::registerStringModule(*api);  // StringModule
  stdlib::registerArrayModule(*api);   // ArrayModule
  stdlib::registerObjectModule(*api);  // ObjectModule
  stdlib::registerTimeModule(*api);    // TimeModule
  stdlib::registerUtilityModule(*api); // UtilityModule
  stdlib::registerTypeModule(*api);    // TypeModule
  stdlib::registerFileModule(*api);    // FileModule
  stdlib::registerPhysicsModule(*api); // PhysicsModule
  stdlib::registerProcessModule(*api); // ProcessModule
  stdlib::registerRegexModule(*api);   // RegexModule
  // stdlib::registerNewClipboardModule(*api); // NewClipboardModule
  // stdlib::registerFileSystemModule(*api);   // FileSystemModule
  // stdlib::registerWindowModule(*api);       // WindowModule
  // stdlib::registerHotkeyModule(*api);       // HotkeyModule
  // stdlib::registerNewTimerModule(*api);     // NewTimerModule

  // Add math, string, array, object, time, utility, type, file, physics,
  // process, regex, clipboard, filesystem, window, hotkey, and timer constants
  // to host_global_names so compiler knows about them
  bridge.options().host_global_names.insert("PI");
  bridge.options().host_global_names.insert("E");
  bridge.options().host_global_names.insert("math");
  bridge.options().host_global_names.insert("String");
  bridge.options().host_global_names.insert("string");
  bridge.options().host_global_names.insert("Array");
  bridge.options().host_global_names.insert("array");
  bridge.options().host_global_names.insert("Object");
  bridge.options().host_global_names.insert("object");
  bridge.options().host_global_names.insert("Time");
  bridge.options().host_global_names.insert("time");
  bridge.options().host_global_names.insert("Utility");
  bridge.options().host_global_names.insert("utility");
  bridge.options().host_global_names.insert("Type");
  bridge.options().host_global_names.insert("type");
  bridge.options().host_global_names.insert("File");
  bridge.options().host_global_names.insert("file");
  bridge.options().host_global_names.insert("Physics");
  bridge.options().host_global_names.insert("physics");
  bridge.options().host_global_names.insert("Process");
  bridge.options().host_global_names.insert("process");
  bridge.options().host_global_names.insert("Regex");
  bridge.options().host_global_names.insert("regex");
  bridge.options().host_global_names.insert("clipboard");
  bridge.options().host_global_names.insert("fs");
  bridge.options().host_global_names.insert("window");
  bridge.options().host_global_names.insert("hotkey");
  bridge.options().host_global_names.insert("timer");
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
  bridge.options().host_global_names.insert("readTextFile");
  bridge.options().host_global_names.insert("writeTextFile");
  bridge.options().host_global_names.insert("fileExists");
  bridge.options().host_global_names.insert("fileSize");
  bridge.options().host_global_names.insert("deleteFile");
  bridge.options().host_global_names.insert("force");
  bridge.options().host_global_names.insert("kinetic_energy");
  bridge.options().host_global_names.insert("potential_energy");
  bridge.options().host_global_names.insert("momentum");
  bridge.options().host_global_names.insert("wavelength");
  bridge.options().host_global_names.insert("execute");
  bridge.options().host_global_names.insert("getpid");
  bridge.options().host_global_names.insert("getppid");
  bridge.options().host_global_names.insert("sleep");
  bridge.options().host_global_names.insert("env");
  bridge.options().host_global_names.insert("setenv");
  bridge.options().host_global_names.insert("exit");
  bridge.options().host_global_names.insert("regex_match");
  bridge.options().host_global_names.insert("regex_search");
  bridge.options().host_global_names.insert("regex_replace");
  bridge.options().host_global_names.insert("regex_extract");
  bridge.options().host_global_names.insert("regex_split");
  bridge.options().host_global_names.insert("escape_regex");

  // Temporarily disabled modules that need VMApi migration:
  // - ArrayModule (direct VM access)
  // - FileModule (direct VM access)
  // - ObjectModule (direct VM access)
  // - PhysicsModule (direct VM access)
  // - ProcessModule (direct VM access)
  // - RegexModule (direct VM access)
  // - TimeModule (direct VM access)
  // - TypeModule (direct VM access)
  // - UtilityModule (direct VM access)
}

} // namespace havel
