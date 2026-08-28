/* BrightnessModule.cpp - VM-native stdlib module (brightness control) */
#include "BrightnessModule.hpp"

#include "core/BrightnessManager.hpp"
#include "havel-lang/core/Value.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"

using havel::core::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

// Global BrightnessManager instance
static havel::BrightnessManager *g_brightnessManager = nullptr;

void registerBrightnessModule(const VMApi &api) {
  // Get or create the singleton BrightnessManager instance
  if (!g_brightnessManager) {
    g_brightnessManager = new havel::BrightnessManager();
  }
  auto &bm = *g_brightnessManager;

  // Register individual functions
  api.registerFunction("brightness.get",
                       [](const std::vector<Value> &args) -> Value {
    if (!g_brightnessManager) return Value::makeNull();
    if (args.empty()) {
      return Value(g_brightnessManager->getBrightness());
    }
    std::string monitor = args[0].toString();
    return Value(g_brightnessManager->getBrightness(monitor));
  });

  api.registerFunction("brightness.set",
                       [](const std::vector<Value> &args) -> Value {
    if (!g_brightnessManager) return Value::makeBool(false);
    if (args.size() < 1) return Value::makeBool(false);
    double brightness = args[0].asDouble();
    if (args.size() == 1) {
      return Value(g_brightnessManager->setBrightness(brightness));
    }
    std::string monitor = args[1].toString();
    return Value(g_brightnessManager->setBrightness(monitor, brightness));
  });

  api.registerFunction("brightness.getRGB",
                       [&api](const std::vector<Value> &args) -> Value {
    if (!g_brightnessManager) return Value::makeNull();
    havel::BrightnessManager::RGBColor rgb;
    if (args.empty()) {
      rgb = g_brightnessManager->getGammaRGB();
    } else {
      std::string monitor = args[0].toString();
      rgb = g_brightnessManager->getGammaRGB(monitor);
    }
    auto obj = api.makeObject();
    api.setField(obj, "red", Value(rgb.red));
    api.setField(obj, "green", Value(rgb.green));
    api.setField(obj, "blue", Value(rgb.blue));
    return obj;
  });

  api.registerFunction("brightness.setRGB",
                       [](const std::vector<Value> &args) -> Value {
    if (!g_brightnessManager) return Value::makeBool(false);
    if (args.size() < 3) return Value::makeBool(false);
    double red = args[0].asDouble();
    double green = args[1].asDouble();
    double blue = args[2].asDouble();
    if (args.size() == 3) {
      return Value(g_brightnessManager->setGammaRGB(red, green, blue));
    }
    std::string monitor = args[3].toString();
    return Value(g_brightnessManager->setGammaRGB(monitor, red, green, blue));
  });

  api.registerFunction("brightness.getTemperature",
                       [](const std::vector<Value> &args) -> Value {
    if (!g_brightnessManager) return Value::makeNull();
    if (args.empty()) {
      return Value(static_cast<int64_t>(g_brightnessManager->getTemperature()));
    }
    std::string monitor = args[0].toString();
    return Value(static_cast<int64_t>(g_brightnessManager->getTemperature(monitor)));
  });

  api.registerFunction("brightness.setTemperature",
                       [](const std::vector<Value> &args) -> Value {
    if (!g_brightnessManager) return Value::makeBool(false);
    if (args.size() < 1) return Value::makeBool(false);
    int kelvin = static_cast<int>(args[0].asInt());
    if (args.size() == 1) {
      return Value(g_brightnessManager->setTemperature(kelvin));
    }
    std::string monitor = args[1].toString();
    return Value(g_brightnessManager->setTemperature(monitor, kelvin));
  });

  api.registerFunction("brightness.getShadowLift",
                       [](const std::vector<Value> &args) -> Value {
    if (!g_brightnessManager) return Value::makeNull();
    if (args.empty()) {
      return Value(g_brightnessManager->getShadowLift());
    }
    std::string monitor = args[0].toString();
    return Value(g_brightnessManager->getShadowLift(monitor));
  });

  api.registerFunction("brightness.setShadowLift",
                       [](const std::vector<Value> &args) -> Value {
    if (!g_brightnessManager) return Value::makeBool(false);
    if (args.size() < 1) return Value::makeBool(false);
    double lift = args[0].asDouble();
    if (args.size() == 1) {
      return Value(g_brightnessManager->setShadowLift(lift));
    }
    std::string monitor = args[1].toString();
    return Value(g_brightnessManager->setShadowLift(monitor, lift));
  });

  api.registerFunction("brightness.increase",
                       [](const std::vector<Value> &args) -> Value {
    if (!g_brightnessManager) return Value::makeBool(false);
    double amount = 0.02;
    if (!args.empty()) amount = args[0].asDouble();
    if (args.size() == 1) {
      return Value(g_brightnessManager->increaseBrightness(amount));
    }
    std::string monitor = args[1].toString();
    return Value(g_brightnessManager->increaseBrightness(monitor, amount));
  });

  api.registerFunction("brightness.decrease",
                       [](const std::vector<Value> &args) -> Value {
    if (!g_brightnessManager) return Value::makeBool(false);
    double amount = 0.02;
    if (!args.empty()) amount = args[0].asDouble();
    if (args.size() == 1) {
      return Value(g_brightnessManager->decreaseBrightness(amount));
    }
    std::string monitor = args[1].toString();
    return Value(g_brightnessManager->decreaseBrightness(monitor, amount));
  });

  api.registerFunction("brightness.increaseTemperature",
                       [](const std::vector<Value> &args) -> Value {
    if (!g_brightnessManager) return Value::makeBool(false);
    int amount = 200;
    if (!args.empty()) amount = static_cast<int>(args[0].asInt());
    if (args.size() == 1) {
      return Value(g_brightnessManager->increaseTemperature(amount));
    }
    std::string monitor = args[1].toString();
    return Value(g_brightnessManager->increaseTemperature(monitor, amount));
  });

  api.registerFunction("brightness.decreaseTemperature",
                       [](const std::vector<Value> &args) -> Value {
    if (!g_brightnessManager) return Value::makeBool(false);
    int amount = 200;
    if (!args.empty()) amount = static_cast<int>(args[0].asInt());
    if (args.size() == 1) {
      return Value(g_brightnessManager->decreaseTemperature(amount));
    }
    std::string monitor = args[1].toString();
    return Value(g_brightnessManager->decreaseTemperature(monitor, amount));
  });

  api.registerFunction("brightness.getMonitors",
                       [&api](const std::vector<Value> &args) -> Value {
    (void)args;
    if (!g_brightnessManager) return api.makeArray();
    auto monitors = g_brightnessManager->getConnectedMonitors();
    auto arr = api.makeArray();
    for (const auto &m : monitors) {
      api.push(arr, api.makeString(m));
    }
    return arr;
  });

  // Create brightness module object
  auto brightnessObj = api.makeObject();
  api.setField(brightnessObj, "get", api.makeFunctionRef("brightness.get"));
  api.setField(brightnessObj, "set", api.makeFunctionRef("brightness.set"));
  api.setField(brightnessObj, "getRGB", api.makeFunctionRef("brightness.getRGB"));
  api.setField(brightnessObj, "setRGB", api.makeFunctionRef("brightness.setRGB"));
  api.setField(brightnessObj, "getTemperature", api.makeFunctionRef("brightness.getTemperature"));
  api.setField(brightnessObj, "setTemperature", api.makeFunctionRef("brightness.setTemperature"));
  api.setField(brightnessObj, "getShadowLift", api.makeFunctionRef("brightness.getShadowLift"));
  api.setField(brightnessObj, "setShadowLift", api.makeFunctionRef("brightness.setShadowLift"));
  api.setField(brightnessObj, "increase", api.makeFunctionRef("brightness.increase"));
  api.setField(brightnessObj, "decrease", api.makeFunctionRef("brightness.decrease"));
  api.setField(brightnessObj, "increaseTemperature", api.makeFunctionRef("brightness.increaseTemperature"));
  api.setField(brightnessObj, "decreaseTemperature", api.makeFunctionRef("brightness.decreaseTemperature"));
  api.setField(brightnessObj, "getMonitors", api.makeFunctionRef("brightness.getMonitors"));

  // Add constants
  api.setField(brightnessObj, "DEFAULT_BRIGHTNESS_AMOUNT", Value(0.02));
  api.setField(brightnessObj, "DEFAULT_TEMP_AMOUNT", Value(200));

  api.setGlobal("brightness", brightnessObj);
}

} // namespace havel::stdlib

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_EAGER_A3(brightness, "1.0.0", "Brightness control module", "display", "xrandr", "x11",
havel::stdlib::registerBrightnessModule(*api);
)
