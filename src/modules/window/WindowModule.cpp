/*
 * WindowModule.cpp
 * 
 * Window query module for Havel language.
 * Exposes window.* API for scripts.
 */
#include "WindowModule.hpp"
#include "../../window/WindowQuery.hpp"
#include <memory>

namespace havel::modules {

void registerWindowQueryModule(Environment& env, std::shared_ptr<IHostAPI>) {
    auto windowObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();

    // window.active - Get active window info
    (*windowObj)["active"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        auto info = WindowQuery::getActive();
        if (!info.valid) {
            return HavelValue(nullptr);
        }
        
        auto resultObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*resultObj)["id"] = HavelValue(static_cast<double>(info.windowId));
        (*resultObj)["title"] = HavelValue(info.title);
        (*resultObj)["class"] = HavelValue(info.windowClass);
        (*resultObj)["exe"] = HavelValue(info.exe);
        (*resultObj)["pid"] = HavelValue(static_cast<double>(info.pid));
        
        return HavelValue(resultObj);
    }));

    // window.any(condition) - Check if any window matches
    (*windowObj)["any"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("window.any() requires a condition function");
        }
        
        // TODO: Extract condition from args and evaluate
        // For now, just check active window
        auto info = WindowQuery::getActive();
        return HavelValue(info.valid);
    }));

    // window.count(condition) - Count matching windows
    (*windowObj)["count"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            // Return total window count
            return HavelValue(1.0);  // TODO: Implement proper count
        }
        
        // TODO: Extract condition and count
        return HavelValue(1.0);
    }));

    // window.filter(condition) - Filter matching windows
    (*windowObj)["filter"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        auto resultArray = std::make_shared<std::vector<HavelValue>>();
        
        // TODO: Extract condition and filter
        // For now, return active window if valid
        auto info = WindowQuery::getActive();
        if (info.valid) {
            auto winObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*winObj)["id"] = HavelValue(static_cast<double>(info.windowId));
            (*winObj)["title"] = HavelValue(info.title);
            (*winObj)["class"] = HavelValue(info.windowClass);
            (*winObj)["exe"] = HavelValue(info.exe);
            resultArray->push_back(HavelValue(winObj));
        }
        
        return HavelValue(resultArray);
    }));

    env.Define("window", HavelValue(windowObj));
}

} // namespace havel::modules
