/*
 * WindowModule.cpp - Simplified working version
 *
 * Window module implementation using VMApi system.
 * Provides window operations with clean host service integration.
 */
#include "WindowModule.hpp"
#include "../../host/window/WindowService.hpp"
#include <cmath>
#include <sstream>

using namespace havel::compiler;

namespace havel::stdlib {

void registerWindowModule(havel::compiler::VMApi &api) {
  // Helper: convert BytecodeValue to string
  auto toString = [](const BytecodeValue &v) -> std::string {
    if (std::holds_alternative<std::string>(v))
      return std::get<std::string>(v);
    if (std::holds_alternative<int64_t>(v))
      return std::to_string(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v)) {
      double val = std::get<double>(v);
      if (val == std::floor(val) && std::abs(val) < 1e15) {
        return std::to_string(static_cast<long long>(val));
      }
      std::ostringstream oss;
      oss.precision(15);
      oss << val;
      return oss.str();
    }
    if (std::holds_alternative<bool>(v))
      return std::get<bool>(v) ? "true" : "false";
    return "";
  };

  // Helper: convert BytecodeValue to number
  auto toNumber = [](const BytecodeValue &v) -> double {
    if (std::holds_alternative<int64_t>(v))
      return static_cast<double>(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v))
      return std::get<double>(v);
    return 0.0;
  };

  // Note: WindowService requires WindowManager instance
  // For now, we'll create placeholder functions that would be integrated
  // with the proper WindowManager in the full implementation

  // window.getActive() - Get active window ID (placeholder)
  api.registerFunction(
      "window.getActive",
      [](const std::vector<BytecodeValue> &args) -> BytecodeValue {
        if (!args.empty()) {
          throw std::runtime_error("window.getActive() takes no arguments");
        }

        // Placeholder: return 0 for now
        return BytecodeValue(static_cast<int64_t>(0));
      });

  // window.getAll() - Get all window IDs (placeholder)
  api.registerFunction(
      "window.getAll",
      [&api](const std::vector<BytecodeValue> &args) -> BytecodeValue {
        if (!args.empty()) {
          throw std::runtime_error("window.getAll() takes no arguments");
        }

        // Placeholder: return empty array for now
        auto array = api.makeArray();
        return array;
      });

  // window.focus(id) - Focus window (placeholder)
  api.registerFunction(
      "window.focus",
      [toNumber](const std::vector<BytecodeValue> &args) -> BytecodeValue {
        if (args.size() != 1) {
          throw std::runtime_error(
              "window.focus() requires exactly one argument (windowId)");
        }

        int64_t windowId = static_cast<int64_t>(toNumber(args[0]));

        // Placeholder: just return true for now
        return BytecodeValue(true);
      });

  // Create window object with methods
  auto windowObj = api.makeObject();

  // Add methods
  api.setField(windowObj, "getActive", api.makeFunctionRef("window.getActive"));
  api.setField(windowObj, "getAll", api.makeFunctionRef("window.getAll"));
  api.setField(windowObj, "focus", api.makeFunctionRef("window.focus"));

  // Register global window object
  api.setGlobal("window", windowObj);
}

} // namespace havel::stdlib
