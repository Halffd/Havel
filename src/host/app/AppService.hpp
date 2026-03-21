/*
 * AppService.hpp - Application service (merged header-only)
 * App info, args, env, exit
 */
#pragma once

#include <string>
#include <vector>
#include <cstdlib>
#include <unistd.h>

#include "../../havel-lang/runtime/Environment.hpp"

namespace havel::host {

using havel::HavelValue;
using havel::HavelRuntimeError;
// BuiltinFunction is already available from Environment.hpp

class AppService {
public:
    static std::string getName() { return "havel"; }
    static std::string getVersion() { return "1.0.0"; }
    static std::string getDescription() { return "Havel Window Manager"; }
    
    static std::vector<std::string> getArgs() { return {}; }
    static std::string getArg(int index) { auto args = getArgs(); return (index >= 0 && index < static_cast<int>(args.size())) ? args[index] : ""; }
    static int getArgCount() { return static_cast<int>(getArgs().size()); }
    static bool hasArg(const std::string& arg) { for (const auto& a : getArgs()) if (a == arg) return true; return false; }
    
    static void exit(int code = 0) { std::exit(code); }
    static void restart() {}
    
    static std::string getEnv(const std::string& name) {
        const char* value = std::getenv(name.c_str());
        return value ? value : "";
    }
    
    static bool setEnv(const std::string& name, const std::string& value) {
        return setenv(name.c_str(), value.c_str(), 1) == 0;
    }
    
    static std::vector<std::pair<std::string, std::string>> getAllEnv() {
        std::vector<std::pair<std::string, std::string>> result;
        for (char** env = ::environ; env && *env; ++env) {
            std::string var(*env);
            size_t pos = var.find('=');
            if (pos != std::string::npos) result.emplace_back(var.substr(0, pos), var.substr(pos + 1));
        }
        return result;
    }
    
    static int getPid() { return getpid(); }
    static int getPpid() { return getppid(); }
    static std::string getCwd() {
        char cwd[1024];
        return getcwd(cwd, sizeof(cwd)) ? cwd : "";
    }
};

// Module registration (co-located)
class Environment;
class IHostAPI;
struct HavelValue;
struct HavelRuntimeError;
template<typename T> struct BuiltinFunction;

inline void registerAppModule(havel::Environment& env, std::shared_ptr<havel::IHostAPI>) {
    auto appObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    (*appObj)["getName"] = HavelValue(::havel::BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(AppService::getName());
    }));
    
    (*appObj)["getVersion"] = HavelValue(::havel::BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(AppService::getVersion());
    }));
    
    (*appObj)["exit"] = HavelValue(::havel::BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        int code = args.empty() ? 0 : static_cast<int>(args[0].asNumber());
        AppService::exit(code);
        return HavelValue(nullptr);
    }));
    
    (*appObj)["getEnv"] = HavelValue(::havel::BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("getEnv requires name");
        return HavelValue(AppService::getEnv(args[0].asString()));
    }));
    
    (*appObj)["setEnv"] = HavelValue(::havel::BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) return HavelRuntimeError("setEnv requires (name, value)");
        return HavelValue(AppService::setEnv(args[0].asString(), args[1].asString()));
    }));
    
    (*appObj)["getPid"] = HavelValue(::havel::BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(static_cast<double>(AppService::getPid()));
    }));
    
    (*appObj)["getCwd"] = HavelValue(::havel::BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(AppService::getCwd());
    }));
    
    env.Define("app", HavelValue(appObj));
}

} // namespace havel::host
