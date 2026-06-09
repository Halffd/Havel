#include "Modules.hpp"
#include "../compiler/runtime/ConcurrencyBridge.hpp"
#include "../../host/module/ModularHostBridges.hpp"
#include "../../host/module/ExecutionPolicy.hpp"
#include "../../host/app/AppService.hpp"
#include "../../host/media/MediaService.hpp"
#include "../../host/network/NetworkService.hpp"
#include "../../host/ServiceRegistry.hpp"
#include "../compiler/vm/VMApi.hpp"
#include "../parser/Parser.h"
#include "../compiler/core/ByteCompiler.hpp"
#include "c/ModulePlugin.h"
#include "../../core/hotkey/HotkeyManager.hpp"
#include "../../extensions/HavelCAPI.h"
// ffi module loaded via plugin
#include <algorithm>

namespace havel {

extern "C" HavelAPI *getHavelAPI(void);

using compiler::Value;
using compiler::ObjectRef;
using compiler::BytecodeChunk;

std::unique_ptr<Modules> createModules(const HostContext &ctx) {
    return std::make_unique<Modules>(ctx);
}

static std::string getTypeName(const Value &value) {
    if (value.isNull()) return "null";
    if (value.isBool()) return "bool";
    if (value.isInt()) return "int";
    if (value.isDouble()) return "float";
    if (value.isStringValId() || value.isStringId()) return "string";
    if (value.isArrayId()) return "array";
    if (value.isObjectId()) return "object";
    if (value.isSetId()) return "set";
    if (value.isEnumId()) return "enum";
    if (value.isRangeId()) return "range";
    if (value.isThreadId()) return "thread";
    if (value.isIntervalId()) return "interval";
    if (value.isTimeoutId()) return "timeout";
    if (value.isErrorId()) return "error";
    if (value.isFunctionObjId()) return "function";
    if (value.isClosureId()) return "closure";
    if (value.isHostFuncId()) return "function";
    return "unknown";
}

Modules::Modules(const HostContext &ctx)
    : ctx_(&ctx), policy_(compiler::ExecutionPolicy::DefaultPolicy()) {
    extensionLoader_ = std::make_unique<Loader>();
    initBridges();
}

Modules::Modules(const HostContext &ctx, const compiler::ExecutionPolicy &policy)
    : ctx_(&ctx), policy_(policy) {
    extensionLoader_ = std::make_unique<Loader>();
    initBridges();
}

Modules::~Modules() { shutdown(); }

void Modules::shutdown() {
    mode_bindings_.clear();
    mode_definition_order_.clear();
    options_.host_functions.clear();
    vm_setup_callbacks_.clear();
    ioBridge_.reset();
    systemBridge_.reset();
    uiBridge_.reset();
    inputBridge_.reset();
    mediaBridge_.reset();
    audioBridge_.reset();
    mpvBridge_.reset();
    displayBridge_.reset();
    configBridge_.reset();
    modeBridge_.reset();
    timerBridge_.reset();
    appBridge_.reset();
    concurrencyBridge_.reset();
    automationBridge_.reset();
    browserBridge_.reset();
    toolsBridge_.reset();
    extensionLoader_.reset();
}

compiler::EventQueue *Modules::eventQueue() const {
    return concurrencyBridge_ ? concurrencyBridge_->eventQueue() : nullptr;
}

void Modules::checkTimers() {
    if (concurrencyBridge_) concurrencyBridge_->checkTimers();
}

bool Modules::loadModule(const std::string &name) {
    if (!ctx_ || !ctx_->vm) return false;
    auto plugin = extensionLoader().loadModulePlugin(name);
    return plugin.has_value();
}

bool Modules::import(const std::string &importSpec) {
    if (!ctx_ || !ctx_->vm) return false;
    auto result = ctx_->vm->loadModule(importSpec);
    return !result.isNull();
}

void Modules::registerModeCallbacks(const std::string &modeName,
                                     CallbackId conditionId,
                                     CallbackId enterId,
                                     CallbackId exitId) {
    ModeBinding binding;
    binding.modeName = modeName;
    binding.condition_id = conditionId;
    binding.enter_id = enterId;
    binding.exit_id = exitId;
    mode_bindings_[modeName] = std::move(binding);
    mode_definition_order_.push_back(modeName);
}

void Modules::initBridges() {
#ifdef HAVEL_CORE_PROFILE
    return;
#else
    ioBridge_ = std::make_unique<compiler::IOBridge>(ctx_);
    systemBridge_ = std::make_unique<compiler::SystemBridge>(ctx_);
    uiBridge_ = std::make_unique<compiler::UIBridge>(ctx_);
    inputBridge_ = std::make_unique<compiler::InputBridge>(ctx_);
    mediaBridge_ = std::make_unique<compiler::MediaBridge>(ctx_);
    audioBridge_ = std::make_unique<compiler::AudioBridge>(ctx_);
    mpvBridge_ = std::make_unique<compiler::MPVBridge>(ctx_);
    displayBridge_ = std::make_unique<compiler::DisplayBridge>(ctx_);
    configBridge_ = std::make_unique<compiler::ConfigBridge>(ctx_);
    modeBridge_ = std::make_unique<compiler::ModeBridge>(ctx_);
    timerBridge_ = std::make_unique<compiler::TimerBridge>(ctx_);
    appBridge_ = std::make_unique<compiler::AppBridge>(ctx_);
    concurrencyBridge_ = std::make_unique<compiler::ConcurrencyBridge>(*ctx_);
    const_cast<HostContext &>(*ctx_).eventQueue = concurrencyBridge_->eventQueue();
    if (ctx_->hotkeyManager) {
        ctx_->hotkeyManager->setEventQueue(concurrencyBridge_->eventQueue());
    }
    automationBridge_ = std::make_unique<compiler::AutomationBridge>(ctx_);
    browserBridge_ = std::make_unique<compiler::BrowserBridge>(ctx_);
    toolsBridge_ = std::make_unique<compiler::ToolsBridge>(ctx_);
#endif
}

void Modules::installHostFunctions() {
    auto *vm = ctx_->vm;
    options_.host_functions.reserve(64);
    vm_setup_callbacks_.reserve(16);

    options_.host_functions["type"] = [this](const std::vector<Value> &args) {
        if (args.empty()) return Value::makeNull();
        auto ref = ctx_->vm->getHeap().allocateString(getTypeName(args[0]));
        return Value::makeStringId(ref.id);
    };

  options_.host_functions["len"] = [this](const std::vector<Value> &args) {
    if (args.empty()) return Value::makeInt(0);
    return ctx_->vm->execLengthOp(args[0]);
  };

#ifdef HAVEL_CORE_PROFILE
    return;
#endif

    if (profile_ == InstallProfile::Full) {
        ioBridge_->install(options_);
        systemBridge_->install(options_);
        uiBridge_->install(options_);
        inputBridge_->install(options_);
        mediaBridge_->install(options_);
        audioBridge_->install(options_);
        mpvBridge_->install(options_);
        displayBridge_->install(options_);
        modeBridge_->install(options_);
        timerBridge_->install(options_);
        appBridge_->install(options_);
        concurrencyBridge_->install(options_);
        automationBridge_->install(options_);
        browserBridge_->install(options_);
        toolsBridge_->install(options_);
    }

    if (ctx_->windowMonitor && ctx_->vm) {
        auto plugin = extensionLoader_->loadModulePlugin("window");
        if (plugin) {
            compiler::VMApi api(*vm);
            plugin->register_fn(static_cast<void *>(&api));
        }
    }

    vm_setup_callbacks_.push_back([](compiler::VM &vm) {
        auto hotkeyObj = vm.createHostObject();
        for (const auto &name : {"register", "register_conditional", "remove_conditional",
                                  "enable_conditional", "disable_conditional", "set_condition",
                                  "evaluate_condition", "conditional_list", "trigger", "list"}) {
            std::string fn = std::string("hotkey.") + name;
            int idx = vm.getHostFunctionIndex(fn);
            if (idx >= 0) {
                vm.setHostObjectField(hotkeyObj, name, Value::makeHostFuncId(static_cast<uint32_t>(idx)));
            }
        }
        vm.setGlobal("hotkey", Value::makeObjectId(hotkeyObj.id));
    });

    options_.host_functions["extension.load"] =
        [this](const std::vector<Value> &args) {
            if (args.empty() || !args[0].isStringValId()) return Value::makeBool(false);
            extensionLoader_->loadExtensionWithInit(args[0].toString(), getHavelAPI());
            return Value(extensionLoader_->isLoaded(args[0].toString()));
        };
    options_.host_functions["extension.isLoaded"] =
        [this](const std::vector<Value> &args) {
            if (args.empty() || !args[0].isStringValId()) return Value::makeBool(false);
            return Value(extensionLoader_->isLoaded(args[0].toString()));
        };
    options_.host_functions["extension.list"] =
        [this](const std::vector<Value> &) {
            auto names = extensionLoader_->getLoadedExtensions();
            auto *vm = ctx_->vm;
            if (!vm) return Value::makeNull();
            auto arr = vm->createHostArray();
            for (const auto &name : names) {
                (void)name;
                vm->pushHostArrayValue(arr, Value::makeNull());
            }
            return Value::makeArrayId(arr.id);
        };
    options_.host_functions["extension.addSearchPath"] =
        [this](const std::vector<Value> &args) {
            if (args.empty() || !args[0].isStringValId()) return Value::makeBool(false);
            extensionLoader_->addSearchPath(args[0].toString());
            return Value::makeBool(true);
        };

    auto isCallable = [](const Value &v) {
        return v.isHostFuncId() || v.isClosureId() || v.isFunctionObjId();
    };

    options_.host_functions["any"] = [this, isCallable](const std::vector<Value> &args) {
        if (args.size() < 2 || !isCallable(args[1])) return Value::makeBool(false);
        auto iterRef = ctx_->vm->createIterator(args[0]);
        while (true) {
            auto result = ctx_->vm->iteratorNext(iterRef);
            if (!result.isObjectId()) return Value::makeBool(false);
            auto resultObjRef = compiler::ObjectRef{result.asObjectId(), true};
            auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
            if (doneVal.isBool() && doneVal.asBool()) return Value::makeBool(false);
            auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "second");
            auto predResult = ctx_->vm->callFunction(args[1], {valueVal});
            if (predResult.isBool() && predResult.asBool()) return Value::makeBool(true);
            if (predResult.isInt() && predResult.asInt() != 0) return Value::makeBool(true);
        }
    };

    options_.host_functions["all"] = [this, isCallable](const std::vector<Value> &args) {
        if (args.size() < 2 || !isCallable(args[1])) return Value::makeBool(false);
        auto iterRef = ctx_->vm->createIterator(args[0]);
        bool found_any = false;
        while (true) {
            auto result = ctx_->vm->iteratorNext(iterRef);
            if (!result.isObjectId()) break;
            auto resultObjRef = compiler::ObjectRef{result.asObjectId(), true};
            auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
            if (doneVal.isBool() && doneVal.asBool()) break;
            auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "second");
            found_any = true;
            auto predResult = ctx_->vm->callFunction(args[1], {valueVal});
            bool isTruthy = (predResult.isBool() && predResult.asBool()) ||
                            (predResult.isInt() && predResult.asInt() != 0);
            if (!isTruthy) return Value::makeBool(false);
        }
        return Value::makeBool(found_any);
    };

    options_.host_functions["eval"] = [this](const std::vector<Value> &args) {
        if (args.empty() || !ctx_ || !ctx_->vm) return Value::makeNull();
        std::string code = args[0].toString();
        if (code.empty()) return Value::makeNull();
        try {
            parser::Parser parser;
            auto program = parser.produceAST(code);
            if (!program || parser.hasErrors()) return Value::makeNull();
            compiler::ByteCompiler byteCompiler;
            auto chunk = byteCompiler.compile(*program);
            if (!chunk) return Value::makeNull();
            return ctx_->vm->execute(*chunk, "__main__");
        } catch (...) {
            return Value::makeNull();
        }
    };

    options_.host_functions["inspect"] = [this](const std::vector<Value> &args) {
        if (args.empty() || !ctx_ || !ctx_->vm) return Value::makeNull();
        auto ref = ctx_->vm->getHeap().allocateString(getTypeName(args[0]));
        return Value::makeStringId(ref.id);
    };

    options_.host_functions["prototypes"] = [this](const std::vector<Value> &args) {
        if (args.empty() || !ctx_ || !ctx_->vm) return Value::makeNull();
        auto arrRef = ctx_->vm->getHeap().allocateArray();
        auto *arr = ctx_->vm->getHeap().array(arrRef.id);
        if (!args[0].isObjectId()) return Value::makeArrayId(arrRef.id);
        auto *obj = ctx_->vm->getHeap().object(args[0].asObjectId());
        while (obj) {
            auto *nameVal = obj->get("__name");
            if (nameVal && nameVal->isStringId()) {
                arr->push_back(*nameVal);
            } else {
                arr->push_back(Value::makeStringId(ctx_->vm->getHeap().allocateString("<anonymous>").id));
            }
            compiler::GCHeap::ObjectEntry *next = nullptr;
            auto *protoVal = obj->get("__proto");
            if (protoVal && protoVal->isObjectId()) next = ctx_->vm->getHeap().object(protoVal->asObjectId());
            if (!next) {
                auto *parentVal = obj->get("__class");
                if (!parentVal) parentVal = obj->get("__parent");
                if (!parentVal) parentVal = obj->get("__struct");
                if (parentVal && parentVal->isObjectId()) next = ctx_->vm->getHeap().object(parentVal->asObjectId());
            }
            obj = next;
        }
        return Value::makeArrayId(arrRef.id);
    };

    options_.host_functions["proto"] = [this](const std::vector<Value> &args) {
        if (args.size() < 2 || !ctx_ || !ctx_->vm) return Value::makeNull();
        if (!args[0].isObjectId()) return args[0];
        ctx_->vm->getHeap().object(args[0].asObjectId())->set("__proto", args[1]);
        return args[0];
    };
    options_.host_functions["getproto"] = [this](const std::vector<Value> &args) {
        if (args.empty() || !ctx_ || !ctx_->vm || !args[0].isObjectId()) return Value::makeNull();
        auto *obj = ctx_->vm->getHeap().object(args[0].asObjectId());
        if (!obj) return Value::makeNull();
        auto *protoVal = obj->get("__proto");
        return protoVal ? *protoVal : Value::makeNull();
    };
    options_.host_functions["setproto"] = [this](const std::vector<Value> &args) {
        if (args.size() < 2 || !ctx_ || !ctx_->vm) return Value::makeNull();
        if (!args[0].isObjectId()) return args[0];
        ctx_->vm->getHeap().object(args[0].asObjectId())->set("__proto", args[1]);
        return args[0];
    };

    options_.host_functions["caller"] = [this](const std::vector<Value> &args) {
        if (!ctx_ || !ctx_->vm) return Value::makeNull();
        int depth = (!args.empty() && args[0].isInt()) ? static_cast<int>(args[0].asInt()) : 0;
        auto info = ctx_->vm->getCallerInfo(depth);
        auto objRef = ctx_->vm->getHeap().allocateObject();
        auto *obj = ctx_->vm->getHeap().object(objRef.id);
        obj->set("function", Value::makeStringId(ctx_->vm->getHeap().allocateString(info.function).id));
        obj->set("line", Value::makeInt(static_cast<int64_t>(info.line)));
        obj->set("column", Value::makeInt(static_cast<int64_t>(info.column)));
        obj->set("file", Value::makeStringId(ctx_->vm->getHeap().allocateString(info.file).id));
        return Value::makeObjectId(objRef.id);
    };

    options_.host_functions["defun"] = [this](const std::vector<Value> &args) {
        if (args.size() < 3 || !ctx_ || !ctx_->vm) return Value::makeNull();
        std::string typeName = args[0].toString();
        std::string methodName = args[1].toString();
        if (typeName.empty() || methodName.empty()) return Value::makeNull();
        Value fn = args[2];
        const compiler::BytecodeChunk *fnChunk = ctx_->vm->getCurrentChunk();
        std::string fullName = typeName + "." + methodName;
        uint32_t funcIdx = ctx_->vm->getHostFunctionIndex(fullName);
        options_.host_functions[fullName] = [this, fn, fnChunk](const std::vector<Value> &callArgs) {
            if (!ctx_ || !ctx_->vm) return Value::makeNull();
            try {
                const auto *savedChunk = ctx_->vm->getCurrentChunk();
                ctx_->vm->setCurrentChunk(fnChunk);
                auto result = ctx_->vm->call(fn, callArgs);
                ctx_->vm->setCurrentChunk(savedChunk);
                return result;
            } catch (...) {
                return Value::makeNull();
            }
        };
        ctx_->vm->registerPrototypeMethod(typeName, methodName, funcIdx);
        return Value::makeBool(true);
    };

    options_.host_functions["del"] = [this](const std::vector<Value> &args) {
        if (args.size() < 2 || !ctx_ || !ctx_->vm) return Value::makeNull();
        if (args[0].isObjectId() && args[1].isStringValId()) {
            auto *obj = ctx_->vm->getHeap().object(args[0].asObjectId());
            if (obj) {
                std::string key = args[1].toString();
                if (!key.empty()) {
                    obj->erase(key);
                    return Value::makeBool(true);
                }
            }
        }
        return Value::makeBool(false);
    };

    options_.host_functions["implements"] = [](const std::vector<Value> &) {
        return Value::makeBool(false);
    };
}

void Modules::installStdLib() {
    auto &vm = *ctx_->vm;
    compiler::VMApi api(vm);
    api.serviceRegistry = &host::ServiceRegistry::instance();
    vm.setServiceRegistry(&host::ServiceRegistry::instance());
    vm.setPluginLoader(extensionLoader_.get());

    extensionLoader_->addModulePaths();

    auto available = extensionLoader_->scanModules();

 for (auto &mod : available) {
 if (mod.eager) {
 auto plugin = extensionLoader_->loadModulePlugin(mod.name);
 if (plugin) {
 plugin->register_fn(static_cast<void *>(&api));
 }
 } else {
        std::string modName = mod.name;
            vm.registerLazyModule(modName, [this, modName](compiler::VMApi &a) {
                auto plugin = extensionLoader_->loadModulePlugin(modName);
                if (plugin) {
                    plugin->register_fn(static_cast<void *>(&a));
                }
            }, mod.aliases);
        }
    }

    // Brightness module loaded dynamically as .so plugin
    // Fallback: register ffi and config as lazy modules via plugin loader if scan missed them
    auto foundFfi = std::find_if(available.begin(), available.end(),
        [](const auto &m) { return m.name == "ffi"; });
    if (foundFfi == available.end()) {
        std::string ffiName = "ffi";
        vm.registerLazyModule(ffiName, [this, ffiName](compiler::VMApi &a) {
            auto plugin = extensionLoader_->loadModulePlugin(ffiName);
            if (plugin) {
                plugin->register_fn(static_cast<void *>(&a));
            }
        });
    }

    auto foundConfig = std::find_if(available.begin(), available.end(),
        [](const auto &m) { return m.name == "config"; });
    if (foundConfig == available.end()) {
        std::string configName = "config";
        vm.registerLazyModule(configName, [this, configName](compiler::VMApi &a) {
            auto plugin = extensionLoader_->loadModulePlugin(configName);
            if (plugin) {
                plugin->register_fn(static_cast<void *>(&a));
            }
        }, {"cfg", "conf"});
    }
}

 void Modules::install(InstallProfile profile, bool eagerBridges) {
    profile_ = profile;

    installStdLib();
    installHostFunctions();

    for (auto &setupFn : vm_setup_callbacks_) {
        setupFn(*ctx_->vm);
    }
}

} // namespace havel
