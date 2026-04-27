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
void registerMathModule(compiler::VMApi &api); // MathModule
void registerStringModule(compiler::VMApi &api); // StringModule
void registerArrayModule(compiler::VMApi &api); // ArrayModule
void registerObjectModule(compiler::VMApi &api); // ObjectModule
void registerTypeModule(compiler::VMApi &api); // TypeModule
void registerUtilityModule(compiler::VMApi &api); // UtilityModule
void registerRegexModule(compiler::VMApi &api); // RegexModule
void registerPhysicsModule(
	compiler::VMApi &api); // PhysicsModule (constants only)
void registerTimeModule(
	compiler::VMApi &api); // TimeModule (timestamp ops only)
void registerHotkeyModule(compiler::VMApi &api); // HotkeyModule
void registerFsModule(compiler::VMApi &api); // FsModule
void registerRandomModule(compiler::VMApi &api); // RandomModule
void registerLogModule(compiler::VMApi &api); // LogModule
void registerSysModule(compiler::VMApi &api); // SysModule
void registerShellModule(compiler::VMApi &api); // ShellModule
void registerPointerModule(compiler::VMApi &api); // PointerModule
void registerFormatModule(compiler::VMApi &api); // FormatModule
void registerPackModule(compiler::VMApi &api); // PackModule
void registerBitModule(compiler::VMApi &api); // BitModule
} // namespace havel::stdlib

namespace havel {

void registerStdLibWithVM(compiler::HostBridge &bridge) {
	// Create VMApi from bridge's VM - store as shared to ensure lifetime
	static auto api = std::make_shared<compiler::VMApi>(*bridge.context().vm);

	// Register all auto-registered single-file modules first
	REGISTER_ALL_MODULES(*api);

	// Register PURE stdlib modules (no OS access)
	stdlib::registerMathModule(*api); // MathModule
	stdlib::registerStringModule(*api); // StringModule
	stdlib::registerArrayModule(*api); // ArrayModule
	stdlib::registerObjectModule(*api); // ObjectModule
	stdlib::registerTypeModule(*api); // TypeModule
	stdlib::registerUtilityModule(*api); // UtilityModule
	stdlib::registerRegexModule(*api); // RegexModule
	stdlib::registerPhysicsModule(*api); // PhysicsModule (constants)
	stdlib::registerTimeModule(*api); // TimeModule (timestamps)
	stdlib::registerHotkeyModule(*api); // HotkeyModule
	stdlib::registerFsModule(*api); // FsModule
	stdlib::registerRandomModule(*api); // RandomModule
	stdlib::registerLogModule(*api); // LogModule
	stdlib::registerSysModule(*api); // SysModule
	stdlib::registerShellModule(*api); // ShellModule
	stdlib::registerPointerModule(*api); // PointerModule
	stdlib::registerFormatModule(*api); // FormatModule
	stdlib::registerPackModule(*api); // PackModule
	stdlib::registerBitModule(*api); // BitModule

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

	// Pipeline function aliases
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
}

} // namespace havel
