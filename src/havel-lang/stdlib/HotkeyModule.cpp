#include "HotkeyModule.hpp"
#include "../../core/HotkeyManager.hpp"
#include "havel-lang/runtime/HostContext.hpp"
#include <mutex>
#include <unordered_map>

using havel::compiler::Value;
using havel::compiler::CallbackId;
using havel::compiler::ObjectRef;
using havel::compiler::VM;
using havel::compiler::VMApi;

namespace havel::stdlib {

// Hotkey context data structure
struct HotkeyContextData {
  std::string id;
  std::string alias;
  std::string key;
  std::string condition;
  std::string info;
  std::string combo;       // Full combo string like "^!t"
  std::string modifiers;   // Comma-separated modifiers like "ctrl,alt"
  std::string state;       // "enabled" or "disabled"
  int64_t addedAt;         // Timestamp when added (epoch ms)
  CallbackId callback;
  bool enabled;

  HotkeyContextData(const std::string &id_, const std::string &alias_,
                    const std::string &key_, const std::string &condition_,
                    const std::string &info_, CallbackId callback_,
                    bool enabled_ = true)
      : id(id_), alias(alias_), key(key_), condition(condition_), info(info_),
        combo(""), modifiers(""), state(enabled_ ? "enabled" : "disabled"),
        addedAt(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()),
        callback(callback_), enabled(enabled_) {}
};

// Global registry to map hotkey IDs to their context data
static std::unordered_map<std::string, std::unique_ptr<HotkeyContextData>>
    g_hotkeyContexts;
static std::mutex g_hotkeyContextsMutex;

// Helper function to get hotkey context data
static HotkeyContextData *getHotkeyContextData(const std::string &hotkeyId) {
  std::lock_guard<std::mutex> lock(g_hotkeyContextsMutex);
  auto it = g_hotkeyContexts.find(hotkeyId);
  return (it != g_hotkeyContexts.end()) ? it->second.get() : nullptr;
}

// Helper function to create hotkey context object
static Value
createHotkeyContextObject(VM *vm, const std::string &hotkeyId,
                          const std::string &alias, const std::string &key,
                          const std::string &condition, const std::string &info,
                          CallbackId callback, bool enabled = true) {

  auto contextObj = vm->createHostObject();

  // Store context data in our registry
  {
    std::lock_guard<std::mutex> lock(g_hotkeyContextsMutex);
    g_hotkeyContexts[hotkeyId] = std::make_unique<HotkeyContextData>(
        hotkeyId, alias, key, condition, info, callback, enabled);
  }

  // Set properties on the object with actual string values
  auto idStr = vm->createRuntimeString(hotkeyId);
  auto aliasStr = vm->createRuntimeString(alias);
  auto keyStr = vm->createRuntimeString(key);
  auto condStr = vm->createRuntimeString(condition);
  auto infoStr = vm->createRuntimeString(info);

  vm->setHostObjectField(contextObj, "id", Value::makeStringId(idStr.id));
  vm->setHostObjectField(contextObj, "alias", Value::makeStringId(aliasStr.id));
  vm->setHostObjectField(contextObj, "key", Value::makeStringId(keyStr.id));
  vm->setHostObjectField(contextObj, "condition", Value::makeStringId(condStr.id));
  vm->setHostObjectField(contextObj, "info", Value::makeStringId(infoStr.id));
  vm->setHostObjectField(contextObj, "enabled", Value::makeBool(enabled));

  return Value::makeObjectId(contextObj.id);
}

// Property getter functions
static Value hotkey_getId(const std::vector<Value> &args,
                                  const havel::HostContext *ctx) {

  if (args.empty() || !args[0].isObjectId()) {
    return Value::makeNull();
  }

  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);

  // Get id field from object
  return vm->getHostObjectField(objRef, "id");
}

static Value hotkey_getAlias(const std::vector<Value> &args,
                                     const havel::HostContext *ctx) {

  if (args.empty() || !args[0].isObjectId()) {
    return Value::makeNull();
  }

  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);

  return vm->getHostObjectField(objRef, "alias");
}

static Value hotkey_getKey(const std::vector<Value> &args,
                                   const havel::HostContext *ctx) {

  if (args.empty() || !args[0].isObjectId()) {
    return Value::makeNull();
  }

  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);

  return vm->getHostObjectField(objRef, "key");
}

static Value hotkey_getCondition(const std::vector<Value> &args,
                                         const havel::HostContext *ctx) {

  if (args.empty() || !args[0].isObjectId()) {
    return Value::makeNull();
  }

  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);

  return vm->getHostObjectField(objRef, "condition");
}

static Value hotkey_getInfo(const std::vector<Value> &args,
                                    const havel::HostContext *ctx) {

  if (args.empty() || !args[0].isObjectId()) {
    return Value::makeNull();
  }

  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);

  return vm->getHostObjectField(objRef, "info");
}

static Value hotkey_getCallback(const std::vector<Value> &args,
                                        const havel::HostContext *ctx) {

  if (args.empty() || !args[0].isObjectId()) {
    return Value::makeNull();
  }

  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);

  // Get id first, then use it to get the callback
  auto idValue = vm->getHostObjectField(objRef, "id");
  if (!idValue.isStringValId()) {
    return Value(nullptr);
  }

  // TODO: string pool lookup
  auto hotkeyId = "<string:" + std::to_string(idValue.asStringValId()) + ">";
  auto *contextData = getHotkeyContextData(hotkeyId);

  if (!contextData) {
    return Value(nullptr);
  }

  // Return callback as a function reference
  // This would need to be implemented as a proper function wrapper
  return Value::makeNull(); // Placeholder
}

static Value hotkey_getState(const std::vector<Value> &args,
                                     const havel::HostContext *ctx) {
  if (args.empty() || !args[0].isObjectId()) {
    return Value::makeNull();
  }
  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);
  auto idValue = vm->getHostObjectField(objRef, "id");
  if (!idValue.isStringValId()) return Value::makeNull();
  auto hotkeyId = "<string:" + std::to_string(idValue.asStringValId()) + ">";
  auto *contextData = getHotkeyContextData(hotkeyId);
  if (!contextData) return Value::makeNull();
  auto strRef = vm->createRuntimeString(contextData->state);
  return Value::makeStringId(strRef.id);
}

static Value hotkey_getModifiers(const std::vector<Value> &args,
                                         const havel::HostContext *ctx) {
  if (args.empty() || !args[0].isObjectId()) {
    return Value::makeNull();
  }
  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);
  auto idValue = vm->getHostObjectField(objRef, "id");
  if (!idValue.isStringValId()) return Value::makeNull();
  auto hotkeyId = "<string:" + std::to_string(idValue.asStringValId()) + ">";
  auto *contextData = getHotkeyContextData(hotkeyId);
  if (!contextData) return Value::makeNull();
  auto strRef = vm->createRuntimeString(contextData->modifiers);
  return Value::makeStringId(strRef.id);
}

static Value hotkey_getCombo(const std::vector<Value> &args,
                                     const havel::HostContext *ctx) {
  if (args.empty() || !args[0].isObjectId()) {
    return Value::makeNull();
  }
  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);
  auto idValue = vm->getHostObjectField(objRef, "id");
  if (!idValue.isStringValId()) return Value::makeNull();
  auto hotkeyId = "<string:" + std::to_string(idValue.asStringValId()) + ">";
  auto *contextData = getHotkeyContextData(hotkeyId);
  if (!contextData) return Value::makeNull();
  auto strRef = vm->createRuntimeString(contextData->combo);
  return Value::makeStringId(strRef.id);
}

static Value hotkey_getAddedAt(const std::vector<Value> &args,
                                       const havel::HostContext *ctx) {
  if (args.empty() || !args[0].isObjectId()) {
    return Value::makeNull();
  }
  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);
  auto idValue = vm->getHostObjectField(objRef, "id");
  if (!idValue.isStringValId()) return Value::makeNull();
  auto hotkeyId = "<string:" + std::to_string(idValue.asStringValId()) + ">";
  auto *contextData = getHotkeyContextData(hotkeyId);
  if (!contextData) return Value::makeNull();
  return Value::makeInt(contextData->addedAt);
}

// Method functions
static Value hotkey_enable(const std::vector<Value> &args,
                                   const havel::HostContext *ctx) {

  if (args.empty() || !args[0].isObjectId()) {
    return Value::makeBool(false);
  }

  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);

  // Get hotkey id
  auto idValue = vm->getHostObjectField(objRef, "id");
  if (!idValue.isStringValId()) {
    return Value::makeBool(false);
  }

  // TODO: string pool lookup
  auto hotkeyId = "<string:" + std::to_string(idValue.asStringValId()) + ">";
  auto *contextData = getHotkeyContextData(hotkeyId);

  if (!contextData) {
    return Value::makeBool(false);
  }

  // Enable hotkey through HotkeyManager
  if (ctx->hotkeyManager) {
    ctx->hotkeyManager->EnableHotkey(hotkeyId);
    contextData->enabled = true;
    contextData->state = "enabled";
    vm->setHostObjectField(objRef, "enabled", Value::makeBool(true));
    auto strRef = vm->createRuntimeString("enabled");
    vm->setHostObjectField(objRef, "state", Value::makeStringId(strRef.id));
    return Value::makeBool(true);
  }

  return Value::makeBool(false);
}

static Value hotkey_disable(const std::vector<Value> &args,
                                    const havel::HostContext *ctx) {

  if (args.empty() || !args[0].isObjectId()) {
    return Value::makeBool(false);
  }

  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);

  // Get hotkey id
  auto idValue = vm->getHostObjectField(objRef, "id");
  if (!idValue.isStringValId()) {
    return Value::makeBool(false);
  }

  // TODO: string pool lookup
  auto hotkeyId = "<string:" + std::to_string(idValue.asStringValId()) + ">";
  auto *contextData = getHotkeyContextData(hotkeyId);

  if (!contextData) {
    return Value::makeBool(false);
  }

  // Disable hotkey through HotkeyManager
  if (ctx->hotkeyManager) {
    ctx->hotkeyManager->DisableHotkey(hotkeyId);
    contextData->enabled = false;
    contextData->state = "disabled";
    vm->setHostObjectField(objRef, "enabled", Value::makeBool(false));
    auto strRef = vm->createRuntimeString("disabled");
    vm->setHostObjectField(objRef, "state", Value::makeStringId(strRef.id));
    return Value::makeBool(true);
  }

  return Value::makeBool(false);
}

static Value hotkey_toggle(const std::vector<Value> &args,
                                   const havel::HostContext *ctx) {

  if (args.empty() || !args[0].isObjectId()) {
    return Value::makeBool(false);
  }

  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);

  // Get hotkey id
  auto idValue = vm->getHostObjectField(objRef, "id");
  if (!idValue.isStringValId()) {
    return Value::makeBool(false);
  }

  // TODO: string pool lookup
  auto hotkeyId = "<string:" + std::to_string(idValue.asStringValId()) + ">";
  auto *contextData = getHotkeyContextData(hotkeyId);

  if (!contextData) {
    return Value::makeBool(false);
  }

  // Toggle hotkey through HotkeyManager
  if (ctx->hotkeyManager) {
    bool newState = !contextData->enabled;
    if (newState) {
      ctx->hotkeyManager->EnableHotkey(hotkeyId);
    } else {
      ctx->hotkeyManager->DisableHotkey(hotkeyId);
    }
    contextData->enabled = newState;
    contextData->state = newState ? "enabled" : "disabled";
    vm->setHostObjectField(objRef, "enabled", Value::makeBool(newState));
    auto strRef = vm->createRuntimeString(contextData->state);
    vm->setHostObjectField(objRef, "state", Value::makeStringId(strRef.id));
    return Value::makeBool(true);
  }

  return Value(false);
}

static Value hotkey_remove(const std::vector<Value> &args,
                                   const havel::HostContext *ctx) {

  if (args.empty() || !args[0].isObjectId()) {
    return Value::makeBool(false);
  }

  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);

  // Get hotkey id
  auto idValue = vm->getHostObjectField(objRef, "id");
  if (!idValue.isStringValId()) {
    return Value::makeBool(false);
  }

  // TODO: string pool lookup
  auto hotkeyId = "<string:" + std::to_string(idValue.asStringValId()) + ">";

  // Remove hotkey through HotkeyManager
  if (ctx->hotkeyManager) {
    bool success = ctx->hotkeyManager->RemoveHotkey(hotkeyId);
    if (success) {
      // Remove from our context registry
      std::lock_guard<std::mutex> lock(g_hotkeyContextsMutex);
      g_hotkeyContexts.erase(hotkeyId);
    }
    return Value::makeBool(success);
  }

  return Value::makeBool(false);
}

void registerHotkeyModule(VMApi &api) {
  // Create hotkey prototype object (not used for method registration)
  auto hotkeyPrototype = api.makeObject();

  // Register property getters - using "Hotkey" as the type name
  api.registerPrototypeMethodByName("Hotkey", "id", "hotkey_getId");
  api.registerPrototypeMethodByName("Hotkey", "alias", "hotkey_getAlias");
  api.registerPrototypeMethodByName("Hotkey", "key", "hotkey_getKey");
  api.registerPrototypeMethodByName("Hotkey", "condition", "hotkey_getCondition");
  api.registerPrototypeMethodByName("Hotkey", "info", "hotkey_getInfo");
  api.registerPrototypeMethodByName("Hotkey", "callback", "hotkey_getCallback");
  api.registerPrototypeMethodByName("Hotkey", "state", "hotkey_getState");
  api.registerPrototypeMethodByName("Hotkey", "modifiers", "hotkey_getModifiers");
  api.registerPrototypeMethodByName("Hotkey", "combo", "hotkey_getCombo");
  api.registerPrototypeMethodByName("Hotkey", "addedAt", "hotkey_getAddedAt");

  // Register methods
  api.registerPrototypeMethodByName("Hotkey", "enable", "hotkey_enable");
  api.registerPrototypeMethodByName("Hotkey", "disable", "hotkey_disable");
  api.registerPrototypeMethodByName("Hotkey", "toggle", "hotkey_toggle");
  api.registerPrototypeMethodByName("Hotkey", "remove", "hotkey_remove");

  // Set prototype object as global "Hotkey" constructor
  api.setGlobal("Hotkey", hotkeyPrototype);
}

// Factory function to be called from hotkey registration
Value HotkeyModule::createHotkeyContext(
    VM *vm, const std::string &hotkeyId, const std::string &alias,
    const std::string &key, const std::string &condition,
    const std::string &info, CallbackId callback) {

  return createHotkeyContextObject(vm, hotkeyId, alias, key, condition, info,
                                   callback);
}

} // namespace havel::stdlib
