/*
 * SystemModule.cpp
 * 
 * System utilities module for Havel language.
 * Host binding - connects language to system services.
 */
#include "SystemModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "core/IO.hpp"
#include "core/HotkeyManager.hpp"
#include "gui/HavelApp.hpp"
#include <regex>
#include <chrono>
#include <thread>
#include <csignal>

namespace havel::modules {

void registerSystemModule(Environment& env, HostContext& ctx) {
    auto& io = *ctx.io;
    auto* hotkeyManager = ctx.hotkeyManager;
    auto* app = HavelApp::instance;
    
    // Helper to convert value to string
    auto valueToString = [](const HavelValue& v) -> std::string {
        if (v.isString()) return v.asString();
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
        if (v.isBool()) return v.asBool() ? "true" : "false";
        return "";
    };
    
    // Helper to convert value to number
    auto valueToNumber = [](const HavelValue& v) -> double {
        return v.asNumber();
    };
    
    // Helper to convert value to bool
    auto valueToBool = [](const HavelValue& v) -> bool {
        if (v.isBool()) return v.asBool();
        if (v.isNumber()) return v.asNumber() != 0.0;
        if (v.isString()) return !v.asString().empty();
        if (v.isNull()) return false;
        return true;
    };
    
    // =========================================================================
    // Boolean constants
    // =========================================================================
    
    env.Define("true", HavelValue(true));
    env.Define("false", HavelValue(false));
    env.Define("null", HavelValue(nullptr));
    
    // =========================================================================
    // Print function
    // =========================================================================
    
    env.Define("print", HavelValue(BuiltinFunction([valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        for (const auto& arg : args) {
            std::cout << valueToString(arg) << " ";
        }
        std::cout << std::endl;
        std::cout.flush();
        return HavelValue(nullptr);
    }));
    
    // =========================================================================
    // Duration parsing helper
    // =========================================================================
    
    auto parseDuration = [](const std::string& durationStr) -> long long {
        long long totalMs = 0;
        
        // Try HH:MM:SS.mmm format first
        std::regex timeRegex(R"((\d+):(\d+):(\d+)(?:\.(\d+))?)");
        std::smatch timeMatch;
        if (std::regex_match(durationStr, timeMatch, timeRegex)) {
            long long hours = std::stoll(timeMatch[1].str());
            long long minutes = std::stoll(timeMatch[2].str());
            long long seconds = std::stoll(timeMatch[3].str());
            long long millis = 0;
            if (timeMatch[4].matched) {
                std::string msStr = timeMatch[4].str();
                while (msStr.length() < 3) msStr += "0";
                millis = std::stoll(msStr.substr(0, 3));
            }
            return ((hours * 3600 + minutes * 60 + seconds) * 1000) + millis;
        }
        
        // Try HH:MM format
        std::regex shortTimeRegex(R"((\d+):(\d+))");
        std::smatch shortMatch;
        if (std::regex_match(durationStr, shortMatch, shortTimeRegex)) {
            long long hours = std::stoll(shortMatch[1].str());
            long long minutes = std::stoll(shortMatch[2].str());
            return (hours * 3600 + minutes * 60) * 1000;
        }
        
        // Try unit-based format
        std::regex unitRegex(R"((\d+)(ms|s|m|h|d|w))", std::regex::icase);
        auto begin = std::sregex_iterator(durationStr.begin(), durationStr.end(), unitRegex);
        auto end = std::sregex_iterator();
        
        for (auto it = begin; it != end; ++it) {
            long long value = std::stoll((*it)[1].str());
            std::string unit = (*it)[2].str();
            std::transform(unit.begin(), unit.end(), unit.begin(), ::tolower);
            
            if (unit == "ms") {
                totalMs += value;
            } else if (unit == "s") {
                totalMs += value * 1000;
            } else if (unit == "m" || unit == "min") {
                totalMs += value * 60 * 1000;
            } else if (unit == "h") {
                totalMs += value * 3600 * 1000;
            } else if (unit == "d") {
                totalMs += value * 24 * 3600 * 1000;
            } else if (unit == "w") {
                totalMs += value * 7 * 24 * 3600 * 1000;
            }
        }
        
        if (totalMs == 0) {
            try {
                totalMs = std::stoll(durationStr);
            } catch (...) {
                // Return 0 if parsing fails
            }
        }
        
        return totalMs;
    };
      
      std::tm targetTime = localNow;
      targetTime.tm_hour = targetHour;
      targetTime.tm_min = targetMinute;
      targetTime.tm_sec = targetSecond;
      
      auto targetTimestamp = std::chrono::system_clock::from_time_t(mktime(&targetTime));
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(targetTimestamp - now);
      
      // If target time has passed today, add 24 hours
      if (duration.count() < 0) {
        duration += std::chrono::hours(24);
      }
      
      return duration.count();
    }
    
    // Try day name format: "thursday 8:00", "monday 14:30"
    std::regex dayTimeRegex(R"((\w+)\s+(\d{1,2}):(\d{2})(?::(\d{2}))?)");
    std::smatch dayMatch;
    if (std::regex_match(timeStr, dayMatch, dayTimeRegex)) {
      std::string dayName = dayMatch[1].str();
      int targetHour = std::stoi(dayMatch[2].str());
      int targetMinute = std::stoi(dayMatch[3].str());
      int targetSecond = dayMatch[4].matched ? std::stoi(dayMatch[4].str()) : 0;
      
      // Convert day name to day of week (0=Sunday, 1=Monday, etc.)
      std::transform(dayName.begin(), dayName.end(), dayName.begin(), ::tolower);
      int targetDay = -1;
      if (dayName == "sunday" || dayName == "sun") targetDay = 0;
      else if (dayName == "monday" || dayName == "mon") targetDay = 1;
      else if (dayName == "tuesday" || dayName == "tue") targetDay = 2;
      else if (dayName == "wednesday" || dayName == "wed") targetDay = 3;
      else if (dayName == "thursday" || dayName == "thu") targetDay = 4;
      else if (dayName == "friday" || dayName == "fri") targetDay = 5;
      else if (dayName == "saturday" || dayName == "sat") targetDay = 6;
      
      if (targetDay >= 0) {
        std::tm targetTime = localNow;
        targetTime.tm_hour = targetHour;
        targetTime.tm_min = targetMinute;
        targetTime.tm_sec = targetSecond;
        targetTime.tm_mday += (targetDay - localNow.tm_wday + 7) % 7;
        
        // If today and time has passed, or future day, adjust
        if (targetDay == localNow.tm_wday) {
          auto targetTimestamp = std::chrono::system_clock::from_time_t(mktime(&targetTime));
          auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(targetTimestamp - now);
          if (duration.count() < 0) {
            targetTime.tm_mday += 7;
          }
        }
        
        auto targetTimestamp = std::chrono::system_clock::from_time_t(mktime(&targetTime));
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(targetTimestamp - now);
        return duration.count();
      }
    }
    
    // Fall back to duration parsing
    return parseDuration(timeStr);
  };

  // Core verb functions - global for fast typing
  // sleep(ms) or sleep("30s") or sleep("1h30m") or sleep("3:10:25")
  environment->Define(
      "sleep",
      BuiltinFunction(
          [this, parseDuration](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError("sleep() requires duration");
            
            long long ms = 0;
            if (args[0].isString()) {
              ms = parseDuration(args[0].asString());
            } else {
              ms = static_cast<long long>(ValueToNumber(args[0]));
            }
            
            if (ms < 0) {
              return HavelRuntimeError("sleep() duration must be non-negative");
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            return HavelValue(nullptr);
          }));

  // sleepUntil("HH:MM") or sleepUntil("thursday 8:00")
  environment->Define(
      "sleepUntil",
      BuiltinFunction(
          [this, parseTimeUntil](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError("sleepUntil() requires time string");
            
            std::string timeStr = args[0].isString() ? args[0].asString() : ValueToString(args[0]);
            long long ms = parseTimeUntil(timeStr);
            
            if (ms < 0) {
              return HavelRuntimeError("sleepUntil(): invalid time format");
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            return HavelValue(nullptr);
          }));

  // Global send function (alias to io.send)
  environment->Define(
      "send", BuiltinFunction(
                  [this](const std::vector<HavelValue> &args) -> HavelResult {
                    if (args.empty())
                      return HavelRuntimeError("send() requires keys string");
                    std::string keys = ValueToString(args[0]);
                    io->Send(keys.c_str());
                    return HavelValue(nullptr);
                  }));

  // Global play function (alias to media.play)
  environment->Define(
      "play", BuiltinFunction(
                  [this](const std::vector<HavelValue> &args) -> HavelResult {
                    if (auto app = HavelApp::instance) {
                      if (app->mpv) {
                        app->mpv->PlayPause();
                        return HavelValue(true);
                      }
                    }
                    return HavelRuntimeError("MPVController not available");
                  }));

  // Global exit function
  environment->Define(
      "exit", BuiltinFunction(
                  [this](const std::vector<HavelValue> &args) -> HavelResult {
                    if (App::instance()) {
                      App::quit();
                    }
                    return HavelValue(nullptr);
                  }));

  // Global file operations
  environment->Define(
      "read", BuiltinFunction(
                  [this](const std::vector<HavelValue> &args) -> HavelResult {
                    if (args.empty())
                      return HavelRuntimeError("read() requires file path");
                    std::string path = ValueToString(args[0]);
                    try {
                      FileManager file(path);
                      return HavelValue(file.read());
                    } catch (const std::exception &e) {
                      return HavelRuntimeError("Failed to read file: " +
                                               std::string(e.what()));
                    }
                  }));

  environment->Define(
      "write", BuiltinFunction(
                   [this](const std::vector<HavelValue> &args) -> HavelResult {
                     if (args.size() < 2)
                       return HavelRuntimeError(
                           "write() requires file path and content");
                     std::string path = ValueToString(args[0]);
                     std::string content = ValueToString(args[1]);
                     try {
                       FileManager file(path);
                       file.write(content);
                       return HavelValue(true);
                     } catch (const std::exception &e) {
                       return HavelRuntimeError("Failed to write file: " +
                                                std::string(e.what()));
                     }
                   }));

  // repeat(n, fn)
  environment->Define(
      "repeat",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("repeat() requires (count, function)");
        int count = static_cast<int>(ValueToNumber(args[0]));
        const HavelValue &fn = args[1];
        for (int i = 0; i < count; ++i) {
          std::vector<HavelValue> fnArgs = {HavelValue(static_cast<double>(i))};
          HavelResult res;
          if (auto *builtin = fn.get_if<BuiltinFunction>()) {
            res = (*builtin)(fnArgs);
          } else if (auto *userFunc =
                         fn.get_if<std::shared_ptr<HavelFunction>>()) {
            auto &func = *userFunc;
            auto funcEnv = std::make_shared<Environment>(func->closure);
            for (size_t p = 0;
                 p < func->declaration->parameters.size() && p < fnArgs.size();
                 ++p) {
              funcEnv->Define(func->declaration->parameters[p]->paramName->symbol,
                              fnArgs[p]);
            }
            auto originalEnv = this->environment;
            this->environment = funcEnv;
            res = Evaluate(*func->declaration->body);
            this->environment = originalEnv;
            if (isError(res))
              return res;
          } else {
            return HavelRuntimeError("repeat() requires callable function");
          }
          if (isError(res))
            return res;
        }
        return HavelValue(nullptr);
      }));

  environment->Define(
      "log", BuiltinFunction(
                 [this](const std::vector<HavelValue> &args) -> HavelResult {
                   std::cout << "[LOG] ";
                   for (const auto &arg : args) {
                     std::cout << this->ValueToString(arg) << " ";
                   }
                   std::cerr << std::endl;
                   std::cerr.flush();
                   return HavelValue(nullptr);
                 }));

  // === MODE SYSTEM FUNCTIONS ===
  // Create mode object with proper namespace
  auto modeObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // Get current mode
  (*modeObj)["get"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        // Try HotkeyManager first for authoritative mode
        if (hotkeyManager) {
          return HavelValue(hotkeyManager->getMode());
        }
        // Fallback to environment variable
        auto currentModeOpt = environment->Get("__current_mode__");
        std::string currentMode = "default";
        if (currentModeOpt && (*currentModeOpt).is<std::string>()) {
          currentMode = (*currentModeOpt).get<std::string>();
        }
        return HavelValue(currentMode);
      });

  // Set current mode
  (*modeObj)["set"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("mode.set() requires mode name");
        std::string newMode = this->ValueToString(args[0]);

        // Store previous mode
        auto currentModeOpt = environment->Get("__current_mode__");
        if (currentModeOpt) {
          environment->Define("__previous_mode__", *currentModeOpt);
        } else {
          environment->Define("__previous_mode__",
                              HavelValue(std::string("default")));
        }

        // Set new current mode in environment
        environment->Define("__current_mode__", HavelValue(newMode));

        // Sync with HotkeyManager
        if (hotkeyManager) {
          hotkeyManager->setMode(newMode);
          // Trigger conditional hotkey update
          hotkeyManager->updateAllConditionalHotkeys();
        }

        return HavelValue(nullptr);
      });

  // Switch to previous mode
  (*modeObj)["toggle"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        auto currentModeOpt = environment->Get("__current_mode__");
        auto previousModeOpt = environment->Get("__previous_mode__");

        std::string currentMode = "default";
        std::string previousMode = "default";

        if (currentModeOpt && (*currentModeOpt).is<std::string>()) {
          currentMode = (*currentModeOpt).get<std::string>();
        }
        if (previousModeOpt && (*previousModeOpt).is<std::string>()) {
          previousMode = (*previousModeOpt).get<std::string>();
        }

        // Swap modes in environment
        environment->Define("__previous_mode__", HavelValue(currentMode));
        environment->Define("__current_mode__", HavelValue(previousMode));
        
        // Sync with HotkeyManager
        if (hotkeyManager) {
          hotkeyManager->setMode(previousMode);
          // Trigger conditional hotkey update
          hotkeyManager->updateAllConditionalHotkeys();
        }
        
        return HavelValue(nullptr);
      });

  // Check if in specific mode
  (*modeObj)["is"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("mode.is() requires mode name");
        std::string checkMode = this->ValueToString(args[0]);

        auto currentModeOpt = environment->Get("__current_mode__");
        if (currentModeOpt && (*currentModeOpt).is<std::string>()) {
          std::string currentMode = (*currentModeOpt).get<std::string>();
          return HavelValue(currentMode == checkMode);
        }
        return HavelValue(false);
      });

  // Define the mode object
  environment->Define("mode", HavelValue(modeObj));

  environment->Define(
      "error", BuiltinFunction(
                   [this](const std::vector<HavelValue> &args) -> HavelResult {
                     std::cerr << "[ERROR] ";
                     for (const auto &arg : args) {
                       std::cerr << this->ValueToString(arg) << " ";
                     }
                     std::cerr << std::endl;
                     std::cerr.flush();
                     return HavelValue(nullptr);
                   }));

  environment->Define(
      "fatal", BuiltinFunction(
                   [this](const std::vector<HavelValue> &args) -> HavelResult {
                     std::cerr << "[FATAL] ";
                     for (const auto &arg : args) {
                       std::cerr << this->ValueToString(arg) << " ";
                     }
                     std::cerr << std::endl;
                     std::cerr.flush();
                     std::exit(1);
                     return HavelValue(nullptr);
                   }));

  environment->Define(
      "sleep",
      BuiltinFunction([this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("sleep() requires milliseconds");
        double ms = ValueToNumber(args[0]);
        std::this_thread::sleep_for(std::chrono::milliseconds((int)ms));
        
        // Return chainable sleep result with send() method
        auto result = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*result)["send"] = BuiltinFunction([this](const std::vector<HavelValue> &args) -> HavelResult {
          if (args.empty())
            return HavelRuntimeError("send() requires text");
          std::string text = this->ValueToString(args[0]);
          this->io->Send(text.c_str());
          return HavelValue(nullptr);
        });
        return HavelValue(result);
      }));

  environment->Define(
      "exit",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        int code = args.empty() ? 0 : (int)ValueToNumber(args[0]);
        std::exit(code);
        return HavelValue(nullptr);
      }));
  environment->Define(
      "type",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("type() requires an argument");

        // Check type using variant index
        switch (args[0].data.index()) {
          case 0: return HavelValue(std::string("null"));       // nullptr_t
          case 1: return HavelValue(std::string("boolean"));    // bool
          case 2: return HavelValue(std::string("number"));     // int
          case 3: return HavelValue(std::string("number"));     // double
          case 4: return HavelValue(std::string("string"));     // std::string
          case 5: return HavelValue(std::string("array"));      // HavelArray
          case 6: return HavelValue(std::string("object"));     // HavelObject
          case 7: return HavelValue(std::string("set"));        // HavelSet
          case 8: return HavelValue(std::string("function"));   // shared_ptr<HavelFunction>
          case 9: return HavelValue(std::string("channel"));    // shared_ptr<Channel>
          case 10: return HavelValue(std::string("builtin"));   // BuiltinFunction
          default: return HavelValue(std::string("unknown"));
        }
      }));

  // implements(obj, traitName) - check if object's type implements a trait
  environment->Define(
      "implements",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("implements() requires (object, traitName)");
        
        // Get type name from object
        std::string typeName;
        if (args[0].isStructInstance()) {
          typeName = args[0].asStructInstance().typeName;
        } else {
          return HavelValue(false);  // Non-struct types don't implement traits
        }
        
        std::string traitName = args[1].isString() ? args[1].asString() : "";
        
        // Check trait registry
        bool result = TraitRegistry::getInstance().implements(typeName, traitName);
        return HavelValue(result);
      }));

  // approx(a, b, epsilon) - fuzzy comparison for floating point (relative tolerance)
  environment->Define(
      "approx",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("approx() requires at least 2 arguments");
        
        double a = args[0].isNumber() ? args[0].asNumber() : 0.0;
        double b = args[1].isNumber() ? args[1].asNumber() : 0.0;
        double eps = (args.size() >= 3 && args[2].isNumber()) ? args[2].asNumber() : 1e-9;
        
        // Use relative tolerance for large magnitude values
        double diff = std::abs(a - b);
        double maxVal = std::max({1.0, std::abs(a), std::abs(b)});
        
        return HavelValue(diff <= eps * maxVal);
      }));

  // Send text/keys to the system
  environment->Define(
      "send", BuiltinFunction(
                  [this](const std::vector<HavelValue> &args) -> HavelResult {
                    if (args.empty())
                      return HavelRuntimeError("send() requires text");
                    std::string text = this->ValueToString(args[0]);
                    this->io->Send(text.c_str());
                    return HavelValue(nullptr);
                  }));

  // POSIX signal constants (used by some hotkey conversions)
  environment->Define("SIGSTOP", HavelValue(static_cast<double>(SIGSTOP)));
  environment->Define("SIGCONT", HavelValue(static_cast<double>(SIGCONT)));
  environment->Define("SIGKILL", HavelValue(static_cast<double>(SIGKILL)));

  environment->Define(
      "hotkey.toggleOverlay",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (!hotkeyManager)
              return HavelRuntimeError("HotkeyManager not available");
            hotkeyManager->toggleFakeDesktopOverlay();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "hotkey.showBlackOverlay",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (!hotkeyManager)
              return HavelRuntimeError("HotkeyManager not available");
            hotkeyManager->showBlackOverlay();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "hotkey.printActiveWindowInfo",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (!hotkeyManager)
              return HavelRuntimeError("HotkeyManager not available");
            hotkeyManager->printActiveWindowInfo();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "hotkey.toggleWindowFocusTracking",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (!hotkeyManager)
              return HavelRuntimeError("HotkeyManager not available");
            hotkeyManager->toggleWindowFocusTracking();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "hotkey.updateConditional",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (!hotkeyManager)
              return HavelRuntimeError("HotkeyManager not available");
            hotkeyManager->updateAllConditionalHotkeys();
            return HavelValue(nullptr);
          }));

  // hotkey.clearAll() - Clear all registered hotkeys
  environment->Define(
      "hotkey.clearAll",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (!hotkeyManager)
              return HavelRuntimeError("HotkeyManager not available");
            hotkeyManager->clearAllHotkeys();
            return HavelValue(nullptr);
          }));

  // hotkey.list() - List all registered hotkeys
  environment->Define(
      "hotkey.list",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (!hotkeyManager)
              return HavelRuntimeError("HotkeyManager not available");

            auto list = hotkeyManager->getHotkeyList();
            auto resultArray = std::make_shared<std::vector<HavelValue>>();
            for (const auto& hk : list) {
              auto hkObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
              (*hkObj)["alias"] = HavelValue(hk.alias);
              (*hkObj)["id"] = HavelValue(static_cast<double>(hk.id));
              (*hkObj)["enabled"] = HavelValue(hk.enabled);
              resultArray->push_back(HavelValue(hkObj));
            }
            return HavelValue(resultArray);
          }));

  // hotkey.getConditional() - Get all conditional hotkeys
  environment->Define(
      "hotkey.getConditional",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (!hotkeyManager)
              return HavelRuntimeError("HotkeyManager not available");

            auto list = hotkeyManager->getConditionalHotkeyList();
            auto resultArray = std::make_shared<std::vector<HavelValue>>();
            for (const auto& ch : list) {
              auto chObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
              (*chObj)["key"] = HavelValue(ch.key);
              (*chObj)["condition"] = HavelValue(ch.condition);
              (*chObj)["enabled"] = HavelValue(ch.enabled);
              (*chObj)["active"] = HavelValue(ch.active);
              resultArray->push_back(HavelValue(chObj));
            }
            return HavelValue(resultArray);
          }));

  // hotkey.monitor() - Enable/disable conditional hotkey monitoring
  environment->Define(
      "hotkey.monitor",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!hotkeyManager)
              return HavelRuntimeError("HotkeyManager not available");
            
            bool enable = args.empty() || (args[0].isBool() && args[0].asBool());
            hotkeyManager->setConditionalHotkeysEnabled(enable);
            return HavelValue(enable);
          }));

  // hotkey.isMonitoring() - Check if conditional hotkey monitoring is enabled
  environment->Define(
      "hotkey.isMonitoring",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (!hotkeyManager)
              return HavelRuntimeError("HotkeyManager not available");
            
            return HavelValue(hotkeyManager->getConditionalHotkeysEnabled());
          }));

  auto hotkeyObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();
  if (auto v = environment->Get("hotkey.toggleOverlay"))
    (*hotkeyObj)["toggleOverlay"] = *v;
  if (auto v = environment->Get("hotkey.showBlackOverlay"))
    (*hotkeyObj)["showBlackOverlay"] = *v;
  if (auto v = environment->Get("hotkey.printActiveWindowInfo"))
    (*hotkeyObj)["printActiveWindowInfo"] = *v;
  if (auto v = environment->Get("hotkey.toggleWindowFocusTracking"))
    (*hotkeyObj)["toggleWindowFocusTracking"] = *v;
  if (auto v = environment->Get("hotkey.updateConditional"))
    (*hotkeyObj)["updateConditional"] = *v;
  if (auto v = environment->Get("hotkey.clearAll"))
    (*hotkeyObj)["clearAll"] = *v;
  if (auto v = environment->Get("hotkey.list"))
    (*hotkeyObj)["list"] = *v;
  if (auto v = environment->Get("hotkey.getConditional"))
    (*hotkeyObj)["getConditional"] = *v;
  if (auto v = environment->Get("hotkey.monitor"))
    (*hotkeyObj)["monitor"] = *v;
  if (auto v = environment->Get("hotkey.isMonitoring"))
    (*hotkeyObj)["isMonitoring"] = *v;
  environment->Define("hotkey", HavelValue(hotkeyObj));

  environment->Define(
      "Hotkey",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2 || args.size() > 4)
          return HavelRuntimeError("Hotkey requires 2–4 arguments");

        if (!hotkeyManager)
          return HavelValue(nullptr);

        std::string key = ValueToString(args[0]);

        // --- helper: convert value → void() ---
        auto toVoidAction =
            [this](const HavelValue &v) -> std::function<void()> {
          if (v.is<std::string>()) {
            std::string cmd = v.get<std::string>();
            return [cmd]() { Launcher::runShellDetached(cmd.c_str()); };
          }

          if (v.is<std::shared_ptr<HavelFunction>>()) {
            auto fn = v.get<std::shared_ptr<HavelFunction>>();
            return [this, fn]() { this->Evaluate(*fn->declaration); };
          }

          if (v.is<BuiltinFunction>()) {
            auto fn = v.get<BuiltinFunction>();
            return [fn]() { fn({}); };
          }

          throw HavelRuntimeError("Invalid action type");
        };

        // --- helper: convert value → bool() ---
        auto toBoolCondition = [this](const HavelValue &v)
            -> std::variant<std::string, std::function<bool()>> {
          if (v.is<std::string>()) {
            return v.asString();
          }

          if (v.is<std::shared_ptr<HavelFunction>>()) {
            auto fn = v.get<std::shared_ptr<HavelFunction>>();
            return std::function<bool()>([this, fn]() {
              auto result = this->Evaluate(*fn->declaration);
              if (isError(result)) {
                std::cerr << "Error in condition: "
                          << std::get<HavelRuntimeError>(result).what()
                          << std::endl;
                return false;
              }
              return ExecResultToBool(result);
            });
          }

          if (v.is<BuiltinFunction>()) {
            auto fn = v.get<BuiltinFunction>();
            return std::function<bool()>([fn]() {
              auto result = fn({});
              if (isError(result)) {
                std::cerr << "Error in condition: "
                          << std::get<HavelRuntimeError>(result).what()
                          << std::endl;
                return false;
              }
              return ExecResultToBool(result);
            });
          }

          throw HavelRuntimeError("Invalid condition type");
        };

        // ---------- CASE 2 args ----------
        if (args.size() == 2) {
          hotkeyManager->AddHotkey(key, toVoidAction(args[1]));
          return HavelValue(nullptr);
        }

        // ---------- CASE 3 or 4 args ----------
        auto condition = toBoolCondition(args[2]);
        auto trueAction = toVoidAction(args[1]);

        std::function<void()> falseAction = nullptr;
        if (args.size() == 4)
          falseAction = toVoidAction(args[3]);

        // string condition
        if (condition.index() == 0) {
          return HavelValue(hotkeyManager->AddContextualHotkey(
              key, std::get<std::string>(condition), trueAction, falseAction));
        }

        // lambda condition
        return HavelValue(hotkeyManager->AddContextualHotkey(
            key, std::get<std::function<bool()>>(condition), trueAction,
            falseAction));
      }));
  // Process helpers
  environment->Define(
      "process.getState",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("process.getState() requires pid");
        pid_t pid = static_cast<pid_t>(ValueToNumber(args[0]));
        auto state = havel::ProcessManager::getProcessState(pid);
        switch (state) {
        case havel::ProcessManager::ProcessState::RUNNING:
          return HavelValue(std::string("RUNNING"));
        case havel::ProcessManager::ProcessState::SLEEPING:
          return HavelValue(std::string("SLEEPING"));
        case havel::ProcessManager::ProcessState::ZOMBIE:
          return HavelValue(std::string("ZOMBIE"));
        case havel::ProcessManager::ProcessState::STOPPED:
          return HavelValue(std::string("STOPPED"));
        case havel::ProcessManager::ProcessState::NO_PERMISSION:
          return HavelValue(std::string("NO_PERMISSION"));
        case havel::ProcessManager::ProcessState::NOT_FOUND:
          return HavelValue(std::string("NOT_FOUND"));
        }
        return HavelValue(std::string("UNKNOWN"));
      }));

  environment->Define(
      "process.sendSignal",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError(
              "process.sendSignal() requires (pid, signal)");
        pid_t pid = static_cast<pid_t>(ValueToNumber(args[0]));
        int sig = static_cast<int>(ValueToNumber(args[1]));
        return HavelValue(havel::ProcessManager::sendSignal(pid, sig));
      }));

  environment->Define(
      "process.kill",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("process.kill() requires (pid, signal)");
        pid_t pid = static_cast<pid_t>(ValueToNumber(args[0]));
        std::string signalStr = ValueToString(args[1]);

        // Convert signal string to number
        int signal = SIGTERM; // default
        if (signalStr == "SIGTERM") {
          signal = SIGTERM;
        } else if (signalStr == "SIGKILL") {
          signal = SIGKILL;
        } else if (signalStr == "SIGINT") {
          signal = SIGINT;
        } else {
          // Try to parse as number
          try {
            signal = std::stoi(signalStr);
          } catch (...) {
            return HavelRuntimeError("Invalid signal: " + signalStr);
          }
        }

        return HavelValue(havel::ProcessManager::sendSignal(pid, signal));
      }));

  environment->Define(
      "process.exists",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "process.exists() requires pid or process name");

        // Check if argument is a number (PID) or string (process
        // name)
        if (args[0].is<double>()) {
          pid_t pid = static_cast<pid_t>(ValueToNumber(args[0]));
          return HavelValue(havel::ProcessManager::isProcessAlive(pid));
        } else {
          // Search by process name
          std::string name = ValueToString(args[0]);
          auto processes = havel::ProcessManager::findProcesses(name);
          return HavelValue(!processes.empty());
        }
      }));

  environment->Define(
      "process.find",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("process.find() requires process name");

        std::string name = ValueToString(args[0]);
        auto processes = havel::ProcessManager::findProcesses(name);

        // Convert to array of process info objects
        auto resultArray = std::make_shared<std::vector<HavelValue>>();
        for (const auto &proc : processes) {
          auto procObj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*procObj)["pid"] = HavelValue(static_cast<double>(proc.pid));
          (*procObj)["ppid"] = HavelValue(static_cast<double>(proc.ppid));
          (*procObj)["name"] = HavelValue(proc.name);
          (*procObj)["command"] = HavelValue(proc.command);
          (*procObj)["user"] = HavelValue(proc.user);
          (*procObj)["cpu_usage"] = HavelValue(proc.cpu_usage);
          (*procObj)["memory_usage"] =
              HavelValue(static_cast<double>(proc.memory_usage));
          resultArray->push_back(HavelValue(procObj));
        }

        return HavelValue(resultArray);
      }));

  environment->Define(
      "process.nice",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("process.nice() requires (pid, nice_value)");

        pid_t pid = static_cast<pid_t>(ValueToNumber(args[0]));
        int niceValue = static_cast<int>(ValueToNumber(args[1]));

        return HavelValue(
            havel::ProcessManager::setProcessNice(pid, niceValue));
      }));

  environment->Define(
      "process.ionice",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 3)
          return HavelRuntimeError(
              "process.ionice() requires (pid, class, data)");

        pid_t pid = static_cast<pid_t>(ValueToNumber(args[0]));
        int ioclass = static_cast<int>(ValueToNumber(args[1]));
        int iodata = static_cast<int>(ValueToNumber(args[2]));

        return HavelValue(
            havel::ProcessManager::setProcessIoPriority(pid, ioclass, iodata));
      }));

  auto processObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();
  if (auto v = environment->Get("process.getState"))
    (*processObj)["getState"] = *v;
  if (auto v = environment->Get("process.sendSignal"))
    (*processObj)["sendSignal"] = *v;
  if (auto v = environment->Get("process.kill"))
    (*processObj)["kill"] = *v;
  if (auto v = environment->Get("process.exists"))
    (*processObj)["exists"] = *v;
  if (auto v = environment->Get("process.find"))
    (*processObj)["find"] = *v;
  if (auto v = environment->Get("process.nice"))
    (*processObj)["nice"] = *v;
  if (auto v = environment->Get("process.ionice"))
    (*processObj)["ionice"] = *v;
  if (auto v = environment->Get("SIGSTOP"))
    (*processObj)["SIGSTOP"] = *v;
  if (auto v = environment->Get("SIGCONT"))
    (*processObj)["SIGCONT"] = *v;
  if (auto v = environment->Get("SIGKILL"))
    (*processObj)["SIGKILL"] = *v;
  environment->Define("process", HavelValue(processObj));


  // === IO METHODS ===
  // Key state methods
  environment->Define(
      "io->getKeyState",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError("io->getKeyState() requires key name");
            std::string key = this->ValueToString(args[0]);
            return HavelValue(this->io->GetKeyState(key));
          }));

  environment->Define(
      "io->isShiftPressed",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->io->IsShiftPressed());
          }));

  environment->Define(
      "io->isCtrlPressed",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->io->IsCtrlPressed());
          }));

  environment->Define(
      "io->isAltPressed",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->io->IsAltPressed());
          }));

  environment->Define(
      "io->isWinPressed",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->io->IsWinPressed());
          }));

  // === AUDIO MANAGER METHODS ===
  // Volume control
  environment->Define(
      "audio.setVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError(
                  "audio.setVolume() requires volume (0.0-1.0)");
            if (args.size() >= 2) {
              std::string device = this->ValueToString(args[0]);
              double volume = ValueToNumber(args[1]);
              return HavelValue(this->audioManager->setVolume(device, volume));
            }
            double volume = ValueToNumber(args[0]);
            return HavelValue(this->audioManager->setVolume(volume));
          }));

  environment->Define(
      "audio.getVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!args.empty()) {
              std::string device = this->ValueToString(args[0]);
              return HavelValue(this->audioManager->getVolume(device));
            }
            return HavelValue(this->audioManager->getVolume());
          }));

  environment->Define(
      "audio.increaseVolume",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() >= 2) {
          std::string device = this->ValueToString(args[0]);
          double amount = ValueToNumber(args[1]);
          return HavelValue(this->audioManager->increaseVolume(device, amount));
        }
        if (args.size() == 1) {
          std::string device = this->ValueToString(args[0]);
          return HavelValue(this->audioManager->increaseVolume(device, 0.05));
        }
        double amount = args.empty() ? 0.05 : ValueToNumber(args[0]);
        return HavelValue(this->audioManager->increaseVolume(amount));
      }));

  environment->Define(
      "audio.decreaseVolume",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() >= 2) {
          std::string device = this->ValueToString(args[0]);
          double amount = ValueToNumber(args[1]);
          return HavelValue(this->audioManager->decreaseVolume(device, amount));
        }
        if (args.size() == 1) {
          std::string device = this->ValueToString(args[0]);
          return HavelValue(this->audioManager->decreaseVolume(device, 0.05));
        }
        double amount = args.empty() ? 0.05 : ValueToNumber(args[0]);
        return HavelValue(this->audioManager->decreaseVolume(amount));
      }));

  // Mute control
  environment->Define(
      "audio.toggleMute",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->audioManager->toggleMute());
          }));

  environment->Define(
      "audio.setMute",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError("audio.setMute() requires boolean");
            bool muted = args[0].get<bool>();
            return HavelValue(this->audioManager->setMute(muted));
          }));

  environment->Define(
      "audio.isMuted",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->audioManager->isMuted());
          }));

  environment->Define(
      "audio.getDevices",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        auto arr = std::make_shared<std::vector<HavelValue>>();
        const auto &devices = this->audioManager->getDevices();
        for (const auto &device : devices) {
          auto obj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*obj)["name"] = HavelValue(device.name);
          (*obj)["description"] = HavelValue(device.description);
          (*obj)["index"] = HavelValue(static_cast<double>(device.index));
          (*obj)["isDefault"] = HavelValue(device.isDefault);
          (*obj)["isMuted"] = HavelValue(device.isMuted);
          (*obj)["volume"] = HavelValue(device.volume);
          arr->push_back(HavelValue(obj));
        }
        return HavelValue(arr);
      }));

  environment->Define(
      "audio.findDeviceByIndex",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("audio.findDeviceByIndex() requires index");
        uint32_t index = static_cast<uint32_t>(args[0].get<double>());
        const auto *device = this->audioManager->findDeviceByIndex(index);
        if (!device) {
          return HavelValue(nullptr);
        }
        auto obj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*obj)["name"] = HavelValue(device->name);
        (*obj)["description"] = HavelValue(device->description);
        (*obj)["index"] = HavelValue(static_cast<double>(device->index));
        (*obj)["isDefault"] = HavelValue(device->isDefault);
        (*obj)["isMuted"] = HavelValue(device->isMuted);
        (*obj)["volume"] = HavelValue(device->volume);
        return HavelValue(obj);
      }));

  environment->Define(
      "audio.setDefaultOutputByIndex",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "audio.setDefaultOutputByIndex() requires index");
        uint32_t index = static_cast<uint32_t>(ValueToNumber(args[0]));
        const auto *device = this->audioManager->findDeviceByIndex(index);
        if (!device) {
          return HavelValue(false);
        }
        return HavelValue(this->audioManager->setDefaultOutput(device->name));
      }));

  environment->Define(
      "audio.findDeviceByName",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("audio.findDeviceByName() requires name");
        std::string name = this->ValueToString(args[0]);
        const auto *device = this->audioManager->findDeviceByName(name);
        if (!device) {
          return HavelValue(nullptr);
        }
        auto obj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*obj)["name"] = HavelValue(device->name);
        (*obj)["description"] = HavelValue(device->description);
        (*obj)["index"] = HavelValue(static_cast<double>(device->index));
        (*obj)["isDefault"] = HavelValue(device->isDefault);
        (*obj)["isMuted"] = HavelValue(device->isMuted);
        (*obj)["volume"] = HavelValue(device->volume);
        return HavelValue(obj);
      }));

  environment->Define(
      "audio.setDefaultOutput",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError(
                  "audio.setDefaultOutput() requires device name");
            std::string device = this->ValueToString(args[0]);
            return HavelValue(this->audioManager->setDefaultOutput(device));
          }));

  environment->Define(
      "audio.getDefaultOutput",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            return HavelValue(this->audioManager->getDefaultOutput());
          }));

  environment->Define(
      "audio.playTestSound",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            return HavelValue(this->audioManager->playTestSound());
          }));

  // Application volume control
  environment->Define(
      "audio.setAppVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.size() < 2)
              return HavelRuntimeError(
                  "audio.setAppVolume() requires (appName, volume)");
            std::string appName = this->ValueToString(args[0]);
            double volume = args[1].get<double>();
            return HavelValue(
                this->audioManager->setApplicationVolume(appName, volume));
          }));

  environment->Define(
      "audio.getAppVolume",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("audio.getAppVolume() requires appName");
        std::string appName = this->ValueToString(args[0]);
        return HavelValue(this->audioManager->getApplicationVolume(appName));
      }));

  environment->Define(
      "audio.increaseAppVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError(
                  "audio.increaseAppVolume() requires appName");
            std::string appName = this->ValueToString(args[0]);
            double amount = args.size() > 1 ? args[1].get<double>() : 0.05;
            return HavelValue(
                this->audioManager->increaseApplicationVolume(appName, amount));
          }));

  environment->Define(
      "audio.decreaseAppVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError(
                  "audio.decreaseAppVolume() requires appName");
            std::string appName = this->ValueToString(args[0]);
            double amount = args.size() > 1 ? args[1].get<double>() : 0.05;
            return HavelValue(
                this->audioManager->decreaseApplicationVolume(appName, amount));
          }));

  // Active window application volume
  environment->Define(
      "audio.setActiveAppVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError(
                  "audio.setActiveAppVolume() requires volume");
            double volume = args[0].get<double>();
            return HavelValue(
                this->audioManager->setActiveApplicationVolume(volume));
          }));

  environment->Define(
      "audio.getActiveAppVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->audioManager->getActiveApplicationVolume());
          }));

  environment->Define(
      "audio.increaseActiveAppVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            double amount = args.empty() ? 0.05 : args[0].get<double>();
            return HavelValue(
                this->audioManager->increaseActiveApplicationVolume(amount));
          }));

  environment->Define(
      "audio.decreaseActiveAppVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            double amount = args.empty() ? 0.05 : args[0].get<double>();
            return HavelValue(
                this->audioManager->decreaseActiveApplicationVolume(amount));
          }));

  // Get applications list
  environment->Define(
      "audio.getApplications",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        auto apps = this->audioManager->getApplications();
        auto arr = std::make_shared<std::vector<HavelValue>>();
        for (const auto &app : apps) {
          auto obj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*obj)["name"] = HavelValue(app.name);
          (*obj)["volume"] = HavelValue(app.volume);
          (*obj)["isMuted"] = HavelValue(app.isMuted);
          (*obj)["index"] = HavelValue(static_cast<double>(app.index));
          arr->push_back(HavelValue(obj));
        }
        return HavelValue(arr);
      }));

  // Create audio module
  auto audioMod =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();
  if (auto v = environment->Get("audio.setVolume"))
    (*audioMod)["setVolume"] = *v;
  if (auto v = environment->Get("audio.getVolume"))
    (*audioMod)["getVolume"] = *v;
  if (auto v = environment->Get("audio.increaseVolume"))
    (*audioMod)["increaseVolume"] = *v;
  if (auto v = environment->Get("audio.decreaseVolume"))
    (*audioMod)["decreaseVolume"] = *v;
  if (auto v = environment->Get("audio.toggleMute"))
    (*audioMod)["toggleMute"] = *v;
  if (auto v = environment->Get("audio.setMute"))
    (*audioMod)["setMute"] = *v;
  if (auto v = environment->Get("audio.isMuted"))
    (*audioMod)["isMuted"] = *v;
  if (auto v = environment->Get("audio.setAppVolume"))
    (*audioMod)["setAppVolume"] = *v;
  if (auto v = environment->Get("audio.getAppVolume"))
    (*audioMod)["getAppVolume"] = *v;
  if (auto v = environment->Get("audio.increaseAppVolume"))
    (*audioMod)["increaseAppVolume"] = *v;
  if (auto v = environment->Get("audio.decreaseAppVolume"))
    (*audioMod)["decreaseAppVolume"] = *v;
  if (auto v = environment->Get("audio.setActiveAppVolume"))
    (*audioMod)["setActiveAppVolume"] = *v;
  if (auto v = environment->Get("audio.getActiveAppVolume"))
    (*audioMod)["getActiveAppVolume"] = *v;
  if (auto v = environment->Get("audio.increaseActiveAppVolume"))
    (*audioMod)["increaseActiveAppVolume"] = *v;
  if (auto v = environment->Get("audio.decreaseActiveAppVolume"))
    (*audioMod)["decreaseActiveAppVolume"] = *v;
  if (auto v = environment->Get("audio.getApplications"))
    (*audioMod)["getApplications"] = *v;
  if (auto v = environment->Get("audio.getDevices"))
    (*audioMod)["getDevices"] = *v;
  if (auto v = environment->Get("audio.findDeviceByIndex"))
    (*audioMod)["findDeviceByIndex"] = *v;
  if (auto v = environment->Get("audio.findDeviceByName"))
    (*audioMod)["findDeviceByName"] = *v;
  if (auto v = environment->Get("audio.setDefaultOutputByIndex"))
    (*audioMod)["setDefaultOutputByIndex"] = *v;
  if (auto v = environment->Get("audio.setDefaultOutput"))
    (*audioMod)["setDefaultOutput"] = *v;
  if (auto v = environment->Get("audio.getDefaultOutput"))
    (*audioMod)["getDefaultOutput"] = *v;
  if (auto v = environment->Get("audio.playTestSound"))
    (*audioMod)["playTestSound"] = *v;
  environment->Define("audio", HavelValue(audioMod));

  // === BROWSER MODULE ===
  // Browser automation via Chrome DevTools Protocol
  auto browserMod =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  (*browserMod)["connect"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        std::string url = args.empty() ? "http://localhost:9222"
                                       : this->ValueToString(args[0]);
        return HavelValue(getBrowser().connect(url));
      }));

  (*browserMod)["disconnect"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        getBrowser().disconnect();
        return HavelValue(true);
      }));

  (*browserMod)["isConnected"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().isConnected());
      }));

  (*browserMod)["open"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("browser.open() requires URL");
        std::string url = this->ValueToString(args[0]);
        return HavelValue(getBrowser().open(url));
      }));

  (*browserMod)["newTab"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        std::string url = args.empty() ? "" : this->ValueToString(args[0]);
        return HavelValue(getBrowser().newTab(url));
      }));

  (*browserMod)["goto"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("browser.goto() requires URL");
        std::string url = this->ValueToString(args[0]);
        return HavelValue(getBrowser().gotoUrl(url));
      }));

  (*browserMod)["back"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().back());
      }));

  (*browserMod)["forward"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().forward());
      }));

  (*browserMod)["reload"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        bool ignoreCache = !args.empty() && args[0].get<double>() != 0;
        return HavelValue(getBrowser().reload(ignoreCache));
      }));

  (*browserMod)["click"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("browser.click() requires selector");
        std::string selector = this->ValueToString(args[0]);
        return HavelValue(getBrowser().click(selector));
      }));

  (*browserMod)["type"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("browser.type() requires (selector, text)");
        std::string selector = this->ValueToString(args[0]);
        std::string text = this->ValueToString(args[1]);
        return HavelValue(getBrowser().type(selector, text));
      }));

  (*browserMod)["setZoom"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "browser.setZoom() requires level (0.5-3.0)");
        double level = args[0].get<double>();
        return HavelValue(getBrowser().setZoom(level));
      }));

  (*browserMod)["getZoom"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().getZoom());
      }));

  (*browserMod)["resetZoom"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().resetZoom());
      }));

  (*browserMod)["eval"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("browser.eval() requires JavaScript code");
        std::string js = this->ValueToString(args[0]);
        return HavelValue(getBrowser().eval(js));
      }));

  (*browserMod)["screenshot"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        std::string path = args.empty() ? "" : this->ValueToString(args[0]);
        return HavelValue(getBrowser().screenshot(path));
      }));

  (*browserMod)["getCurrentUrl"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().getCurrentUrl());
      }));

  (*browserMod)["getTitle"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().getTitle());
      }));

  (*browserMod)["listTabs"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        auto tabs = getBrowser().listTabs();
        auto arr = std::make_shared<std::vector<HavelValue>>();
        for (const auto &tab : tabs) {
          auto obj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*obj)["id"] = HavelValue(static_cast<double>(tab.id));
          (*obj)["idStr"] = HavelValue(tab.idStr);
          (*obj)["title"] = HavelValue(tab.title);
          (*obj)["url"] = HavelValue(tab.url);
          (*obj)["type"] = HavelValue(tab.type);
          (*obj)["webSocketUrl"] = HavelValue(tab.webSocketUrl);
          arr->push_back(HavelValue(obj));
        }
        return HavelValue(arr);
      }));

  (*browserMod)["activate"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("browser.activate() requires tabId");
        int tabId = static_cast<int>(args[0].get<double>());
        return HavelValue(getBrowser().activate(tabId));
      }));

  (*browserMod)["close"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        int tabId = args.empty() ? -1 : static_cast<int>(args[0].get<double>());
        return HavelValue(getBrowser().closeTab(tabId));
      }));

  (*browserMod)["closeAll"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().closeAll());
      }));

  (*browserMod)["getActiveTab"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        BrowserTab tab = getBrowser().getActiveTab();
        auto tabObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*tabObj)["id"] = HavelValue(tab.id);
        (*tabObj)["title"] = HavelValue(tab.title);
        (*tabObj)["url"] = HavelValue(tab.url);
        (*tabObj)["type"] = HavelValue(tab.type);
        return HavelValue(tabObj);
      }));

  (*browserMod)["getActiveTabTitle"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().getActiveTabTitle());
      }));

  (*browserMod)["getActiveTabInfo"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().getActiveTabInfo());
      }));

  (*browserMod)["setActiveTab"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("browser.setActiveTab() requires tab index");
        int tabId = static_cast<int>(ValueToNumber(args[0]));
        getBrowser().setCurrentTabId(tabId);
        return HavelValue(true);
      }));

  // === New Browser Functions ===

  (*browserMod)["connectFirefox"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        int port =
            args.empty() ? 2828 : static_cast<int>(args[0].get<double>());
        return HavelValue(getBrowser().connectFirefox(port));
      }));

  (*browserMod)["setPort"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("browser.setPort() requires port number");
        int port = static_cast<int>(args[0].get<double>());
        getBrowser().setPort(port);
        return HavelValue(true);
      }));

  (*browserMod)["getPort"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().getPort());
      }));

  (*browserMod)["getBrowserType"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        auto type = getBrowser().getBrowserType();
        std::string typeName = type == BrowserType::Firefox    ? "firefox"
                               : type == BrowserType::Chrome   ? "chrome"
                               : type == BrowserType::Chromium ? "chromium"
                               : type == BrowserType::Edge     ? "edge"
                               : type == BrowserType::Brave    ? "brave"
                                                               : "unknown";
        return HavelValue(typeName);
      }));

  (*browserMod)["getOpenBrowsers"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        auto browsers = getBrowser().getOpenBrowsers();
        auto arr = std::make_shared<std::vector<HavelValue>>();
        for (const auto &b : browsers) {
          auto obj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*obj)["type"] =
              HavelValue(b.type == BrowserType::Firefox  ? "firefox"
                         : b.type == BrowserType::Chrome ? "chrome"
                                                         : "chromium");
          (*obj)["name"] = HavelValue(b.name);
          (*obj)["pid"] = HavelValue(static_cast<double>(b.pid));
          (*obj)["cdpPort"] = HavelValue(static_cast<double>(b.cdpPort));
          arr->push_back(HavelValue(obj));
        }
        return HavelValue(arr);
      }));

  (*browserMod)["getDefaultBrowser"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        auto browser = getBrowser().getDefaultBrowser();
        auto obj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*obj)["type"] =
            HavelValue(browser.type == BrowserType::Firefox  ? "firefox"
                       : browser.type == BrowserType::Chrome ? "chrome"
                                                             : "chromium");
        (*obj)["name"] = HavelValue(browser.name);
        (*obj)["path"] = HavelValue(browser.path);
        return HavelValue(obj);
      }));

  (*browserMod)["listWindows"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        auto windows = getBrowser().listWindows();
        auto arr = std::make_shared<std::vector<HavelValue>>();
        for (const auto &w : windows) {
          auto obj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*obj)["id"] = HavelValue(static_cast<double>(w.id));
          (*obj)["x"] = HavelValue(static_cast<double>(w.x));
          (*obj)["y"] = HavelValue(static_cast<double>(w.y));
          (*obj)["width"] = HavelValue(static_cast<double>(w.width));
          (*obj)["height"] = HavelValue(static_cast<double>(w.height));
          arr->push_back(HavelValue(obj));
        }
        return HavelValue(arr);
      }));

  (*browserMod)["listExtensions"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        auto extensions = getBrowser().listExtensions();
        auto arr = std::make_shared<std::vector<HavelValue>>();
        for (const auto &e : extensions) {
          auto obj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*obj)["id"] = HavelValue(e.id);
          (*obj)["name"] = HavelValue(e.name);
          (*obj)["version"] = HavelValue(e.version);
          (*obj)["enabled"] = HavelValue(e.enabled);
          arr->push_back(HavelValue(obj));
        }
        return HavelValue(arr);
      }));

  (*browserMod)["enableExtension"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "browser.enableExtension() requires extensionId");
        std::string extId = this->ValueToString(args[0]);
        return HavelValue(getBrowser().enableExtension(extId));
      }));

  (*browserMod)["disableExtension"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "browser.disableExtension() requires extensionId");
        std::string extId = this->ValueToString(args[0]);
        return HavelValue(getBrowser().disableExtension(extId));
      }));

  (*browserMod)["setWindowSize"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError(
              "browser.setWindowSize() requires (width, height)");
        int width = static_cast<int>(args[0].get<double>());
        int height = static_cast<int>(args[1].get<double>());
        return HavelValue(getBrowser().setWindowSize(-1, width, height));
      }));

  (*browserMod)["setWindowPosition"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError(
              "browser.setWindowPosition() requires (x, y)");
        int x = static_cast<int>(args[0].get<double>());
        int y = static_cast<int>(args[1].get<double>());
        return HavelValue(getBrowser().setWindowPosition(-1, x, y));
      }));

  (*browserMod)["maximizeWindow"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        int windowId =
            args.empty() ? -1 : static_cast<int>(args[0].get<double>());
        return HavelValue(getBrowser().maximizeWindow(windowId));
      }));

  (*browserMod)["minimizeWindow"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        int windowId =
            args.empty() ? -1 : static_cast<int>(args[0].get<double>());
        return HavelValue(getBrowser().minimizeWindow(windowId));
      }));

  (*browserMod)["fullscreenWindow"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        int windowId =
            args.empty() ? -1 : static_cast<int>(args[0].get<double>());
        return HavelValue(getBrowser().fullscreenWindow(windowId));
      }));

  environment->Define("browser", HavelValue(browserMod));

