/*
 * LauncherModule.cpp
 *
 * Process launching module for Havel language.
 * Host binding - connects language to Launcher.
 */
#include "../../havel-lang/runtime/Environment.hpp"
#include "process/Launcher.hpp"

namespace havel::modules {

void registerLauncherModule(Environment &env, std::shared_ptr<IHostAPI>) {
  // Create launcher module object
  auto launcherObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // =========================================================================
  // Process launching functions
  // =========================================================================

  (*launcherObj)["run"] = HavelValue(BuiltinFunction(
      [](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("run() requires command");
        }
        std::string command =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));

        auto result = Launcher::runSync(command);

        // Create result object with all available information
        auto resultObj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*resultObj)["success"] = HavelValue(result.success);
        (*resultObj)["exitCode"] =
            HavelValue(static_cast<double>(result.exitCode));
        (*resultObj)["pid"] = HavelValue(static_cast<double>(result.pid));
        (*resultObj)["stdout"] = HavelValue(result.stdout);
        (*resultObj)["stderr"] = HavelValue(result.stderr);
        (*resultObj)["error"] = HavelValue(result.error);
        (*resultObj)["executionTimeMs"] =
            HavelValue(static_cast<double>(result.executionTimeMs));

        return HavelValue(resultObj);
      }));

  (*launcherObj)["runAsync"] = HavelValue(BuiltinFunction(
      [](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("runAsync() requires command");
        }
        std::string command =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));

        auto result = Launcher::runAsync(command);
        return HavelValue(static_cast<double>(result.pid));
      }));

  (*launcherObj)["runShell"] = HavelValue(BuiltinFunction(
      [](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("runShell() requires command");
        }
        std::string command =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));

        auto result = Launcher::runShell(command);

        auto resultObj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*resultObj)["success"] = HavelValue(result.success);
        (*resultObj)["exitCode"] =
            HavelValue(static_cast<double>(result.exitCode));
        (*resultObj)["pid"] = HavelValue(static_cast<double>(result.pid));
        (*resultObj)["stdout"] = HavelValue(result.stdout);
        (*resultObj)["stderr"] = HavelValue(result.stderr);
        (*resultObj)["error"] = HavelValue(result.error);
        (*resultObj)["executionTimeMs"] =
            HavelValue(static_cast<double>(result.executionTimeMs));

        return HavelValue(resultObj);
      }));

  (*launcherObj)["runDetached"] = HavelValue(BuiltinFunction(
      [](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("runDetached() requires command");
        }
        std::string command =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));

        auto result = Launcher::runDetached(command);
        return HavelValue(result.success);
      }));

  (*launcherObj)["terminal"] = HavelValue(BuiltinFunction(
      [](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("terminal() requires command");
        }
        std::string command =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));

        auto result = Launcher::terminal(command);
        return HavelValue(result.success);
      }));

  // Register launcher module
  // MIGRATED TO BYTECODE VM: env.Define("launcher", HavelValue(launcherObj));
}

} // namespace havel::modules
