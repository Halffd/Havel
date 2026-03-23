/*
 * AppModule.cpp
 *
 * Application module for Havel language.
 * Provides application control (quit, restart, info, args).
 */
#include "AppModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "core/io/EventListener.hpp"
#include "core/process/ProcessManager.hpp"
#include <QApplication>
#include <QCoreApplication>

namespace havel::modules {

void registerAppModule(Environment &env, std::shared_ptr<IHostAPI> hostAPI) {
  auto &io = hostAPI;

  // Create app module object
  auto appObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // =========================================================================
  // app.quit() - Exit application
  // =========================================================================

  (*appObj)["quit"] = HavelValue(
      BuiltinFunction([io](const std::vector<HavelValue> &) -> HavelResult {
        info("Quit requested - performing hard exit");

        // TODO: Stop EventListener before exit when implemented
        // For now, perform hard exit immediately

        std::exit(0);
        return HavelValue(nullptr); // Never reached
      }));

  // =========================================================================
  // app.restart() - Restart application
  // =========================================================================

  (*appObj)["restart"] = HavelValue(
      BuiltinFunction([io](const std::vector<HavelValue> &) -> HavelResult {
        info("Restart requested");

        // TODO: Stop EventListener before restart when implemented
        // For now, restart immediately

        if (QApplication::instance()) {
          // Use Qt's proper restart mechanism - exit code 42 signals restart
          QCoreApplication::exit(42);
          return HavelValue(true);
        }

        // No QApplication - can't restart properly
        return HavelRuntimeError("app.restart() requires GUI application");
      }));

  // =========================================================================
  // app.info() - Get application information
  // =========================================================================

  (*appObj)["info"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        auto infoObj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();

        auto pid = ProcessManager::getCurrentPid();
        (*infoObj)["pid"] = HavelValue(static_cast<double>(pid));

        std::string exec = ProcessManager::getProcessExecutablePath(pid);
        (*infoObj)["path"] = HavelValue(exec);

        (*infoObj)["version"] = HavelValue("2.0.0");
        (*infoObj)["name"] = HavelValue("Havel");

        return HavelValue(infoObj);
      }));

  // =========================================================================
  // app.args - Get command line arguments
  // =========================================================================

  (*appObj)["args"] = HavelValue(BuiltinFunction(
      [hostAPI](const std::vector<HavelValue> &) -> HavelResult {
        // Get command line arguments from HostAPI
        if (!hostAPI) {
          return HavelRuntimeError("HostAPI not available");
        }

        const auto &args = hostAPI->GetCommandLineArgs();
        auto arr = std::make_shared<std::vector<HavelValue>>();

        for (const auto &arg : args) {
          arr->push_back(HavelValue(arg));
        }

        return HavelValue(arr);
      }));

  // Register app module
  // MIGRATED TO BYTECODE VM: env.Define("app", HavelValue(appObj));
}

} // namespace havel::modules
