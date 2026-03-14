/*
 * SystemModule.cpp
 * 
 * System information module for Havel language.
 * Provides CPU, memory, OS, and temperature information.
 */
#include "SystemModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "core/system/CpuInfo.hpp"
#include "core/system/MemoryInfo.hpp"
#include "core/system/OSInfo.hpp"
#include "core/system/Temperature.hpp"

namespace havel::modules {

void registerSystemModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI) {
    (void)hostAPI;  // System info doesn't need host context
    
    // =========================================================================
    // CPU information
    // =========================================================================
    
    auto cpuObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    (*cpuObj)["cores"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(static_cast<double>(CpuInfo::cores()));
    }));
    
    (*cpuObj)["threads"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(static_cast<double>(CpuInfo::threads()));
    }));
    
    (*cpuObj)["name"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(CpuInfo::name());
    }));
    
    (*cpuObj)["frequency"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(CpuInfo::frequency());
    }));
    
    (*cpuObj)["usage"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(CpuInfo::usage());
    }));
    
    (*cpuObj)["usagePerCore"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        auto usage = CpuInfo::usagePerCore();
        auto arr = std::make_shared<std::vector<HavelValue>>();
        for (double u : usage) {
            arr->push_back(HavelValue(u));
        }
        return HavelValue(arr);
    }));
    
    env.Define("system.cpu", HavelValue(cpuObj));
    
    // =========================================================================
    // Memory information
    // =========================================================================
    
    auto memObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    (*memObj)["total"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(static_cast<double>(MemoryInfo::total()));
    }));
    
    (*memObj)["used"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(static_cast<double>(MemoryInfo::used()));
    }));
    
    (*memObj)["free"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(static_cast<double>(MemoryInfo::free()));
    }));
    
    (*memObj)["swapTotal"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(static_cast<double>(MemoryInfo::swapTotal()));
    }));
    
    (*memObj)["swapUsed"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(static_cast<double>(MemoryInfo::swapUsed()));
    }));
    
    (*memObj)["swapFree"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(static_cast<double>(MemoryInfo::swapFree()));
    }));
    
    env.Define("system.memory", HavelValue(memObj));
    
    // =========================================================================
    // OS information
    // =========================================================================
    
    auto osObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    (*osObj)["name"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(OSInfo::name());
    }));
    
    (*osObj)["distro"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(OSInfo::distro());
    }));
    
    (*osObj)["kernel"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(OSInfo::kernel());
    }));
    
    (*osObj)["hostname"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(OSInfo::hostname());
    }));
    
    (*osObj)["arch"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(OSInfo::arch());
    }));
    
    (*osObj)["uptime"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(OSInfo::uptime());
    }));
    
    env.Define("system.os", HavelValue(osObj));
    
    // =========================================================================
    // Temperature information
    // =========================================================================
    
    auto tempObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    (*tempObj)["cpu"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(Temperature::cpu());
    }));
    
    (*tempObj)["gpu"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(Temperature::gpu());
    }));
    
    (*tempObj)["all"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        auto sensors = Temperature::all();
        auto arr = std::make_shared<std::vector<HavelValue>>();
        
        for (const auto& sensor : sensors) {
            auto sensorObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*sensorObj)["name"] = HavelValue(sensor.name);
            (*sensorObj)["temperature"] = HavelValue(sensor.temperature);
            arr->push_back(HavelValue(sensorObj));
        }
        
        return HavelValue(arr);
    }));
    
    env.Define("system.temperature", HavelValue(tempObj));
}

} // namespace havel::modules
