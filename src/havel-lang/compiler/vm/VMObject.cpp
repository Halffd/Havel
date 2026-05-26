#include "VM.hpp"
#include "VMInternals.hpp"
#include "../../../utils/Logger.hpp"
#include "../../utils/ErrorPrinter.hpp"

#include <set>
#include <sstream>

namespace havel::compiler {

void VM::registerHostFunction(const std::string &name,
                               BytecodeHostFunction function) {
  host_functions[name] = std::move(function);
  for (uint32_t i = 0; i < host_function_names_.size(); i++) {
    if (host_function_names_[i] == name) {
      host_function_globals_[name] = Value::makeHostFuncId(i);
      return;
    }
  }
    uint32_t idx = static_cast<uint32_t>(host_function_names_.size());
    host_function_names_.push_back(name);
    host_function_globals_[name] = Value::makeHostFuncId(idx);
}

void VM::registerHostFunction(const std::string &name, size_t arity,
BytecodeHostFunction function) {
  registerHostFunction(
    name,
    [arity, function = std::move(function), name](const std::vector<Value> &args) -> Value {
      if (args.size() != arity) {
                COMPILER_THROW("Host function '" + name + "' expects " +
                    std::to_string(arity) + " arguments, got " +
                    std::to_string(args.size()));
            }
            return function(args);
        });
}

bool VM::hasHostFunction(const std::string &name) const {
  return host_functions.find(name) != host_functions.end();
}

uint32_t VM::getHostFunctionIndex(const std::string &name) {
  // Find existing index
  for (uint32_t i = 0; i < host_function_names_.size(); i++) {
    if (host_function_names_[i] == name) {
      return i;
    }
  }
  // Register if not found (shouldn't happen for registered functions)
  uint32_t idx = static_cast<uint32_t>(host_function_names_.size());
  host_function_names_.push_back(name);
  return idx;
}

ObjectRef VM::createHostObject() {
  ObjectRef ref = heap_.allocateObject();
  Value root = Value::makeObjectId(ref.id);
  stack.push(root);
  stack.pop();
  return ref;
}

ArrayRef VM::createHostArray() {
  ArrayRef ref = heap_.allocateArray();
  Value root = Value::makeArrayId(ref.id);
  stack.push(root);
  stack.pop();
  return ref;
}

StringRef VM::createRuntimeString(std::string value) {
  StringRef ref = heap_.allocateString(std::move(value));
  Value root = Value::makeStringId(ref.id);
  stack.push(root);
  stack.pop();
  return ref;
}

size_t VM::getRuntimeStringLength(StringRef string_ref) {
  auto *str = heap_.string(string_ref.id);
  return str ? str->length() : 0;
}

void VM::setHostObjectField(ObjectRef object_ref, const std::string &key,
                            Value value) {
  auto *object = heap_.object(object_ref.id);
  if (!object) {
    COMPILER_THROW("setHostObjectField unknown object id");
  }
  (*object)[key] = std::move(value);
}

void VM::pushHostArrayValue(ArrayRef array_ref, Value value) {
  auto *array = heap_.array(array_ref.id);
  if (!array) {
    COMPILER_THROW("pushHostArrayValue unknown array id");
  }
  array->push_back(std::move(value));
}

// Array helpers
size_t VM::getHostArrayLength(ArrayRef array_ref) {
    auto *array = heap_.array(array_ref.id);
    if (!array)
        return 0;
    return array->size();
}

Value VM::execLengthOp(Value v) {
 if (v.isObjectId()) {
 Value opMethod = getHostObjectField(ObjectRef{v.asObjectId(), true}, "op_length");
 if (!opMethod.isNull() && (opMethod.isFunctionObjId() || opMethod.isClosureId() || opMethod.isHostFuncId())) {
 return callFunction(opMethod, {v});
 }
 // Try object.len prototype
 auto typeIt = prototypes_.find("object");
 if (typeIt != prototypes_.end()) {
 auto methodIt = typeIt->second.find("len");
 if (methodIt != typeIt->second.end()) {
 auto fnIt = host_functions.find(host_function_names_[methodIt->second]);
 if (fnIt != host_functions.end()) return fnIt->second({v});
 }
 }
 }
 if (v.isArrayId()) {
 return Value::makeInt(static_cast<int64_t>(getHostArrayLength(ArrayRef{v.asArrayId()})));
 } else if (v.isStringValId()) {
 if (current_chunk) {
 return Value::makeInt(static_cast<int64_t>(current_chunk->getString(v.asStringValId()).size()));
 }
 return Value::makeInt(0);
 } else if (v.isStringId()) {
 auto *s = heap_.string(v.asStringId());
 return Value::makeInt(s ? static_cast<int64_t>(s->size()) : 0);
 } else if (v.isSetId()) {
 // Try set.len prototype
 auto typeIt = prototypes_.find("set");
 if (typeIt != prototypes_.end()) {
 auto methodIt = typeIt->second.find("len");
 if (methodIt != typeIt->second.end()) {
 auto fnIt = host_functions.find(host_function_names_[methodIt->second]);
 if (fnIt != host_functions.end()) return fnIt->second({v});
 }
 }
 }
 COMPILER_THROW("Length operator requires array, string, object, or set");
}

Value VM::getHostArrayValue(ArrayRef array_ref, size_t index) {
  auto *array = heap_.array(array_ref.id);
  if (!array || index >= array->size())
    return Value::makeNull();
  return (*array)[index];
}

void VM::setHostArrayValue(ArrayRef array_ref, size_t index,
                           Value value) {
  auto *array = heap_.array(array_ref.id);
  if (!array)
    return;
  if (index >= array->size()) {
    // Extend array if needed
    while (array->size() <= index) {
      array->push_back(Value::makeNull());
    }
  }
  (*array)[index] = std::move(value);
}

Value VM::popHostArrayValue(ArrayRef array_ref) {
  auto *array = heap_.array(array_ref.id);
  if (!array || array->empty())
    return Value::makeNull();
  auto value = std::move(array->back());
  array->pop_back();
  return value;
}

void VM::insertHostArrayValue(ArrayRef array_ref, size_t index,
                              Value value) {
  auto *array = heap_.array(array_ref.id);
  if (!array)
    return;
  if (index > array->size())
    index = array->size();
  array->insert(array->begin() + index, std::move(value));
}

Value VM::removeHostArrayValue(ArrayRef array_ref, size_t index) {
  auto *array = heap_.array(array_ref.id);
  if (!array || index >= array->size())
    return Value::makeNull();
  auto value = std::move((*array)[index]);
  array->erase(array->begin() + index);
  return value;
}

// Range helpers
bool VM::isInRange(RangeRef range_ref, int64_t value) {
  auto *r = heap_.range(range_ref.id);
  if (!r)
    return false;

  if (r->step > 0) {
    return value >= r->start && value < r->end &&
           (value - r->start) % r->step == 0;
  } else {
    return value <= r->start && value > r->end &&
           (r->start - value) % (-r->step) == 0;
  }
}

// Enum helpers
uint32_t VM::registerEnumType(const std::string &name,
                              const std::vector<std::string> &variants) {
  return heap_.registerEnumType(name, variants);
}

EnumRef VM::createEnum(uint32_t typeId, uint32_t tag, size_t payloadCount) {
  return heap_.allocateEnum(typeId, tag, payloadCount);
}

uint32_t VM::getEnumTag(EnumRef enum_ref) { return heap_.enumTag(enum_ref.id); }

Value VM::getEnumPayload(EnumRef enum_ref, size_t index) {
  auto it = heap_.enums_.find(enum_ref.id);
  if (it == heap_.enums_.end() || index >= it->second.second.size()) {
    return Value::makeNull();
  }
  return it->second.second[index];
}

void VM::setEnumPayload(EnumRef enum_ref, size_t index,
                        const Value &value) {
  auto it = heap_.enums_.find(enum_ref.id);
  if (it == heap_.enums_.end() || index >= it->second.second.size()) {
    return;
  }
  it->second.second[index] = value;
}

uint32_t VM::getEnumPayloadCount(EnumRef enum_ref) {
  auto it = heap_.enums_.find(enum_ref.id);
  if (it == heap_.enums_.end())
    return 0;
  return static_cast<uint32_t>(it->second.second.size());
}

std::string VM::getEnumTypeName(uint32_t typeId) const {
  if (typeId >= heap_.enumTypes_.size())
    return "";
  return heap_.enumTypes_[typeId].name;
}

std::string VM::getEnumVariantName(uint32_t typeId, uint32_t tag) const {
  if (typeId >= heap_.enumTypes_.size())
    return "";
  const auto &variants = heap_.enumTypes_[typeId].variantNames;
  if (tag >= variants.size())
    return "";
  return variants[tag];
}

uint32_t VM::getEnumTypeVariantCount(uint32_t typeId) const {
  if (typeId >= heap_.enumTypes_.size())
    return 0;
  return static_cast<uint32_t>(heap_.enumTypes_[typeId].variantNames.size());
}

// Membership helpers
bool VM::arrayContains(ArrayRef array_ref, const Value &value) {
  auto *array = heap_.array(array_ref.id);
  if (!array)
    return false;
  for (const auto &item : *array) {
    if (valuesEqual(item, value)) {
      return true;
    }
  }
  return false;
}

bool VM::objectHasKey(ObjectRef object_ref, const std::string &key) {
  auto *object = heap_.object(object_ref.id);
  if (!object)
    return false;
  return object->find(key) != object->end();
}

// Iterator helpers
IteratorRef VM::createIterator(const Value &iterable) {
    IteratorRef ref;

    // _G globals mirror: iterate the live globals maps, not the stale heap snapshot
    if (iterable.isObjectId() && iterable.asObjectId() == globals_mirror_object_id_) {
        ref.id = heap_.createIterator(iterable);
        auto *iter = heap_.iterator(ref.id);
        if (iter) {
            iter->keys.clear();
            std::set<std::string> seen;
            for (const auto& [name, _] : globals) {
                if (seen.insert(name).second) iter->keys.push_back(name);
            }
            for (const auto& [name, _] : host_function_globals_) {
                if (seen.insert(name).second) iter->keys.push_back(name);
            }
        }
        return ref;
    }

    // Materialize StringValId to heap StringId so iterator can resolve characters
    Value effective = iterable;
    if (iterable.isStringValId()) {
        if (current_chunk) {
            const std::string &src = current_chunk->getString(iterable.asStringValId());
            auto strRef = heap_.allocateString(src);
            effective = Value::makeStringId(strRef.id);
        }
    }

    ref.id = heap_.createIterator(effective);
    return ref;
}

Value VM::iteratorNext(IteratorRef iterRef) {
  auto *iter = heap_.iterator(iterRef.id);
  if (!iter) {
    return heap_.iteratorNext(iterRef.id);
  }

  // _G globals mirror: resolve values from the live globals maps
  if (iter->iterable.isObjectId() && iter->iterable.asObjectId() == globals_mirror_object_id_) {
    if (iter->index >= iter->keys.size()) {
      auto resultObj = heap_.allocateObject();
      auto *obj = heap_.object(resultObj.id);
      (*obj)["first"] = Value::makeNull();
      (*obj)["second"] = Value::makeNull();
      (*obj)["done"] = Value::makeBool(true);
      return Value::makeObjectId(resultObj.id);
    }
    auto key = iter->keys[iter->index++];
    auto keyStrRef = heap_.allocateString(key);
    Value first = Value::makeStringId(keyStrRef.id);
    Value second = Value::makeNull();
    auto it = globals.find(key);
    if (it != globals.end()) {
      second = it->second;
    } else {
      auto hostIt = host_function_globals_.find(key);
      if (hostIt != host_function_globals_.end()) {
        second = hostIt->second;
      }
    }
    auto resultObj = heap_.allocateObject();
    auto *obj = heap_.object(resultObj.id);
    (*obj)["first"] = first;
    (*obj)["second"] = second;
    (*obj)["done"] = Value::makeBool(false);
    return Value::makeObjectId(resultObj.id);
  }

  return heap_.iteratorNext(iterRef.id);
}

// Object helpers
std::vector<std::string> VM::getHostObjectKeys(ObjectRef object_ref) {
  if (object_ref.id == globals_mirror_object_id_) {
    std::vector<std::string> keys;
    for (const auto &pair : globals) {
      keys.push_back(pair.first);
    }
    return keys;
  }
  auto *object = heap_.object(object_ref.id);
  if (!object)
    return {};
  std::vector<std::string> keys;
  keys.reserve(object->size());
  for (const auto &[key, value] : *object) {
    keys.push_back(key);
  }
  return keys;
}

std::vector<std::pair<std::string, Value>>
VM::getHostObjectEntries(ObjectRef object_ref) {
  auto *object = heap_.object(object_ref.id);
  if (!object)
    return {};
  return std::vector<std::pair<std::string, Value>>(object->begin(),
                                                            object->end());
}

bool VM::hasHostObjectField(ObjectRef object_ref, const std::string &key) {
  auto *object = heap_.object(object_ref.id);
  if (!object)
    return false;
  return object->find(key) != object->end();
}

Value VM::getHostObjectField(ObjectRef object_ref,
	const std::string &key) {
	auto *object = heap_.object(object_ref.id);
	if (!object)
		return Value::makeNull();

	auto it = object->find(key);
	if (it != object->end())
		return it->second;

	// Check prototype chain via __proto__, __class, or __struct
	for (const char *protoKey : {"__proto__", "__class", "__struct"}) {
		auto protoIt = object->find(protoKey);
		if (protoIt != object->end() && protoIt->second.isObjectId()) {
			return getHostObjectField(ObjectRef{protoIt->second.asObjectId(), true}, key);
		}
	}

	return Value::makeNull();
}

bool VM::deleteHostObjectField(ObjectRef object_ref, const std::string &key) {
  auto *object = heap_.object(object_ref.id);
  if (!object)
    return false;
  return object->erase(key) > 0;
}

void VM::setHostObjectFrozen(ObjectRef, bool) {
  // TODO: Implement object freezing
}

void VM::setHostObjectSealed(ObjectRef, bool) {
  // TODO: Implement object sealing
}

// Function calling
Value VM::callHostFunction(const Value &fn,
                                    const std::vector<Value> &args) {
  if (fn.isHostFuncId()) {
    uint32_t host_func_idx = fn.asHostFuncId();
    if (host_func_idx >= host_function_names_.size()) {
      COMPILER_THROW("Host function index out of range: " +
                     std::to_string(host_func_idx));
    }
    const std::string &name = host_function_names_[host_func_idx];
    auto it = host_functions.find(name);
    if (it == host_functions.end()) {
      COMPILER_THROW("Host function not found: " + name);
    }
    return it->second(args);
  }
  return Value::makeNull();
}

// General function call (handles both VM closures and host functions)
Value VM::callFunction(const Value &fn,
                               const std::vector<Value> &args) {
  // Host function - direct call
  if (fn.isHostFuncId()) {
    return callHostFunction(fn, args);
  }

  // VM Closure or FunctionObject - use synchronous call with state isolation
  return callFunctionSync(fn, args);
}

// Prototype system - methods on types
void VM::registerPrototypeMethod(const std::string &typeName,
                                 const std::string &methodName,
                                 uint32_t hostFuncIndex) {
  prototypes_[typeName][methodName] = hostFuncIndex;
}

void VM::registerPrototypeMethodByName(const std::string &typeName,
                                         const std::string &methodName,
                                         const std::string &funcName) {
    // Find the function index by name
    for (size_t i = 0; i < host_function_names_.size(); ++i) {
        if (host_function_names_[i] == funcName) {
        prototypes_[typeName][methodName] = static_cast<uint32_t>(i);
        return;
        }
    }
    // Not found - register with 0 (will be null)
    prototypes_[typeName][methodName] = 0;
}

std::optional<uint32_t>
VM::getPrototypeMethod(const Value &value,
                       const std::string &methodName) {
  // Determine type name (try both lowercase and capitalized)
  std::string typeName;
  std::string moduleName;
  if (value.isStringValId() || value.isStringId() || value.isRegexValId()) {
    typeName = "string";
    moduleName = "string";
  } else if (value.isArrayId()) {
    typeName = "array";
    moduleName = "array";
  } else if (value.isObjectId()) {
    typeName = "object";
    moduleName = "Object"; // Object module uses capital O
  } else {
    return std::nullopt;
  }

  // Look up method in prototype table
  auto typeIt = prototypes_.find(typeName);
  if (typeIt != prototypes_.end()) {
    auto methodIt = typeIt->second.find(methodName);
    if (methodIt != typeIt->second.end())
      return methodIt->second;
  }

  // Check if module object has this method (monkey-patching support)
  // Try both lowercase and capitalized module names
  for (const auto &modName : {moduleName, typeName}) {
    auto modIt = globals.find(modName);
    if (modIt != globals.end() && modIt->second.isObjectId()) {
      auto *modObj = heap_.object(modIt->second.asObjectId());
      if (modObj) {
        auto *val = modObj->get(methodName);
        if (val) {
          // If it's a host function, use it directly
          if (val->isHostFuncId()) {
            uint32_t idx = val->asHostFuncId();
            // Cache it in prototypes_ for faster future lookups
            if (typeIt == prototypes_.end()) {
              prototypes_[typeName][methodName] = idx;
            } else {
              typeIt->second[methodName] = idx;
            }
            return idx;
          }
          // If it's a closure or function object, we need to handle it differently
          // For now, store it as a special entry in prototypes_
          if (val->isClosureId() || val->isFunctionObjId()) {
            // Return a special index to indicate we need to call it differently
            // For now, just return the existing prototype index (will be handled by CALL_METHOD)
            return 0;
          }
        }
      }
    }
  }

  return std::nullopt;
}

std::vector<std::string> VM::getPrototypeMethods(const Value &value) {
  std::string typeName;
  if (value.isStringValId() || value.isRegexValId()) {
    typeName = "String";
  } else if (value.isArrayId()) {
    typeName = "Array";
  } else if (value.isObjectId()) {
    typeName = "Object";
  } else {
    return {};
  }

  auto typeIt = prototypes_.find(typeName);
  if (typeIt == prototypes_.end())
    return {};

  std::vector<std::string> methods;
  for (const auto &[name, fn] : typeIt->second) {
    methods.push_back(name);
  }
  return methods;
}

void VM::registerProtocol(const std::string &protocolName,
                          const std::unordered_set<std::string> &methods) {
  protocol_contracts_[protocolName] = methods;
}

void VM::registerProtocolImpl(const std::string &protocolName,
                              const std::string &typeName) {
  protocol_impls_[protocolName].insert(typeName);
  type_protocols_[typeName].insert(protocolName);
}

bool VM::typeImplementsProtocol(const std::string &typeName,
                                const std::string &protocolName) const {
  auto it = type_protocols_.find(typeName);
  if (it == type_protocols_.end()) return false;
  return it->second.count(protocolName) > 0;
}

std::unordered_set<std::string> VM::getTypeProtocols(const std::string &typeName) const {
  auto it = type_protocols_.find(typeName);
  if (it == type_protocols_.end()) return {};
  return it->second;
}

std::unordered_set<std::string> VM::getProtocolMethods(const std::string &protocolName) const {
  auto it = protocol_contracts_.find(protocolName);
  if (it == protocol_contracts_.end()) return {};
  return it->second;
}

std::vector<std::string> VM::getProtocolNames() const {
  std::vector<std::string> names;
  names.reserve(protocol_contracts_.size());
  for (const auto &[name, _] : protocol_contracts_) {
    names.push_back(name);
  }
  return names;
}

std::string VM::getTypeName(const Value &value) const {
  if (!value.isObjectId()) return "";
  const auto *obj = heap_.object(value.asObjectId());
  if (!obj) return "";
  // Check __name on the object itself (struct/class prototypes store __name)
  auto nameIt = obj->data.find("__name");
  if (nameIt != obj->data.end() && nameIt->second.isStringValId() && current_chunk) {
    return current_chunk->getString(nameIt->second.asStringValId());
  }
  // Walk __class/__struct prototype chain for __name
  for (const char *protoKey : {"__class", "__struct"}) {
    auto protoIt = obj->data.find(protoKey);
    if (protoIt != obj->data.end() && protoIt->second.isObjectId()) {
      const auto *proto = heap_.object(protoIt->second.asObjectId());
      if (proto) {
        auto pnIt = proto->data.find("__name");
        if (pnIt != proto->data.end() && pnIt->second.isStringValId() && current_chunk) {
          return current_chunk->getString(pnIt->second.asStringValId());
        }
      }
    }
  }
  return "";
}

std::optional<std::string> VM::getHostFunctionName(uint32_t index) const {
  if (index >= host_function_names_.size()) {
    return std::nullopt;
  }
  return host_function_names_[index];
}

uint64_t VM::pinExternalRoot(const Value &value) {
  return heap_.pinExternalRoot(value);
}

bool VM::unpinExternalRoot(uint64_t root_id) {
  return heap_.unpinExternalRoot(root_id);
}

std::optional<Value> VM::externalRootValue(uint64_t root_id) const {
  return heap_.externalRoot(root_id);
}


} // namespace havel::compiler
