/*
 * ClipboardModule.cpp
 * 
 * Clipboard module for Havel language.
 * Host binding - connects language to ClipboardManager and Qt clipboard.
 */
#include "../../host/HostContext.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "gui/ClipboardManager.hpp"
#include <QClipboard>
#include <QGuiApplication>
#include <QMetaObject>

namespace havel::modules {

void registerClipboardModule(Environment& env, HostContext& ctx) {
    // Basic clipboard functions don't need ClipboardManager
    // Create clipboard module object
    auto clip = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    // =========================================================================
    // Basic clipboard functions (always available)
    // =========================================================================
    
    (*clip)["get"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        QClipboard* clipboard = QGuiApplication::clipboard();
        return HavelValue(clipboard->text().toStdString());
    }));
    
    (*clip)["in"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        QClipboard* clipboard = QGuiApplication::clipboard();
        return HavelValue(clipboard->text().toStdString());
    }));
    
    (*clip)["out"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        QClipboard* clipboard = QGuiApplication::clipboard();
        return HavelValue(clipboard->text().toStdString());
    }));
    
    (*clip)["set"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("clipboard.set() requires text");
        }
        std::string text = args[0].isString() ? args[0].asString() : 
            std::to_string(static_cast<int>(args[0].asNumber()));
        QClipboard* clipboard = QGuiApplication::clipboard();
        clipboard->setText(QString::fromStdString(text));
        return HavelValue(true);
    }));
    
    (*clip)["clear"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        QClipboard* clipboard = QGuiApplication::clipboard();
        clipboard->clear();
        return HavelValue(nullptr);
    }));
    
    // =========================================================================
    // ClipboardManager functions (only if available)
    // =========================================================================
    
    if (ctx.clipboardManager) {
        auto& cm = *ctx.clipboardManager;
        auto clipMgrObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        
        // Show/hide clipboard manager window
        (*clipMgrObj)["show"] = HavelValue(BuiltinFunction([&cm](const std::vector<HavelValue>&) -> HavelResult {
            QMetaObject::invokeMethod(&cm, "showAndFocus", Qt::QueuedConnection);
            return HavelValue(nullptr);
        }));
        
        (*clipMgrObj)["hide"] = HavelValue(BuiltinFunction([&cm](const std::vector<HavelValue>&) -> HavelResult {
            cm.hide();
            return HavelValue(nullptr);
        }));
        
        (*clipMgrObj)["toggle"] = HavelValue(BuiltinFunction([&cm](const std::vector<HavelValue>&) -> HavelResult {
            QMetaObject::invokeMethod(&cm, "toggleVisibility", Qt::QueuedConnection);
            return HavelValue(nullptr);
        }));
        
        // Clipboard history operations
        (*clipMgrObj)["history"] = HavelValue(BuiltinFunction([&cm](const std::vector<HavelValue>&) -> HavelResult {
            auto historyArray = std::make_shared<std::vector<HavelValue>>();
            int count = cm.getHistoryCount();
            for (int i = 0; i < count; ++i) {
                QString item = cm.getHistoryItem(i);
                historyArray->push_back(HavelValue(item.toStdString()));
            }
            return HavelValue(historyArray);
        }));
        
        (*clipMgrObj)["count"] = HavelValue(BuiltinFunction([&cm](const std::vector<HavelValue>&) -> HavelResult {
            return HavelValue(cm.getHistoryCount());
        }));
        
        (*clipMgrObj)["getItem"] = HavelValue(BuiltinFunction([&cm](const std::vector<HavelValue>& args) -> HavelResult {
            if (args.empty()) {
                return HavelRuntimeError("clipboardmanager.getItem() requires index");
            }
            int index = static_cast<int>(args[0].asNumber());
            QString item = cm.getHistoryItem(index);
            return HavelValue(item.toStdString());
        }));
        
        (*clipMgrObj)["clear"] = HavelValue(BuiltinFunction([&cm](const std::vector<HavelValue>&) -> HavelResult {
            QMetaObject::invokeMethod(&cm, "clearHistoryPublic", Qt::QueuedConnection);
            return HavelValue(nullptr);
        }));
        
        (*clipMgrObj)["copy"] = HavelValue(BuiltinFunction([&cm](const std::vector<HavelValue>& args) -> HavelResult {
            if (args.empty()) {
                return HavelRuntimeError("clipboardmanager.copy() requires text");
            }
            std::string text = args[0].isString() ? args[0].asString() : 
                std::to_string(static_cast<int>(args[0].asNumber()));
            QMetaObject::invokeMethod(&cm, [&cm, text]() {
                cm.addToHistoryPublic(QString::fromStdString(text));
            }, Qt::QueuedConnection);
            return HavelValue(nullptr);
        }));
        
        (*clipMgrObj)["paste"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
            QClipboard* clipboard = QGuiApplication::clipboard();
            return HavelValue(clipboard->text().toStdString());
        }));
        
        (*clipMgrObj)["enableHotkeys"] = HavelValue(BuiltinFunction([&cm](const std::vector<HavelValue>&) -> HavelResult {
            cm.initializeHotkeys();
            return HavelValue(nullptr);
        }));
        
        env.Define("clipboardmanager", HavelValue(clipMgrObj));
    }
    
    // Register clipboard module
    env.Define("clipboard", HavelValue(clip));
}

} // namespace havel::modules
