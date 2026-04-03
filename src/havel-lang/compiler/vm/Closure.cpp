#include "Closure.hpp"
#include "havel-lang/compiler/gc/GC.hpp"

namespace havel::compiler {

// ============================================================================
// Upvalue Implementation
// ============================================================================

Upvalue::Upvalue(uint32_t sourceIndex, Type type, bool isConst)
    : sourceIndex_(sourceIndex), type_(type), isConst_(isConst) {}

Value Upvalue::getValue() const {
  if (gcCell_) {
    return gcCell_->get();
  }
  return nullptr;
}

void Upvalue::setValue(const Value& value) {
  if (gcCell_) {
    gcCell_->set(value);
  }
}

void Upvalue::close(const Value& value) {
  if (gcCell_) {
    gcCell_->close(value);
  }
}

bool Upvalue::isClosed() const {
  if (gcCell_) {
    return gcCell_->isClosed();
  }
  return false;
}

// ============================================================================
// Closure Implementation
// ============================================================================

Closure::Closure(uint32_t functionIndex, std::vector<Upvalue> upvalues)
    : functionIndex_(functionIndex), upvalues_(std::move(upvalues)) {}

Closure Closure::create(
    uint32_t functionIndex,
    const std::vector<UpvalueDescriptor>& descriptors,
    const std::vector<std::shared_ptr<GCHeap::UpvalueCell>>& parentUpvalues,
    const std::vector<Value>& locals) {

  std::vector<Upvalue> upvalues;
  upvalues.reserve(descriptors.size());

  for (const auto& desc : descriptors) {
    Upvalue::Type type = desc.captures_local ? Upvalue::Type::Local
                                             : Upvalue::Type::Upvalue;
    Upvalue upvalue(desc.index, type);

    if (desc.captures_local) {
      // Capture local from parent frame
      if (desc.index < locals.size()) {
        // This would need the GC cell from the open upvalues
        // For now, just store the descriptor
      }
    } else {
      // Capture upvalue from parent
      if (desc.index < parentUpvalues.size()) {
        upvalue.setGCTarget(parentUpvalues[desc.index]);
      }
    }

    upvalues.push_back(std::move(upvalue));
  }

  return Closure(functionIndex, std::move(upvalues));
}

Upvalue& Closure::getUpvalue(size_t index) {
  if (index >= upvalues_.size()) {
    throw std::out_of_range("Upvalue index out of range");
  }
  return upvalues_[index];
}

const Upvalue& Closure::getUpvalue(size_t index) const {
  if (index >= upvalues_.size()) {
    throw std::out_of_range("Upvalue index out of range");
  }
  return upvalues_[index];
}

Value Closure::getUpvalueValue(size_t index) const {
  return getUpvalue(index).getValue();
}

void Closure::setUpvalueValue(size_t index, const Value& value) {
  getUpvalue(index).setValue(value);
}

bool Closure::hasUpvalue(uint32_t sourceIndex) const {
  return findUpvalueIndex(sourceIndex).has_value();
}

std::optional<size_t> Closure::findUpvalueIndex(uint32_t sourceIndex) const {
  for (size_t i = 0; i < upvalues_.size(); ++i) {
    if (upvalues_[i].getSourceIndex() == sourceIndex) {
      return i;
    }
  }
  return std::nullopt;
}

Value Closure::toValue(uint32_t closureId) const {
  return Value::makeClosureId(closureId);
}

// ============================================================================
// ClosureFactory Implementation
// ============================================================================

Closure ClosureFactory::createFromFunction(uint32_t functionIndex) {
  return Closure(functionIndex, {});
}

Closure ClosureFactory::createWithUpvalues(
    uint32_t functionIndex,
    const std::vector<UpvalueDescriptor>& descriptors) {
  std::vector<Upvalue> upvalues;
  upvalues.reserve(descriptors.size());

  for (const auto& desc : descriptors) {
    Upvalue::Type type = desc.captures_local ? Upvalue::Type::Local
                                             : Upvalue::Type::Upvalue;
    upvalues.emplace_back(desc.index, type);
  }

  return Closure(functionIndex, std::move(upvalues));
}

// ============================================================================
// UpvalueManager Implementation
// ============================================================================

UpvalueManager::UpvalueManager(GCHeap& heap) : heap_(heap) {}

std::shared_ptr<GCHeap::UpvalueCell> UpvalueManager::openUpvalue(
    uint32_t localIndex, const Value& value) {
  auto it = openUpvalues_.find(localIndex);
  if (it != openUpvalues_.end()) {
    return it->second;
  }

  auto cell = heap_.createUpvalue(localIndex);
  openUpvalues_[localIndex] = cell;
  return cell;
}

void UpvalueManager::closeUpvalues(uint32_t localsBase, uint32_t localsEnd,
                                     const std::vector<Value>& locals) {
  std::vector<uint32_t> toClose;

  for (const auto& [index, cell] : openUpvalues_) {
    if (index >= localsBase && index < localsEnd) {
      toClose.push_back(index);
    }
  }

  for (uint32_t index : toClose) {
    auto it = openUpvalues_.find(index);
    if (it != openUpvalues_.end()) {
      Value value = (index < locals.size()) ? locals[index] : nullptr;
      it->second->close(value);
      openUpvalues_.erase(it);
    }
  }
}

void UpvalueManager::closeAllUpvalues(const std::vector<Value>& locals) {
  for (const auto& [index, cell] : openUpvalues_) {
    Value value = (index < locals.size()) ? locals[index] : nullptr;
    cell->close(value);
  }
  openUpvalues_.clear();
}

std::shared_ptr<GCHeap::UpvalueCell> UpvalueManager::getOpenUpvalue(
    uint32_t localIndex) const {
  auto it = openUpvalues_.find(localIndex);
  if (it != openUpvalues_.end()) {
    return it->second;
  }
  return nullptr;
}

bool UpvalueManager::isUpvalueOpen(uint32_t localIndex) const {
  return openUpvalues_.count(localIndex) > 0;
}

void UpvalueManager::clear() {
  openUpvalues_.clear();
}

} // namespace havel::compiler
