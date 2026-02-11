#pragma once

#include <variant>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>

namespace havel::runtime {

// Forward declarations
class String;
class Array;
class Object;
class Function;

// Garbage-collected heap object base class
class GCObject {
public:
    virtual ~GCObject() = default;
    
    // Reference counting for simple GC
    mutable std::atomic<uint32_t> ref_count{1};
    
    void retain() const {
        ref_count.fetch_add(1, std::memory_order_relaxed);
    }
    
    void release() const {
        if (ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }
    
    uint32_t getRefCount() const {
        return ref_count.load(std::memory_order_acquire);
    }
    
    // Type information for GC
    enum class Type {
        STRING,
        ARRAY,
        OBJECT,
        FUNCTION,
        CLOSURE
    };
    
    virtual Type getType() const = 0;
    virtual size_t getSize() const = 0;
    virtual void markChildren() = 0;
};

// Garbage collector with reference counting
class GarbageCollector {
private:
    std::vector<GCObject*> objects;
    std::atomic<uint32_t> total_objects{0};
    std::atomic<uint32_t> total_memory{0};
    std::atomic<bool> collection_in_progress{false};
    
    // GC thresholds
    static constexpr uint32_t GC_THRESHOLD = 10000;  // Trigger GC after 10k objects
    static constexpr size_t MEMORY_THRESHOLD = 100 * 1024 * 1024;  // 100MB
    
public:
    static GarbageCollector& getInstance() {
        static GarbageCollector instance;
        return instance;
    }
    
    // Register object with GC
    void registerObject(GCObject* obj) {
        objects.push_back(obj);
        total_objects.fetch_add(1, std::memory_order_relaxed);
        
        // Trigger GC if thresholds exceeded
        if (shouldTriggerGC()) {
            collect();
        }
    }
    
    // Unregister object (when destroyed)
    void unregisterObject(GCObject* obj) {
        auto it = std::find(objects.begin(), objects.end(), obj);
        if (it != objects.end()) {
            objects.erase(it);
            total_objects.fetch_sub(1, std::memory_order_relaxed);
        }
    }
    
    // Manual garbage collection
    void collect() {
        if (collection_in_progress.exchange(true)) {
            return; // Collection already in progress
        }
        
        std::cout << "GC: Starting collection (objects: " << objects.size() << ")" << std::endl;
        
        size_t collected = 0;
        size_t memory_freed = 0;
        
        auto it = objects.begin();
        while (it != objects.end()) {
            GCObject* obj = *it;
            
            if (obj->getRefCount() == 0) {
                memory_freed += obj->getSize();
                delete obj;
                it = objects.erase(it);
                collected++;
            } else {
                ++it;
            }
        }
        
        total_memory.fetch_sub(memory_freed, std::memory_order_relaxed);
        
        std::cout << "GC: Collected " << collected << " objects, freed " 
                 << memory_freed << " bytes" << std::endl;
        
        collection_in_progress.store(false, std::memory_order_release);
    }
    
    // Get GC statistics
    struct GCStats {
        uint32_t total_objects;
        uint32_t total_memory_kb;
        bool collection_in_progress;
    };
    
    GCStats getStats() const {
        return {
            total_objects.load(std::memory_order_acquire),
            static_cast<uint32_t>(total_memory.load(std::memory_order_acquire) / 1024),
            collection_in_progress.load(std::memory_order_acquire)
        };
    }
    
    // Force collection
    void forceCollect() {
        collect();
    }
    
private:
    bool shouldTriggerGC() const {
        return objects.size() >= GC_THRESHOLD || 
               total_memory.load(std::memory_order_acquire) >= MEMORY_THRESHOLD;
    }
};

// GC-managed string
class String : public GCObject {
private:
    std::string data;
    
public:
    String(const std::string& str) : data(str) {
        GarbageCollector::getInstance().registerObject(this);
    }
    
    ~String() {
        GarbageCollector::getInstance().unregisterObject(this);
    }
    
    Type getType() const override { return Type::STRING; }
    size_t getSize() const override { return sizeof(String) + data.size(); }
    void markChildren() const override {} // Strings have no children
    
    const std::string& getData() const { return data; }
    
    // Convenience operators
    operator const std::string&() const { return data; }
    bool operator==(const String& other) const { return data == other.data; }
    bool operator!=(const String& other) const { return data != other.data; }
    
    // Smart pointer type
    using Ptr = std::shared_ptr<String>;
    
    static Ptr create(const std::string& str) {
        return Ptr(new String(str));
    }
};

// GC-managed array
class Array : public GCObject {
private:
    std::vector<BytecodeValue> elements;
    
public:
    Array(size_t capacity = 0) : elements(capacity) {
        GarbageCollector::getInstance().registerObject(this);
    }
    
    ~Array() {
        GarbageCollector::getInstance().unregisterObject(this);
    }
    
    Type getType() const override { return Type::ARRAY; }
    size_t getSize() const override { 
        return sizeof(Array) + elements.size() * sizeof(BytecodeValue); 
    }
    
    void markChildren() const override {
        for (const auto& element : elements) {
            if (auto* obj = std::get_if<GCObject*>(&element)) {
                obj->markChildren();
            }
        }
    }
    
    // Array operations
    void push(const BytecodeValue& value) { elements.push_back(value); }
    BytecodeValue get(size_t index) const { return elements[index]; }
    void set(size_t index, const BytecodeValue& value) { elements[index] = value; }
    size_t size() const { return elements.size(); }
    bool empty() const { return elements.empty(); }
    
    // Smart pointer type
    using Ptr = std::shared_ptr<Array>;
    
    static Ptr create(size_t capacity = 0) {
        return Ptr(new Array(capacity));
    }
};

// GC-managed object
class Object : public GCObject {
private:
    std::unordered_map<std::string, BytecodeValue> properties;
    
public:
    Object() {
        GarbageCollector::getInstance().registerObject(this);
    }
    
    ~Object() {
        GarbageCollector::getInstance().unregisterObject(this);
    }
    
    Type getType() const override { return Type::OBJECT; }
    size_t getSize() const override { 
        return sizeof(Object) + properties.size() * (sizeof(std::string) + sizeof(BytecodeValue)); 
    }
    
    void markChildren() const override {
        for (const auto& [key, value] : properties) {
            if (auto* obj = std::get_if<GCObject*>(&value)) {
                obj->markChildren();
            }
        }
    }
    
    // Object operations
    void set(const std::string& key, const BytecodeValue& value) { properties[key] = value; }
    BytecodeValue get(const std::string& key) const { 
        auto it = properties.find(key);
        return it != properties.end() ? it->second : BytecodeValue(nullptr); 
    }
    bool has(const std::string& key) const { return properties.find(key) != properties.end(); }
    size_t size() const { return properties.size(); }
    
    // Smart pointer type
    using Ptr = std::shared_ptr<Object>;
    
    static Ptr create() {
        return Ptr(new Object());
    }
};

// GC-managed function
class Function : public GCObject {
private:
    std::string name;
    std::vector<std::string> parameters;
    std::vector<BytecodeValue> bytecode;
    
public:
    Function(const std::string& func_name, 
            const std::vector<std::string>& params,
            const std::vector<BytecodeValue>& code)
        : name(func_name), parameters(params), bytecode(code) {
        GarbageCollector::getInstance().registerObject(this);
    }
    
    ~Function() {
        GarbageCollector::getInstance().unregisterObject(this);
    }
    
    Type getType() const override { return Type::FUNCTION; }
    size_t getSize() const override { 
        return sizeof(Function) + 
               name.size() + 
               parameters.size() * sizeof(std::string) +
               bytecode.size() * sizeof(BytecodeValue); 
    }
    
    void markChildren() const override {
        for (const auto& instruction : bytecode) {
            if (auto* obj = std::get_if<GCObject*>(&instruction)) {
                obj->markChildren();
            }
        }
    }
    
    // Function operations
    const std::string& getName() const { return name; }
    const std::vector<std::string>& getParameters() const { return parameters; }
    const std::vector<BytecodeValue>& getBytecode() const { return bytecode; }
    
    // Smart pointer type
    using Ptr = std::shared_ptr<Function>;
    
    static Ptr create(const std::string& name,
                   const std::vector<std::string>& params,
                   const std::vector<BytecodeValue>& code) {
        return Ptr(new Function(name, params, code));
    }
};

// Enhanced BytecodeValue with GC support
using BytecodeValue = std::variant<
    std::nullptr_t,
    bool,
    int64_t,
    double,
    String::Ptr,      // GC-managed string
    Array::Ptr,       // GC-managed array
    Object::Ptr,      // GC-managed object
    Function::Ptr      // GC-managed function
>;

// GC-aware value operations
namespace GC {
    // Create GC-managed values
    inline BytecodeValue createString(const std::string& str) {
        return String::create(str);
    }
    
    inline BytecodeValue createArray(size_t capacity = 0) {
        return Array::create(capacity);
    }
    
    inline BytecodeValue createObject() {
        return Object::create();
    }
    
    inline BytecodeValue createFunction(const std::string& name,
                                   const std::vector<std::string>& params,
                                   const std::vector<BytecodeValue>& code) {
        return Function::create(name, params, code);
    }
    
    // Check if value is GC-managed
    inline bool isGCManaged(const BytecodeValue& value) {
        return std::holds_alternative<String::Ptr>(value) ||
               std::holds_alternative<Array::Ptr>(value) ||
               std::holds_alternative<Object::Ptr>(value) ||
               std::holds_alternative<Function::Ptr>(value);
    }
    
    // Get GC object from value
    inline GCObject* getGCObject(const BytecodeValue& value) {
        if (auto* str = std::get_if<String::Ptr>(&value)) {
            return str->get();
        }
        if (auto* arr = std::get_if<Array::Ptr>(&value)) {
            return arr->get();
        }
        if (auto* obj = std::get_if<Object::Ptr>(&value)) {
            return obj->get();
        }
        if (auto* func = std::get_if<Function::Ptr>(&value)) {
            return func->get();
        }
        return nullptr;
    }
    
    // Force garbage collection
    inline void collect() {
        GarbageCollector::getInstance().collect();
    }
    
    // Get GC statistics
    inline GarbageCollector::GCStats getStats() {
        return GarbageCollector::getInstance().getStats();
    }
}

} // namespace havel::runtime
