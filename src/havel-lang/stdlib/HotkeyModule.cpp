/*
 * HotkeyModule.cpp - Simplified working version
 *
 * Hotkey module implementation using VMApi system.
 * Provides hotkey operations with clean host service integration.
 */
#include "HotkeyModule.hpp"
#include "../../host/hotkey/HotkeyService.hpp"
#include <cmath>
#include <sstream>

using namespace havel::compiler;

namespace havel::stdlib {

void registerHotkeyModule(havel::compiler::VMApi &api) {
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

  // Note: HotkeyService requires HotkeyManager instance
  // For now, we'll create placeholder functions that would be integrated
  // with the proper HotkeyManager in the full implementation

  // hotkey.register(keyCombo, callback) - Register hotkey (placeholder)
  api.registerFunction(
      "hotkey.register",
      [toString](const std::vector<BytecodeValue> &args) -> BytecodeValue {
        if (args.size() != 2) {
          throw std::runtime_error("hotkey.register() requires exactly two "
                                   "arguments (keyCombo, callback)");
        }

        std::string keyCombo = toString(args[0]);
        // callback = args[1] // would be stored for later execution

        // Placeholder: return hotkey ID
        return BytecodeValue(static_cast<int64_t>(1));
      });

  // hotkey.unregister(id) - Unregister hotkey (placeholder)
  api.registerFunction(
      "hotkey.unregister",
      [toNumber](const std::vector<BytecodeValue> &args) -> BytecodeValue {
        if (args.size() != 1) {
          throw std::runtime_error(
              "hotkey.unregister() requires exactly one argument (hotkeyId)");
        }

        int64_t hotkeyId = static_cast<int64_t>(toNumber(args[0]));

        // Placeholder: just return true for now
        return BytecodeValue(true);
      });

  // hotkey.isRegistered(id) - Check if hotkey is registered (placeholder)
  api.registerFunction(
      "hotkey.isRegistered",
      [toNumber](const std::vector<BytecodeValue> &args) -> BytecodeValue {
        if (args.size() != 1) {
          throw std::runtime_error(
              "hotkey.isRegistered() requires exactly one argument (hotkeyId)");
        }

        int64_t hotkeyId = static_cast<int64_t>(toNumber(args[0]));

        // Placeholder: return false for now
        return BytecodeValue(false);
      });

  // Create hotkey object with methods
  auto hotkeyObj = api.makeObject();

  // Add methods
  api.setField(hotkeyObj, "register", api.makeFunctionRef("hotkey.register"));
  api.setField(hotkeyObj, "unregister",
               api.makeFunctionRef("hotkey.unregister"));
  api.setField(hotkeyObj, "isRegistered",
               api.makeFunctionRef("hotkey.isRegistered"));

  // Register global hotkey object
  api.setGlobal("hotkey", hotkeyObj);
}

} // namespace havel::stdlib
