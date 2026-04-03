#include "HotkeyModule.hpp"
#include "../../core/HotkeyManager.hpp"
#include "havel-lang/runtime/HostContext.hpp"
#include <mutex>
#include <unordered_map>

using havel::compiler::BytecodeValue;
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
  CallbackId callback;
  bool enabled;

  HotkeyContextData(const std::string &id_, const std::string &alias_,
                    const std::string &key_, const std::string &condition_,
                    const std::string &info_, CallbackId callback_,
                    bool enabled_ = true)
      : id(id_), alias(alias_), key(key_), condition(condition_), info(info_),
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
static BytecodeValue
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

  // Set properties on the object
  vm->setHostObjectField(contextObj, "id", BytecodeValue(hotkeyId));
  vm->setHostObjectField(contextObj, "alias", BytecodeValue(alias));
  vm->setHostObjectField(contextObj, "key", BytecodeValue(key));
  vm->setHostObjectField(contextObj, "condition", BytecodeValue(condition));
  vm->setHostObjectField(contextObj, "info", BytecodeValue(info));
  vm->setHostObjectField(contextObj, "enabled", BytecodeValue(enabled));

  return BytecodeValue(contextObj);
}

// Property getter functions
static BytecodeValue hotkey_getId(const std::vector<BytecodeValue> &args,
                                  const havel::HostContext *ctx) {

  if (args.empty() || !args[0].isObjectId()) {
    return BytecodeValue(nullptr);
  }

  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);

  // Get id field from object
  return vm->getHostObjectField(objRef, "id");
}

static BytecodeValue hotkey_getAlias(const std::vector<BytecodeValue> &args,
                                     const havel::HostContext *ctx) {

  if (args.empty() || !args[0].isObjectId()) {
    return BytecodeValue(nullptr);
  }

  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);

  return vm->getHostObjectField(objRef, "alias");
}

static BytecodeValue hotkey_getKey(const std::vector<BytecodeValue> &args,
                                   const havel::HostContext *ctx) {

  if (args.empty() || !args[0].isObjectId()) {
    return BytecodeValue(nullptr);
  }

  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);

  return vm->getHostObjectField(objRef, "key");
}

static BytecodeValue hotkey_getCondition(const std::vector<BytecodeValue> &args,
                                         const havel::HostContext *ctx) {

  if (args.empty() || !args[0].isObjectId()) {
    return BytecodeValue(nullptr);
  }

  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);

  return vm->getHostObjectField(objRef, "condition");
}

static BytecodeValue hotkey_getInfo(const std::vector<BytecodeValue> &args,
                                    const havel::HostContext *ctx) {

  if (args.empty() || !args[0].isObjectId()) {
    return BytecodeValue(nullptr);
  }

  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);

  return vm->getHostObjectField(objRef, "info");
}

static BytecodeValue hotkey_getCallback(const std::vector<BytecodeValue> &args,
                                        const havel::HostContext *ctx) {

  if (args.empty() || !args[0].isObjectId()) {
    return BytecodeValue(nullptr);
  }

  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);

  // Get id first, then use it to get the callback
  auto idValue = vm->getHostObjectField(objRef, "id");
  if (!idValue.isStringValId()) {
    return BytecodeValue(nullptr);
  }

  // TODO: string pool lookup
  auto hotkeyId = "<string:" + std::to_string(idValue.asStringValId()) + ">";
  auto *contextData = getHotkeyContextData(hotkeyId);

  if (!contextData) {
    return BytecodeValue(nullptr);
  }

  // Return callback as a function reference
  // This would need to be implemented as a proper function wrapper
  return BytecodeValue(nullptr); // Placeholder
}

// Method functions
static BytecodeValue hotkey_enable(const std::vector<BytecodeValue> &args,
                                   const havel::HostContext *ctx) {

  if (args.empty() || !args[0].isObjectId()) {
    return BytecodeValue(false);
  }

  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);

  // Get hotkey id
  auto idValue = vm->getHostObjectField(objRef, "id");
  if (!idValue.isStringValId()) {
    return BytecodeValue(false);
  }

  // TODO: string pool lookup
  auto hotkeyId = "<string:" + std::to_string(idValue.asStringValId()) + ">";
  auto *contextData = getHotkeyContextData(hotkeyId);

  if (!contextData) {
    return BytecodeValue(false);
  }

  // Enable hotkey through HotkeyManager
  if (ctx->hotkeyManager) {
    ctx->hotkeyManager->EnableHotkey(hotkeyId);
    contextData->enabled = true;
    vm->setHostObjectField(objRef, "enabled", Value::makeBool(true));
    return BytecodeValue(true);
  }

  return BytecodeValue(false);
}

static BytecodeValue hotkey_disable(const std::vector<BytecodeValue> &args,
                                    const havel::HostContext *ctx) {

  if (args.empty() || !args[0].isObjectId()) {
    return BytecodeValue(false);
  }

  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);

  // Get hotkey id
  auto idValue = vm->getHostObjectField(objRef, "id");
  if (!idValue.isStringValId()) {
    return BytecodeValue(false);
  }

  // TODO: string pool lookup
  auto hotkeyId = "<string:" + std::to_string(idValue.asStringValId()) + ">";
  auto *contextData = getHotkeyContextData(hotkeyId);

  if (!contextData) {
    return BytecodeValue(false);
  }

  // Disable hotkey through HotkeyManager
  if (ctx->hotkeyManager) {
    ctx->hotkeyManager->DisableHotkey(hotkeyId);
    contextData->enabled = false;
    vm->setHostObjectField(objRef, "enabled", Value::makeBool(false));
    return BytecodeValue(true);
  }

  return BytecodeValue(false);
}

static BytecodeValue hotkey_toggle(const std::vector<BytecodeValue> &args,
                                   const havel::HostContext *ctx) {

  if (args.empty() || !args[0].isObjectId()) {
    return BytecodeValue(false);
  }

  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);

  // Get hotkey id
  auto idValue = vm->getHostObjectField(objRef, "id");
  if (!idValue.isStringValId()) {
    return BytecodeValue(false);
  }

  // TODO: string pool lookup
  auto hotkeyId = "<string:" + std::to_string(idValue.asStringValId()) + ">";
  auto *contextData = getHotkeyContextData(hotkeyId);

  if (!contextData) {
    return BytecodeValue(false);
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
    vm->setHostObjectField(objRef, "enabled", Value::makeBool(newState));
    return BytecodeValue(true);
  }

  return BytecodeValue(false);
}

static BytecodeValue hotkey_remove(const std::vector<BytecodeValue> &args,
                                   const havel::HostContext *ctx) {

  if (args.empty() || !args[0].isObjectId()) {
    return BytecodeValue(false);
  }

  auto objRef = ObjectRef{args[0].asObjectId(), true};
  auto *vm = static_cast<VM *>(ctx->vm);

  // Get hotkey id
  auto idValue = vm->getHostObjectField(objRef, "id");
  if (!idValue.isStringValId()) {
    return BytecodeValue(false);
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
    return BytecodeValue(success);
  }

  return BytecodeValue(false);
}

void registerHotkeyModule(VMApi &api) {
  // Create hotkey prototype object (not used for method registration)
  auto hotkeyPrototype = api.makeObject();

  // Register property getters - using "Hotkey" as the type name
  api.registerPrototypeMethod("Hotkey", "id", "hotkey_getId");
  api.registerPrototypeMethod("Hotkey", "alias", "hotkey_getAlias");
  api.registerPrototypeMethod("Hotkey", "key", "hotkey_getKey");
  api.registerPrototypeMethod("Hotkey", "condition", "hotkey_getCondition");
  api.registerPrototypeMethod("Hotkey", "info", "hotkey_getInfo");
  api.registerPrototypeMethod("Hotkey", "callback", "hotkey_getCallback");

  // Register methods
  api.registerPrototypeMethod("Hotkey", "enable", "hotkey_enable");
  api.registerPrototypeMethod("Hotkey", "disable", "hotkey_disable");
  api.registerPrototypeMethod("Hotkey", "toggle", "hotkey_toggle");
  api.registerPrototypeMethod("Hotkey", "remove", "hotkey_remove");

  // Set prototype object as global "Hotkey" constructor
  api.setGlobal("Hotkey", hotkeyPrototype);
}

// Factory function to be called from hotkey registration
BytecodeValue HotkeyModule::createHotkeyContext(
    VM *vm, const std::string &hotkeyId, const std::string &alias,
    const std::string &key, const std::string &condition,
    const std::string &info, CallbackId callback) {

  return createHotkeyContextObject(vm, hotkeyId, alias, key, condition, info,
                                   callback);
}

} // namespace havel::stdlib
