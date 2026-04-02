#pragma once

#include "../core/BytecodeIR.hpp"
#include <vector>
#include <unordered_set>
#include <memory>
#include <functional>

namespace havel::compiler {

// Forward declarations
class GCHeap;

// ============================================================================
// GCObject - Base class for GC-managed objects
// ============================================================================
struct GCObject {
  enum class Type : uint8_t {
    String,
    Array,
    Object,
    Closure,
    Upvalue,
    Struct,
    Class,
    Enum,
    Iterator,
    Image,
    NativeData
  };

  Type type;
  bool marked = false;
  uint32_t id = 0;

  explicit GCObject(Type t) : type(t) {}
  virtual ~GCObject() = default;

  virtual void markChildren(std::function<void(uint32_t)> marker) {}
  virtual size_t size() const { return sizeof(*this); }
};

// ============================================================================
// GCRootSet - Manages root references for GC
// ============================================================================
class GCRootSet {
public:
  void addRoot(uint32_t objectId);
  void removeRoot(uint32_t objectId);
  bool isRoot(uint32_t objectId) const;
  const std::unordered_set<uint32_t>& getRoots() const { return roots_; }
  void clear();

  // External root management (for values pinned from outside VM)
  uint64_t addExternalRoot(uint32_t objectId);
  bool removeExternalRoot(uint64_t handle);
  std::optional<uint32_t> getExternalRoot(uint64_t handle) const;

private:
  std::unordered_set<uint32_t> roots_;
  std::unordered_map<uint64_t, uint32_t> externalRoots_;
  uint64_t nextExternalId_ = 1;
};

// ============================================================================
// GCManager - High-level garbage collection management
// ============================================================================
class GCManager {
public:
  struct Stats {
    size_t allocated = 0;
    size_t collected = 0;
    size_t heapSize = 0;
    size_t objectCount = 0;
    size_t collectionCount = 0;
    double totalPauseTimeMs = 0.0;
  };

  struct Config {
    size_t initialHeapSize = 64 * 1024;      // 64KB
    size_t maxHeapSize = 64 * 1024 * 1024;   // 64MB
    size_t allocationBudget = 1024 * 1024;   // 1MB before collection
    double growthFactor = 2.0;
    bool incremental = false;
  };

  explicit GCManager(GCHeap& heap);
  ~GCManager();

  // Configuration
  void setConfig(const Config& config) { config_ = config; }
  const Config& getConfig() const { return config_; }

  // Collection control
  void maybeCollect(const std::vector<BytecodeValue>& roots);
  void collect(const std::vector<BytecodeValue>& roots);
  void forceFullCollect(const std::vector<BytecodeValue>& roots);

  // Object registration
  uint32_t registerObject(std::unique_ptr<GCObject> obj);
  void unregisterObject(uint32_t id);
  GCObject* getObject(uint32_t id);

  // Root management
  void addRoot(uint32_t objectId);
  void removeRoot(uint32_t objectId);

  // Stats
  Stats getStats() const { return stats_; }
  void resetStats() { stats_ = Stats{}; }

  // Memory pressure
  bool isMemoryPressure() const;
  size_t getHeapSize() const { return stats_.heapSize; }
  size_t getObjectCount() const { return stats_.objectCount; }

  // Pause/resume for critical sections
  void pause();
  void resume();
  bool isPaused() const { return paused_; }

private:
  GCHeap& heap_;
  Config config_;
  Stats stats_;
  GCRootSet rootSet_;

  bool paused_ = false;
  size_t currentAllocation_ = 0;
  size_t nextCollectionThreshold_;

  std::unordered_map<uint32_t, std::unique_ptr<GCObject>> objects_;
  uint32_t nextObjectId_ = 1;

  // Mark-sweep implementation
  void mark(const std::vector<BytecodeValue>& roots);
  void markObject(uint32_t id);
  void sweep();
  void markRoots(const std::vector<BytecodeValue>& roots);
  void markValue(const BytecodeValue& value);
};

// ============================================================================
// GCPtr - Smart pointer for GC-managed objects (for C++ usage)
// ============================================================================
template<typename T>
class GCPtr {
public:
  GCPtr() = default;
  explicit GCPtr(uint32_t id, GCManager* gc) : id_(id), gc_(gc) {}

  T* get() const {
    if (!gc_ || id_ == 0) return nullptr;
    return static_cast<T*>(gc_->getObject(id_));
  }

  T* operator->() const { return get(); }
  T& operator*() const { return *get(); }

  explicit operator bool() const { return get() != nullptr; }
  uint32_t id() const { return id_; }

  void reset() {
    id_ = 0;
    gc_ = nullptr;
  }

private:
  uint32_t id_ = 0;
  GCManager* gc_ = nullptr;
};

// ============================================================================
// GCString - String object for GC heap
// ============================================================================
struct GCString : GCObject {
  std::string value;

  explicit GCString(std::string s)
    : GCObject(GCObject::Type::String), value(std::move(s)) {}

  size_t size() const override { return sizeof(*this) + value.capacity(); }
};

// ============================================================================
// GCArray - Array object for GC heap
// ============================================================================
struct GCArray : GCObject {
  std::vector<BytecodeValue> elements;

  GCArray() : GCObject(GCObject::Type::Array) {}

  void markChildren(std::function<void(uint32_t)> marker) override;
  size_t size() const override;
};

// ============================================================================
// GCObjectMap - Object/map for GC heap
// ============================================================================
struct GCObjectMap : GCObject {
  std::unordered_map<std::string, BytecodeValue> properties;

  GCObjectMap() : GCObject(GCObject::Type::Object) {}

  void markChildren(std::function<void(uint32_t)> marker) override;
  size_t size() const override;
};

} // namespace havel::compiler
