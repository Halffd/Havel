/*
 * BrightnessModule.cpp
 * 
 * Brightness management module for Havel language.
 * Host binding - connects language to BrightnessManager.
 */
#include "../../host/HostContext.hpp"
#include "../runtime/Environment.hpp"
#include "core/BrightnessManager.hpp"

namespace havel::modules {

void registerBrightnessModule(Environment& env, HostContext& ctx) {
    if (!ctx.isValid() || !ctx.brightnessManager) {
        return;  // Skip if no brightness manager available
    }
    
    auto& bm = *ctx.brightnessManager;
    
    // Create brightnessManager module object
    auto brightnessObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    // Helper to convert value to number
    auto valueToNumber = [](const HavelValue& v) -> double {
        return v.asNumber();
    };
    
    // =========================================================================
    // Brightness get/set functions
    // =========================================================================
    
    (*brightnessObj)["getBrightness"] = HavelValue(BuiltinFunction([&bm](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelValue(bm.getBrightness());
        }
        int monitorIndex = static_cast<int>(valueToNumber(args[0]));
        return HavelValue(bm.getBrightness(monitorIndex));
    }));
    
    (*brightnessObj)["getTemperature"] = HavelValue(BuiltinFunction([&bm](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelValue(static_cast<double>(bm.getTemperature()));
        }
        int monitorIndex = static_cast<int>(valueToNumber(args[0]));
        return HavelValue(static_cast<double>(bm.getTemperature(monitorIndex)));
    }));
    
    (*brightnessObj)["setBrightness"] = HavelValue(BuiltinFunction([&bm](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("setBrightness() requires value or (monitorIndex, value)");
        }
        if (args.size() >= 2) {
            int monitorIndex = static_cast<int>(valueToNumber(args[0]));
            double brightness = valueToNumber(args[1]);
            bm.setBrightness(monitorIndex, brightness);
            return HavelValue(nullptr);
        }
        double brightness = valueToNumber(args[0]);
        bm.setBrightness(brightness);
        return HavelValue(nullptr);
    }));
    
    (*brightnessObj)["increaseBrightness"] = HavelValue(BuiltinFunction([&bm](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() >= 2) {
            int monitorIndex = static_cast<int>(valueToNumber(args[0]));
            double step = valueToNumber(args[1]);
            bm.increaseBrightness(monitorIndex, step);
            return HavelValue(nullptr);
        }
        double step = args.empty() ? 0.1 : valueToNumber(args[0]);
        bm.increaseBrightness(step);
        return HavelValue(nullptr);
    }));
    
    (*brightnessObj)["decreaseBrightness"] = HavelValue(BuiltinFunction([&bm](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() >= 2) {
            int monitorIndex = static_cast<int>(valueToNumber(args[0]));
            double step = valueToNumber(args[1]);
            bm.decreaseBrightness(monitorIndex, step);
            return HavelValue(nullptr);
        }
        double step = args.empty() ? 0.1 : valueToNumber(args[0]);
        bm.decreaseBrightness(step);
        return HavelValue(nullptr);
    }));
    
    (*brightnessObj)["setTemperature"] = HavelValue(BuiltinFunction([&bm](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("setTemperature() requires kelvin or (monitorIndex, kelvin)");
        }
        if (args.size() >= 2) {
            int monitorIndex = static_cast<int>(valueToNumber(args[0]));
            int kelvin = static_cast<int>(valueToNumber(args[1]));
            bm.setTemperature(monitorIndex, kelvin);
            return HavelValue(nullptr);
        }
        int kelvin = static_cast<int>(valueToNumber(args[0]));
        bm.setTemperature(kelvin);
        return HavelValue(nullptr);
    }));
    
    (*brightnessObj)["getShadowLift"] = HavelValue(BuiltinFunction([&bm](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelValue(bm.getShadowLift());
        }
        int monitorIndex = static_cast<int>(valueToNumber(args[0]));
        return HavelValue(bm.getShadowLift(monitorIndex));
    }));
    
    (*brightnessObj)["setShadowLift"] = HavelValue(BuiltinFunction([&bm](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("setShadowLift() requires lift or (monitorIndex, lift)");
        }
        if (args.size() >= 2) {
            int monitorIndex = static_cast<int>(valueToNumber(args[0]));
            double lift = valueToNumber(args[1]);
            bm.setShadowLift(monitorIndex, lift);
            return HavelValue(nullptr);
        }
        double lift = valueToNumber(args[0]);
        bm.setShadowLift(lift);
        return HavelValue(nullptr);
    }));
    
    (*brightnessObj)["decreaseGamma"] = HavelValue(BuiltinFunction([&bm](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("decreaseGamma() requires amount or (monitorIndex, amount)");
        }
        if (args.size() >= 2) {
            int monitorIndex = static_cast<int>(valueToNumber(args[0]));
            int amount = static_cast<int>(valueToNumber(args[1]));
            bm.decreaseGamma(monitorIndex, amount);
            return HavelValue(nullptr);
        }
        int amount = static_cast<int>(valueToNumber(args[0]));
        bm.decreaseGamma(amount);
        return HavelValue(nullptr);
    }));
    
    (*brightnessObj)["increaseGamma"] = HavelValue(BuiltinFunction([&bm](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("increaseGamma() requires amount or (monitorIndex, amount)");
        }
        if (args.size() >= 2) {
            int monitorIndex = static_cast<int>(valueToNumber(args[0]));
            int amount = static_cast<int>(valueToNumber(args[1]));
            bm.increaseGamma(monitorIndex, amount);
            return HavelValue(nullptr);
        }
        int amount = static_cast<int>(valueToNumber(args[0]));
        bm.increaseGamma(amount);
        return HavelValue(nullptr);
    }));
    
    (*brightnessObj)["setGammaRGB"] = HavelValue(BuiltinFunction([&bm](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 3) {
            return HavelRuntimeError("setGammaRGB() requires (r, g, b) or (monitorIndex, r, g, b)");
        }
        if (args.size() >= 4) {
            int monitorIndex = static_cast<int>(valueToNumber(args[0]));
            double r = valueToNumber(args[1]);
            double g = valueToNumber(args[2]);
            double b = valueToNumber(args[3]);
            bm.setGammaRGB(monitorIndex, r, g, b);
            return HavelValue(nullptr);
        }
        double r = valueToNumber(args[0]);
        double g = valueToNumber(args[1]);
        double b = valueToNumber(args[2]);
        bm.setGammaRGB(r, g, b);
        return HavelValue(nullptr);
    }));
    
    (*brightnessObj)["increaseTemperature"] = HavelValue(BuiltinFunction([&bm](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("increaseTemperature() requires amount or (monitorIndex, amount)");
        }
        if (args.size() >= 2) {
            int monitorIndex = static_cast<int>(valueToNumber(args[0]));
            int amount = static_cast<int>(valueToNumber(args[1]));
            bm.increaseTemperature(monitorIndex, amount);
            return HavelValue(nullptr);
        }
        int amount = static_cast<int>(valueToNumber(args[0]));
        bm.increaseTemperature(amount);
        return HavelValue(nullptr);
    }));
    
    (*brightnessObj)["decreaseTemperature"] = HavelValue(BuiltinFunction([&bm](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("decreaseTemperature() requires amount or (monitorIndex, amount)");
        }
        if (args.size() >= 2) {
            int monitorIndex = static_cast<int>(valueToNumber(args[0]));
            int amount = static_cast<int>(valueToNumber(args[1]));
            bm.decreaseTemperature(monitorIndex, amount);
            return HavelValue(nullptr);
        }
        int amount = static_cast<int>(valueToNumber(args[0]));
        bm.decreaseTemperature(amount);
        return HavelValue(nullptr);
    }));
    
    // Register brightnessManager module
    env.Define("brightnessManager", HavelValue(brightnessObj));
}

} // namespace havel::modules
