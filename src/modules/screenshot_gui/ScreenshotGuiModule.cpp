#include "ScreenshotGuiModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "host/ServiceRegistry.hpp"
#include "host/screenshot/ScreenshotService.hpp"
#include "utils/Logger.hpp"
#include "extensions/gui/screenshot_manager/ScreenshotManager.hpp"
#include "host/ui/UIManager.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

static const char* MODULE_MARKER = "__screenshot_gui_module";

static bool isModuleObject(const VMApi& api, const Value& val) {
    if (!val.isObjectId()) return false;
    auto marker = api.getField(val, MODULE_MARKER);
    return marker.isBool() && marker.asBool();
}

static std::vector<Value> stripReceiver(const VMApi& api, const std::vector<Value>& args) {
    if (!args.empty() && isModuleObject(api, args[0])) {
        return std::vector<Value>(args.begin() + 1, args.end());
    }
    return args;
}

static havel::ScreenshotManager* getScreenshotManager() {
    auto& uiManager = havel::host::UIManager::instance();
    auto* backend = uiManager.backend();
    if (!backend) {
        debug("ScreenshotGuiModule: No UI backend available");
        return nullptr;
    }
    void* ptr = backend->getScreenshotManager();
    return static_cast<havel::ScreenshotManager*>(ptr);
}

void registerScreenshotGuiModule(const VMApi& api) {
    HAVEL_BEGIN_MODULE("ScreenshotGui");

    // takeScreenshot() - Full desktop capture with UI preview
    HAVEL_REGISTER_FUNCTION(api, "screenshot_gui.takeScreenshot", [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        auto* manager = getScreenshotManager();
        if (!manager) return api.makeNull();
        
        QString path = manager->takeScreenshot();
        if (path.isEmpty()) return api.makeNull();
        return api.makeString(path.toStdString());
    });

    // takeRegionScreenshot() - Interactive region selector (async)
    HAVEL_REGISTER_FUNCTION(api, "screenshot_gui.takeRegionScreenshot", [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        auto* manager = getScreenshotManager();
        if (!manager) return api.makeNull();
        
        QString path = manager->takeRegionScreenshot();
        if (path.isEmpty()) return api.makeBool(true); // Started successfully
        return api.makeString(path.toStdString());
    });

    // takeScreenshotOfCurrentMonitor() - Current monitor capture
    HAVEL_REGISTER_FUNCTION(api, "screenshot_gui.takeScreenshotOfCurrentMonitor", [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        auto* manager = getScreenshotManager();
        if (!manager) return api.makeNull();
        
        QString path = manager->takeScreenshotOfCurrentMonitor();
        if (path.isEmpty()) return api.makeNull();
        return api.makeString(path.toStdString());
    });

    // captureRegion(x, y, width, height) - Programmatic region capture
    HAVEL_REGISTER_FUNCTION(api, "screenshot_gui.captureRegion", [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        auto* manager = getScreenshotManager();
        if (!manager) return api.makeNull();
        if (args.size() < 4) return api.makeNull();
        
        int x = args[0].isInt() ? static_cast<int>(args[0].asInt()) : 0;
        int y = args[1].isInt() ? static_cast<int>(args[1].asInt()) : 0;
        int width = args[2].isInt() ? static_cast<int>(args[2].asInt()) : 0;
        int height = args[3].isInt() ? static_cast<int>(args[3].asInt()) : 0;
        
        QRect region(x, y, width, height);
        QString path = manager->captureRegion(region);
        if (path.isEmpty()) return api.makeNull();
        return api.makeString(path.toStdString());
    });

    // getScreenshotDirectory() - Get the screenshot save directory
    HAVEL_REGISTER_FUNCTION(api, "screenshot_gui.getScreenshotDirectory", [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        auto* manager = getScreenshotManager();
        if (!manager) return api.makeNull();
        
        QString dir = manager->getScreenshotDirectory();
        return api.makeString(dir.toStdString());
    });

    // setScreenshotDirectory(path) - Set custom screenshot directory
    HAVEL_REGISTER_FUNCTION(api, "screenshot_gui.setScreenshotDirectory", [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        auto* manager = getScreenshotManager();
        if (!manager) return api.makeBool(false);
        if (args.empty()) return api.makeBool(false);
        
        std::string path = api.resolveString(args[0]);
        if (path.empty()) return api.makeBool(false);
        manager->setScreenshotDirectory(QString::fromStdString(path));
        return api.makeBool(true);
    });

    // showManager() - Show the screenshot manager window
    HAVEL_REGISTER_FUNCTION(api, "screenshot_gui.showManager", [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        auto* manager = getScreenshotManager();
        if (!manager) return api.makeBool(false);
        
        manager->showManager();
        return api.makeBool(true);
    });

    // hideManager() - Hide the screenshot manager window
    HAVEL_REGISTER_FUNCTION(api, "screenshot_gui.hideManager", [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        auto* manager = getScreenshotManager();
        if (!manager) return api.makeBool(false);
        
        manager->hideManager();
        return api.makeBool(true);
    });

    auto obj = api.makeObject();
    api.setGlobal("screenshot_gui", obj);
    api.setField(obj, MODULE_MARKER, Value::makeBool(true));
    api.setField(obj, "takeScreenshot", api.makeFunctionRef("screenshot_gui.takeScreenshot"));
    api.setField(obj, "takeRegionScreenshot", api.makeFunctionRef("screenshot_gui.takeRegionScreenshot"));
    api.setField(obj, "takeScreenshotOfCurrentMonitor", api.makeFunctionRef("screenshot_gui.takeScreenshotOfCurrentMonitor"));
    api.setField(obj, "captureRegion", api.makeFunctionRef("screenshot_gui.captureRegion"));
    api.setField(obj, "getScreenshotDirectory", api.makeFunctionRef("screenshot_gui.getScreenshotDirectory"));
    api.setField(obj, "setScreenshotDirectory", api.makeFunctionRef("screenshot_gui.setScreenshotDirectory"));
    api.setField(obj, "showManager", api.makeFunctionRef("screenshot_gui.showManager"));
    api.setField(obj, "hideManager", api.makeFunctionRef("screenshot_gui.hideManager"));

    HAVEL_END_MODULE();
}

} // namespace havel::modules

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(screenshot_gui, "1.0.0", "Interactive GUI screenshot module",
    havel::modules::registerScreenshotGuiModule(*api);
)
#endif
