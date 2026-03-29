#include "GC.hpp"

#include <chrono>

namespace havel::compiler {

void GCHeap::reset() {
  closures_.clear();
  arrays_.clear();
  objects_.clear();
  sets_.clear();
  ranges_.clear();
  iterators_.clear();
  next_closure_id_ = 1;
  next_array_id_ = 1;
  next_object_id_ = 1;
  next_set_id_ = 1;
  next_range_id_ = 1;
  next_iterator_id_ = 1;
  allocations_since_last_ = 0;
  external_roots_.clear();
  next_external_root_id_ = 1;
  collections_ = 0;
  last_pause_ns_ = 0;
}

ClosureRef GCHeap::allocateClosure(RuntimeClosure closure) {
  const uint32_t id = next_closure_id_++;
  closures_.emplace(id, std::move(closure));
  return ClosureRef{.id = id};
}

ArrayRef GCHeap::allocateArray() {
  const uint32_t id = next_array_id_++;
  arrays_[id] = {};
  return ArrayRef{.id = id};
}

ObjectRef GCHeap::allocateObject() {
  const uint32_t id = next_object_id_++;
  objects_[id] = {};
  return ObjectRef{.id = id};
}

SetRef GCHeap::allocateSet() {
  const uint32_t id = next_set_id_++;
  sets_[id] = {};
  return SetRef{.id = id};
}

RangeRef GCHeap::allocateRange(int64_t start, int64_t end, int64_t step) {
  const uint32_t id = next_range_id_++;
  Range range;
  range.start = start;
  range.end = end;
  range.step = step;
  ranges_[id] = range;
  return RangeRef{.id = id};
}

GCHeap::Range *GCHeap::range(uint32_t id) {
  auto it = ranges_.find(id);
  return it == ranges_.end() ? nullptr : &it->second;
}

const GCHeap::Range *GCHeap::range(uint32_t id) const {
  auto it = ranges_.find(id);
  return it == ranges_.end() ? nullptr : &it->second;
}

IteratorRef GCHeap::allocateIterator(const BytecodeValue &iterable) {
  const uint32_t id = next_iterator_id_++;
  Iterator iter;
  iter.iterable = iterable;
  iter.index = 0;

  // For objects, pre-compute keys
  if (std::holds_alternative<ObjectRef>(iterable)) {
    auto *obj = object(std::get<ObjectRef>(iterable).id);
    if (obj) {
      for (const auto &[key, _] : *obj) {
        iter.keys.push_back(key);
      }
    }
  } else if (std::holds_alternative<SetRef>(iterable)) {
    auto *setObj = set(std::get<SetRef>(iterable).id);
    if (setObj) {
      for (const auto &[key, present] : *setObj) {
        if (std::holds_alternative<bool>(present) && !std::get<bool>(present)) {
          continue;
        }
        iter.keys.push_back(key);
      }
    }
  }

  iterators_[id] = std::move(iter);
  return IteratorRef{.id = id};
}

GCHeap::Iterator *GCHeap::iterator(uint32_t id) {
  auto it = iterators_.find(id);
  return it == iterators_.end() ? nullptr : &it->second;
}

const GCHeap::Iterator *GCHeap::iterator(uint32_t id) const {
  auto it = iterators_.find(id);
  return it == iterators_.end() ? nullptr : &it->second;
}

uint32_t GCHeap::createIterator(const BytecodeValue &iterable) {
  IteratorRef ref = allocateIterator(iterable);
  return ref.id;
}

// Struct type registration
uint32_t GCHeap::registerStructType(const std::string &name,
                                    const std::vector<std::string> &fields) {
  uint32_t id = static_cast<uint32_t>(structTypes_.size()) +
                1; // Start at 1 to avoid 0=nullptr issue
  structTypes_.push_back(StructType{name, fields});
  return id;
}

// Struct allocation
StructRef GCHeap::allocateStruct(uint32_t typeId, size_t fieldCount) {
  const uint32_t id = next_array_id_++;
  structs_[id] = std::vector<BytecodeValue>(fieldCount, BytecodeValue(nullptr));
  return StructRef{.id = id, .typeId = typeId};
}

std::optional<uint32_t>
GCHeap::findStructTypeId(const std::string &name) const {
  for (uint32_t i = 0; i < structTypes_.size(); ++i) {
    if (structTypes_[i].name == name) {
      return i + 1; // Return ID with +1 offset
    }
  }
  return std::nullopt;
}

size_t GCHeap::structFieldCount(uint32_t typeId) const {
  if (typeId == 0 || typeId > structTypes_.size()) {
    return 0;
  }
  return structTypes_[typeId - 1].fieldNames.size();
}

std::optional<size_t> GCHeap::structFieldIndex(uint32_t typeId,
                                               const std::string &field) const {
  if (typeId == 0 || typeId > structTypes_.size()) {
    return std::nullopt;
  }
  const auto &fields = structTypes_[typeId - 1].fieldNames;
  for (size_t i = 0; i < fields.size(); ++i) {
    if (fields[i] == field) {
      return i;
    }
  }
  return std::nullopt;
}

// Class type registration with parent
uint32_t GCHeap::registerClassType(const std::string &name,
                                   const std::vector<std::string> &fields,
                                   uint32_t parentTypeId) {
  uint32_t id = static_cast<uint32_t>(classTypes_.size()) +
                1; // Start at 1 to avoid 0=nullptr issue
  ClassType ct;
  ct.name = name;
  ct.fieldNames = fields;
  ct.parentTypeId = parentTypeId;
  classTypes_.push_back(std::move(ct));
  return id;
}

// Class allocation (reference type) with parent instance
ClassRef GCHeap::allocateClass(uint32_t typeId, size_t fieldCount,
                               uint32_t parentInstanceId) {
  const uint32_t id = next_array_id_++;
  classes_[id] = std::vector<BytecodeValue>(fieldCount, BytecodeValue(nullptr));
  return ClassRef{.id = id, .typeId = typeId, .parentId = parentInstanceId};
}

std::optional<uint32_t> GCHeap::findClassTypeId(const std::string &name) const {
  for (uint32_t i = 0; i < classTypes_.size(); ++i) {
    if (classTypes_[i].name == name) {
      return i + 1; // Return ID with +1 offset
    }
  }
  return std::nullopt;
}

size_t GCHeap::classFieldCount(uint32_t typeId) const {
  if (typeId == 0 || typeId > classTypes_.size()) {
    return 0;
  }
  return classTypes_[typeId - 1].fieldNames.size();
}

std::optional<size_t> GCHeap::classFieldIndex(uint32_t typeId,
                                              const std::string &field) const {
  if (typeId == 0 || typeId > classTypes_.size()) {
    return std::nullopt;
  }
  const auto &fields = classTypes_[typeId - 1].fieldNames;
  for (size_t i = 0; i < fields.size(); ++i) {
    if (fields[i] == field) {
      return i;
    }
  }
  return std::nullopt;
}

uint32_t GCHeap::getClassParentTypeId(uint32_t typeId) const {
  if (typeId == 0 || typeId > classTypes_.size()) {
    return 0;
  }
  return classTypes_[typeId - 1].parentTypeId;
}

void GCHeap::registerClassMethod(uint32_t typeId, const std::string &methodName,
                                 uint32_t functionIndex) {
  if (typeId == 0 || typeId > classTypes_.size()) {
    return;
  }
  classTypes_[typeId - 1].methodIndices[methodName] = functionIndex;
}

std::optional<uint32_t>
GCHeap::findClassMethod(uint32_t typeId, const std::string &methodName) const {
  if (typeId == 0 || typeId > classTypes_.size()) {
    return std::nullopt;
  }
  const auto &ct = classTypes_[typeId - 1];
  auto it = ct.methodIndices.find(methodName);
  if (it != ct.methodIndices.end()) {
    return it->second;
  }
  // Check parent class
  if (ct.parentTypeId > 0) {
    return findClassMethod(ct.parentTypeId, methodName);
  }
  return std::nullopt;
}
uint32_t GCHeap::registerEnumType(const std::string &name,
                                  const std::vector<std::string> &variants) {
  uint32_t id = static_cast<uint32_t>(enumTypes_.size());
  enumTypes_.push_back(EnumType{name, variants});
  return id;
}

// Enum allocation
EnumRef GCHeap::allocateEnum(uint32_t typeId, uint32_t tag,
                             size_t payloadCount) {
  const uint32_t id = next_array_id_++;
  enums_[id] = {
      tag, std::vector<BytecodeValue>(payloadCount, BytecodeValue(nullptr))};
  return EnumRef{.id = id, .tag = tag, .typeId = typeId};
}

BytecodeValue GCHeap::iteratorNext(uint32_t id) {
  auto *iter = iterator(id);
  if (!iter) {
    // Return {value: null, done: true}
    auto resultObj = allocateObject();
    auto *obj = object(resultObj.id);
    (*obj)["value"] = BytecodeValue(nullptr);
    (*obj)["done"] = BytecodeValue(true);
    return BytecodeValue(resultObj);
  }

  bool done = false;
  BytecodeValue value;

  // Dispatch based on iterable type
  if (std::holds_alternative<ArrayRef>(iter->iterable)) {
    auto *arr = array(std::get<ArrayRef>(iter->iterable).id);
    if (!arr || iter->index >= arr->size()) {
      done = true;
      value = nullptr;
    } else {
      value = (*arr)[iter->index++];
    }
  } else if (std::holds_alternative<std::string>(iter->iterable)) {
    const auto &str = std::get<std::string>(iter->iterable);
    if (iter->index >= str.length()) {
      done = true;
      value = std::string("");
    } else {
      value = std::string(1, str[iter->index++]);
    }
  } else if (std::holds_alternative<ObjectRef>(iter->iterable)) {
    if (iter->index >= iter->keys.size()) {
      done = true;
      value = nullptr;
    } else {
      // For objects, return the value (not the key)
      // Use object.keys()/values()/entries() for explicit control
      auto *obj = object(std::get<ObjectRef>(iter->iterable).id);
      if (obj && iter->index < iter->keys.size()) {
        const auto &key = iter->keys[iter->index++];
        auto it = obj->find(key);
        if (it != obj->end()) {
          value = it->second;
        } else {
          value = nullptr;
        }
      } else {
        value = nullptr;
      }
    }
  } else if (std::holds_alternative<SetRef>(iter->iterable)) {
    if (iter->index >= iter->keys.size()) {
      done = true;
      value = nullptr;
    } else {
      value = iter->keys[iter->index++];
    }
  } else if (std::holds_alternative<RangeRef>(iter->iterable)) {
    auto *r = range(std::get<RangeRef>(iter->iterable).id);
    if (!r) {
      done = true;
      value = nullptr;
    } else {
      int64_t current = r->start + (iter->index * r->step);
      if ((r->step > 0 && current >= r->end) ||
          (r->step < 0 && current <= r->end)) {
        done = true;
        value = nullptr;
      } else {
        value = BytecodeValue(current);
        iter->index++;
      }
    }
  } else {
    // Unknown type, just return done
    done = true;
    value = nullptr;
  }

  // Return {value, done}
  auto resultObj = allocateObject();
  auto *obj = object(resultObj.id);
  (*obj)["value"] = value;
  (*obj)["done"] = BytecodeValue(done);
  return BytecodeValue(resultObj);
}

GCHeap::RuntimeClosure *GCHeap::closure(uint32_t id) {
  auto it = closures_.find(id);
  return it == closures_.end() ? nullptr : &it->second;
}

const GCHeap::RuntimeClosure *GCHeap::closure(uint32_t id) const {
  auto it = closures_.find(id);
  return it == closures_.end() ? nullptr : &it->second;
}

std::vector<BytecodeValue> *GCHeap::array(uint32_t id) {
  auto it = arrays_.find(id);
  return it == arrays_.end() ? nullptr : &it->second;
}

std::unordered_map<std::string, BytecodeValue> *GCHeap::object(uint32_t id) {
  auto it = objects_.find(id);
  return it == objects_.end() ? nullptr : &it->second;
}

std::unordered_map<std::string, BytecodeValue> *GCHeap::set(uint32_t id) {
  auto it = sets_.find(id);
  return it == sets_.end() ? nullptr : &it->second;
}

uint64_t GCHeap::pinExternalRoot(const BytecodeValue &value) {
  const uint64_t id = next_external_root_id_++;
  external_roots_[id] = value;
  return id;
}

bool GCHeap::unpinExternalRoot(uint64_t root_id) {
  return external_roots_.erase(root_id) > 0;
}

std::optional<BytecodeValue> GCHeap::externalRoot(uint64_t root_id) const {
  auto it = external_roots_.find(root_id);
  if (it == external_roots_.end()) {
    return std::nullopt;
  }
  return it->second;
}

GCHeap::Stats GCHeap::stats() const {
  uint64_t heap_size = 0;
  for (const auto &[_, array] : arrays_) {
    heap_size += sizeof(array) +
                 static_cast<uint64_t>(array.size()) * sizeof(BytecodeValue);
  }
  for (const auto &[_, object] : objects_) {
    heap_size += sizeof(object);
    for (const auto &[key, _] : object) {
      heap_size += static_cast<uint64_t>(key.size()) + sizeof(BytecodeValue);
    }
  }
  for (const auto &[_, set] : sets_) {
    heap_size += sizeof(set);
    for (const auto &[key, _] : set) {
      heap_size += static_cast<uint64_t>(key.size()) + sizeof(BytecodeValue);
    }
  }
  for (const auto &[_, closure] : closures_) {
    heap_size += sizeof(closure);
    heap_size += static_cast<uint64_t>(closure.upvalues.size()) *
                 sizeof(std::shared_ptr<UpvalueCell>);
  }

  return Stats{
      .heap_size = heap_size,
      .object_count = static_cast<uint64_t>(arrays_.size() + objects_.size() +
                                            sets_.size() + closures_.size()),
      .collections = collections_,
      .last_pause_ns = last_pause_ns_,
  };
}

void GCHeap::maybeCollectGarbage(
    const std::vector<BytecodeValue> &stack_values,
    const std::vector<BytecodeValue> &locals,
    const std::unordered_map<std::string, BytecodeValue> &globals,
    const std::vector<uint32_t> &active_closure_ids,
    const std::function<std::optional<BytecodeValue>(uint32_t)>
        &open_local_reader) {
  allocations_since_last_++;
  if (allocations_since_last_ < allocation_budget_) {
    return;
  }
  collectGarbage(stack_values, locals, globals, active_closure_ids,
                 open_local_reader);
}

void GCHeap::markValue(
    const BytecodeValue &value, std::unordered_set<uint32_t> &marked_arrays,
    std::unordered_set<uint32_t> &marked_objects,
    std::unordered_set<uint32_t> &marked_sets,
    std::unordered_set<uint32_t> &marked_closures,
    const std::function<std::optional<BytecodeValue>(uint32_t)>
        &open_local_reader) const {
  if (std::holds_alternative<ArrayRef>(value)) {
    uint32_t id = std::get<ArrayRef>(value).id;
    if (!marked_arrays.insert(id).second) {
      return;
    }
    auto it = arrays_.find(id);
    if (it == arrays_.end()) {
      return;
    }
    for (const auto &entry : it->second) {
      markValue(entry, marked_arrays, marked_objects, marked_sets,
                marked_closures, open_local_reader);
    }
    return;
  }

  if (std::holds_alternative<ObjectRef>(value)) {
    uint32_t id = std::get<ObjectRef>(value).id;
    if (!marked_objects.insert(id).second) {
      return;
    }
    auto it = objects_.find(id);
    if (it == objects_.end()) {
      return;
    }
    for (const auto &[_, entry] : it->second) {
      markValue(entry, marked_arrays, marked_objects, marked_sets,
                marked_closures, open_local_reader);
    }
    return;
  }

  if (std::holds_alternative<SetRef>(value)) {
    uint32_t id = std::get<SetRef>(value).id;
    if (!marked_sets.insert(id).second) {
      return;
    }
    auto it = sets_.find(id);
    if (it == sets_.end()) {
      return;
    }
    for (const auto &[_, entry] : it->second) {
      markValue(entry, marked_arrays, marked_objects, marked_sets,
                marked_closures, open_local_reader);
    }
    return;
  }

  if (std::holds_alternative<StructRef>(value)) {
    // Structs are value types, but we still need to mark their fields
    uint32_t id = std::get<StructRef>(value).id;
    auto it = structs_.find(id);
    if (it == structs_.end()) {
      return;
    }
    for (const auto &entry : it->second) {
      markValue(entry, marked_arrays, marked_objects, marked_sets,
                marked_closures, open_local_reader);
    }
    return;
  }

  if (std::holds_alternative<ClassRef>(value)) {
    // Classes are reference types - need to mark as visited
    uint32_t id = std::get<ClassRef>(value).id;
    if (!marked_arrays.insert(id)
             .second) { // Reuse marked_arrays for class tracking
      return;
    }
    auto it = classes_.find(id);
    if (it == classes_.end()) {
      return;
    }
    for (const auto &entry : it->second) {
      markValue(entry, marked_arrays, marked_objects, marked_sets,
                marked_closures, open_local_reader);
    }
    return;
  }

  if (std::holds_alternative<EnumRef>(value)) {
    // Enums have a payload array that needs marking
    uint32_t id = std::get<EnumRef>(value).id;
    auto it = enums_.find(id);
    if (it == enums_.end()) {
      return;
    }
    for (const auto &entry : it->second.second) {
      markValue(entry, marked_arrays, marked_objects, marked_sets,
                marked_closures, open_local_reader);
    }
    return;
  }

  if (std::holds_alternative<ClosureRef>(value)) {
    uint32_t id = std::get<ClosureRef>(value).id;
    if (!marked_closures.insert(id).second) {
      return;
    }
    auto it = closures_.find(id);
    if (it == closures_.end()) {
      return;
    }
    for (const auto &cell : it->second.upvalues) {
      if (!cell) {
        continue;
      }
      if (cell->is_open) {
        auto local_value = open_local_reader(cell->open_index);
        if (local_value.has_value()) {
          markValue(*local_value, marked_arrays, marked_objects, marked_sets,
                    marked_closures, open_local_reader);
        }
      } else {
        markValue(cell->closed_value, marked_arrays, marked_objects,
                  marked_sets, marked_closures, open_local_reader);
      }
    }
    return;
  }
}

void GCHeap::collectGarbage(
    const std::vector<BytecodeValue> &stack_values,
    const std::vector<BytecodeValue> &locals,
    const std::unordered_map<std::string, BytecodeValue> &globals,
    const std::vector<uint32_t> &active_closure_ids,
    const std::function<std::optional<BytecodeValue>(uint32_t)>
        &open_local_reader) {
  const auto pause_start = std::chrono::steady_clock::now();

  std::unordered_set<uint32_t> marked_arrays;
  std::unordered_set<uint32_t> marked_objects;
  std::unordered_set<uint32_t> marked_sets;
  std::unordered_set<uint32_t> marked_closures;

  for (const auto &value : stack_values) {
    markValue(value, marked_arrays, marked_objects, marked_sets,
              marked_closures, open_local_reader);
  }
  for (const auto &value : locals) {
    markValue(value, marked_arrays, marked_objects, marked_sets,
              marked_closures, open_local_reader);
  }
  for (const auto &[_, value] : globals) {
    markValue(value, marked_arrays, marked_objects, marked_sets,
              marked_closures, open_local_reader);
  }
  for (const auto &[_, value] : external_roots_) {
    markValue(value, marked_arrays, marked_objects, marked_sets,
              marked_closures, open_local_reader);
  }
  for (uint32_t closure_id : active_closure_ids) {
    if (closure_id == 0) {
      continue;
    }
    markValue(ClosureRef{.id = closure_id}, marked_arrays, marked_objects,
              marked_sets, marked_closures, open_local_reader);
  }

  for (auto it = arrays_.begin(); it != arrays_.end();) {
    if (marked_arrays.find(it->first) == marked_arrays.end()) {
      it = arrays_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = objects_.begin(); it != objects_.end();) {
    if (marked_objects.find(it->first) == marked_objects.end()) {
      it = objects_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = sets_.begin(); it != sets_.end();) {
    if (marked_sets.find(it->first) == marked_sets.end()) {
      it = sets_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = closures_.begin(); it != closures_.end();) {
    if (marked_closures.find(it->first) == marked_closures.end()) {
      it = closures_.erase(it);
    } else {
      ++it;
    }
  }

  allocations_since_last_ = 0;
  collections_++;
  last_pause_ns_ = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - pause_start)
          .count());
}

} // namespace havel::compiler
