/*
 * BrowserModule.cpp
 *
 * Browser automation module for Havel language.
 * Provides browser control via Chrome DevTools Protocol.
 */
#include "BrowserModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "core/browser/BrowserModule.hpp"

namespace havel::modules {

void registerBrowserModule(Environment &env,
                           std::shared_ptr<IHostAPI> hostAPI) {
  (void)hostAPI; // Browser doesn't need host context

  // Browser automation via Chrome DevTools Protocol
  auto browserMod =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // Helper to convert value to string
  auto valueToString = [](const HavelValue &v) -> std::string {
    if (v.isString())
      return v.asString();
    if (v.isNumber()) {
      double val = v.asNumber();
      if (val == std::floor(val) && std::abs(val) < 1e15) {
        return std::to_string(static_cast<long long>(val));
      } else {
        std::ostringstream oss;
        oss.precision(15);
        oss << val;
        std::string s = oss.str();
        if (s.find('.') != std::string::npos) {
          size_t last = s.find_last_not_of('0');
          if (last != std::string::npos && s[last] == '.') {
            s = s.substr(0, last);
          } else if (last != std::string::npos) {
            s = s.substr(0, last + 1);
          }
        }
        return s;
      }
    }
    if (v.isBool())
      return v.asBool() ? "true" : "false";
    return "";
  };

  // =========================================================================
  // Connection functions
  // =========================================================================

  (*browserMod)["connect"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        std::string url =
            args.empty() ? "http://localhost:9222" : valueToString(args[0]);
        return HavelValue(getBrowser().connect(url));
      }));

  (*browserMod)["disconnect"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        getBrowser().disconnect();
        return HavelValue(true);
      }));

  (*browserMod)["isConnected"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        return HavelValue(getBrowser().isConnected());
      }));

  // =========================================================================
  // Navigation functions
  // =========================================================================

  (*browserMod)["open"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("browser.open() requires URL");
        }
        std::string url = valueToString(args[0]);
        return HavelValue(getBrowser().open(url));
      }));

  (*browserMod)["newTab"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        std::string url = args.empty() ? "" : valueToString(args[0]);
        return HavelValue(getBrowser().newTab(url));
      }));

  (*browserMod)["goto"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("browser.goto() requires URL");
        }
        std::string url = valueToString(args[0]);
        return HavelValue(getBrowser().gotoUrl(url));
      }));

  (*browserMod)["back"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        return HavelValue(getBrowser().back());
      }));

  (*browserMod)["forward"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        return HavelValue(getBrowser().forward());
      }));

  (*browserMod)["reload"] = HavelValue(BuiltinFunction(
      [](const std::vector<HavelValue> &args) -> HavelResult {
        bool ignoreCache = !args.empty() && args[0].asBool();
        return HavelValue(getBrowser().reload(ignoreCache));
      }));

  // =========================================================================
  // Interaction functions
  // =========================================================================

  (*browserMod)["click"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("browser.click() requires selector");
        }
        std::string selector = valueToString(args[0]);
        return HavelValue(getBrowser().click(selector));
      }));

  (*browserMod)["type"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError("browser.type() requires (selector, text)");
        }
        std::string selector = valueToString(args[0]);
        std::string text = valueToString(args[1]);
        return HavelValue(getBrowser().type(selector, text));
      }));

  // Add the missing methods that are available in Browser class

  (*browserMod)["evaluate"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError(
              "browser.evaluate() requires JavaScript expression");
        }
        std::string js = valueToString(args[0]);
        return HavelValue(getBrowser().eval(js));
      }));

  (*browserMod)["getHtml"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        // Get HTML using JavaScript evaluation
        std::string html =
            getBrowser().eval("document.documentElement.outerHTML");
        return HavelValue(html);
      }));

  (*browserMod)["getUrl"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        return HavelValue(getBrowser().getCurrentUrl());
      }));

  (*browserMod)["screenshot"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        std::string path = args.empty() ? "" : valueToString(args[0]);
        return HavelValue(getBrowser().screenshot(path));
      }));

  (*browserMod)["getTitle"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        return HavelValue(getBrowser().getTitle());
      }));

  // Register browser module
  // MIGRATED TO BYTECODE VM: env.Define("browser", HavelValue(browserMod));
}

} // namespace havel::modules
