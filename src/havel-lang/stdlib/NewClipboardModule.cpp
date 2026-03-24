/*
 * ClipboardModule.cpp
 *
 * Clipboard module implementation using VMApi system.
 * Provides clipboard operations with clean host service integration.
 */
#include "NewClipboardModule.hpp"
#include "../../host/clipboard/Clipboard.hpp"
#include <cmath>
#include <sstream>

using namespace havel::compiler;

namespace havel::stdlib {

void registerNewClipboardModule(VMApi &api) {
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

  // Create clipboard service instance
  static havel::host::Clipboard clipboard;

  // Register clipboard functions

  // clipboard.getText() - Get clipboard text
  api.registerFunction(
      "clipboard.getText",
      [toString](const std::vector<BytecodeValue> &args) -> BytecodeValue {
        if (!args.empty()) {
          throw std::runtime_error("clipboard.getText() takes no arguments");
        }

        std::string text = clipboard.getText();
        return BytecodeValue(text);
      });

  // clipboard.setText(text) - Set clipboard text
  api.registerFunction(
      "clipboard.setText",
      [toString](const std::vector<BytecodeValue> &args) -> BytecodeValue {
        if (args.size() != 1) {
          throw std::runtime_error(
              "clipboard.setText() requires exactly one argument (text)");
        }

        std::string text = toString(args[0]);
        bool success = clipboard.setText(text);
        return BytecodeValue(success);
      });

  // clipboard.clear() - Clear clipboard
  api.registerFunction(
      "clipboard.clear",
      [](const std::vector<BytecodeValue> &args) -> BytecodeValue {
        if (!args.empty()) {
          throw std::runtime_error("clipboard.clear() takes no arguments");
        }

        bool success = clipboard.clear();
        return BytecodeValue(success);
      });

  // clipboard.hasText() - Check if clipboard has text
  api.registerFunction(
      "clipboard.hasText",
      [](const std::vector<BytecodeValue> &args) -> BytecodeValue {
        if (!args.empty()) {
          throw std::runtime_error("clipboard.hasText() takes no arguments");
        }

        bool hasText = clipboard.hasText();
        return BytecodeValue(hasText);
      });

  // Convenience aliases

  // clipboard.get() - Alias for getText
  api.registerFunction(
      "clipboard.get",
      [toString](const std::vector<BytecodeValue> &args) -> BytecodeValue {
        if (!args.empty()) {
          throw std::runtime_error("clipboard.get() takes no arguments");
        }

        std::string text = clipboard.get();
        return BytecodeValue(text);
      });

  // clipboard.in() - Alias for getText (input direction)
  api.registerFunction(
      "clipboard.in",
      [toString](const std::vector<BytecodeValue> &args) -> BytecodeValue {
        if (!args.empty()) {
          throw std::runtime_error("clipboard.in() takes no arguments");
        }

        std::string text = clipboard.in();
        return BytecodeValue(text);
      });

  // clipboard.out(text) - Alias for setText (output direction)
  api.registerFunction(
      "clipboard.out",
      [toString](const std::vector<BytecodeValue> &args) -> BytecodeValue {
        if (args.size() != 1) {
          throw std::runtime_error(
              "clipboard.out() requires exactly one argument (text)");
        }

        std::string text = toString(args[0]);
        bool success = clipboard.setText(text);
        return BytecodeValue(success);
      });

  // Create clipboard object with methods
  auto clipboardObj = api.makeObject();

  // Add methods to object
  api.setField(clipboardObj, "getText",
               api.makeFunctionRef("clipboard.getText"));
  api.setField(clipboardObj, "setText",
               api.makeFunctionRef("clipboard.setText"));
  api.setField(clipboardObj, "clear", api.makeFunctionRef("clipboard.clear"));
  api.setField(clipboardObj, "hasText",
               api.makeFunctionRef("clipboard.hasText"));
  api.setField(clipboardObj, "get", api.makeFunctionRef("clipboard.get"));
  api.setField(clipboardObj, "in", api.makeFunctionRef("clipboard.in"));
  api.setField(clipboardObj, "out", api.makeFunctionRef("clipboard.out"));

  // Register global clipboard object
  api.setGlobal("clipboard", clipboardObj);
}

} // namespace havel::stdlib
