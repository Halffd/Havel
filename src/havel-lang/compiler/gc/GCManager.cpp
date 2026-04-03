#include "GCManager.hpp"
#include <chrono>
#include <stack>

namespace havel::compiler {

// ============================================================================
// GCArray Implementation
// ============================================================================

void GCArray::markChildren(std::function<void(uint32_t)> marker) {
  for (const auto& elem : elements) {
    if (elem.isObjectId()) {
      marker(elem.asObjectId());
    } else if (elem.isArrayId()) {
      marker(elem.asArrayId());
    } else if (elem.isClosureId()) {
      marker(elem.asClosureId());
    }
    // Other types don't contain GC references
  }
}

size_t GCArray::size() const {
  size_t total = sizeof(*this);
  total += elements.capacity() * sizeof(Value);
  return total;
}

// ============================================================================
// GCObjectMap Implementation
// ============================================================================

void GCObjectMap::markChildren(std::function<void(uint32_t)> marker) {
  for (const auto& [key, value] : properties) {
    (void)key;
    if (value.isObjectId()) {
      marker(value.asObjectId());
    } else if (value.isArrayId()) {
      marker(value.asArrayId());
    } else if (value.isClosureId()) {
      marker(value.asClosureId());
    }
  }
}

size_t GCObjectMap::size() const {
  size_t total = sizeof(*this);
  for (const auto& [key, value] : properties) {
    total += key.capacity() + sizeof(value);
  }
  return total;
}

// ============================================================================
// GCRootSet Implementation
// ============================================================================

void GCRootSet::addRoot(uint32_t objectId) {
  roots_.insert(objectId);
}

void GCRootSet::removeRoot(uint32_t objectId) {
  roots_.erase(objectId);
}

bool GCRootSet::isRoot(uint32_t objectId) const {
  return roots_.count(objectId) > 0;
}

void GCRootSet::clear() {
  roots_.clear();
  externalRoots_.clear();
}

uint64_t GCRootSet::addExternalRoot(uint32_t objectId) {
  uint64_t id = nextExternalId_++;
  externalRoots_[id] = objectId;
  roots_.insert(objectId);
  return id;
}

bool GCRootSet::removeExternalRoot(uint64_t handle) {
  auto it = externalRoots_.find(handle);
  if (it == externalRoots_.end()) {
    return false;
  }
  roots_.erase(it->second);
  externalRoots_.erase(it);
  return true;
}

std::optional<uint32_t> GCRootSet::getExternalRoot(uint64_t handle) const {
  auto it = externalRoots_.find(handle);
  if (it != externalRoots_.end()) {
    return it->second;
  }
  return std::nullopt;
}

// ============================================================================
// GCManager Implementation
// ============================================================================

GCManager::GCManager(GCHeap& heap) : heap_(heap) {
  nextCollectionThreshold_ = config_.allocationBudget;
}

GCManager::~GCManager() = default;

void GCManager::maybeCollect(const std::vector<Value>& roots) {
  if (paused_) return;
  if (currentAllocation_ < nextCollectionThreshold_) return;

  collect(roots);
}

void GCManager::collect(const std::vector<Value>& roots) {
  if (paused_) return;

  auto startTime = std::chrono::steady_clock::now();

  mark(roots);
  sweep();

  auto endTime = std::chrono::steady_clock::now();
  double pauseMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

  stats_.totalPauseTimeMs += pauseMs;
  stats_.collectionCount++;

  // Adjust threshold
  nextCollectionThreshold_ = static_cast<size_t>(stats_.heapSize * config_.growthFactor);
  if (nextCollectionThreshold_ > config_.maxHeapSize) {
    nextCollectionThreshold_ = config_.maxHeapSize;
  }
  currentAllocation_ = 0;
}

void GCManager::forceFullCollect(const std::vector<Value>& roots) {
  if (paused_) return;

  // Reset allocation counter to force full collection
  currentAllocation_ = config_.maxHeapSize;
  collect(roots);
}

uint32_t GCManager::registerObject(std::unique_ptr<GCObject> obj) {
  if (!obj) return 0;

  uint32_t id = nextObjectId_++;
  obj->id = id;
  obj->marked = false;

  stats_.heapSize += obj->size();
  stats_.objectCount++;
  stats_.allocated++;

  objects_[id] = std::move(obj);
  return id;
}

void GCManager::unregisterObject(uint32_t id) {
  auto it = objects_.find(id);
  if (it == objects_.end()) return;

  stats_.heapSize -= it->second->size();
  stats_.objectCount--;
  stats_.collected++;

  objects_.erase(it);
}

GCObject* GCManager::getObject(uint32_t id) {
  auto it = objects_.find(id);
  if (it != objects_.end()) {
    return it->second.get();
  }
  return nullptr;
}

void GCManager::addRoot(uint32_t objectId) {
  rootSet_.addRoot(objectId);
}

void GCManager::removeRoot(uint32_t objectId) {
  rootSet_.removeRoot(objectId);
}

bool GCManager::isMemoryPressure() const {
  return stats_.heapSize > config_.maxHeapSize * 0.9;
}

void GCManager::pause() {
  paused_ = true;
}

void GCManager::resume() {
  paused_ = false;
}

void GCManager::mark(const std::vector<Value>& roots) {
  // Clear all marks
  for (auto& [id, obj] : objects_) {
    (void)id;
    obj->marked = false;
  }

  // Mark from roots
  markRoots(roots);

  // Mark from registered roots
  for (uint32_t rootId : rootSet_.getRoots()) {
    markObject(rootId);
  }
}

void GCManager::markObject(uint32_t id) {
  auto it = objects_.find(id);
  if (it == objects_.end()) return;
  if (it->second->marked) return;

  it->second->marked = true;

  // Mark children
  it->second->markChildren([this](uint32_t childId) {
    markObject(childId);
  });
}

void GCManager::sweep() {
  std::vector<uint32_t> toRemove;

  for (auto& [id, obj] : objects_) {
    if (!obj->marked) {
      toRemove.push_back(id);
    }
  }

  for (uint32_t id : toRemove) {
    unregisterObject(id);
  }
}

void GCManager::markRoots(const std::vector<Value>& roots) {
  for (const auto& root : roots) {
    markValue(root);
  }
}

void GCManager::markValue(const Value& value) {
  if (value.isObjectId()) {
    markObject(value.asObjectId());
  } else if (value.isArrayId()) {
    markObject(value.asArrayId());
  } else if (value.isClosureId()) {
    markObject(value.asClosureId());
  }
  // Other types don't contain GC references
}

} // namespace havel::compiler
