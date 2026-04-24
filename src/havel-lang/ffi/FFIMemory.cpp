#include "FFIMemory.hpp"
#include "FFITypes.hpp"
#include "../core/Value.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>

#ifdef HAVE_LIBFFI

namespace havel::ffi {

std::unordered_map<void*, Allocation> FFIMemory::allocations_;
std::mutex FFIMemory::alloc_mutex_;
size_t FFIMemory::total_allocated_ = 0;
size_t FFIMemory::total_used_ = 0;

void* FFIMemory::alloc(std::shared_ptr<FFIType> type) {
    if (!type) return nullptr;
    size_t size = FFITypeRegistry::size_of(type);
    return alloc_bytes(size);
}

void* FFIMemory::alloc_bytes(size_t size) {
    if (size == 0) return nullptr;
    
    void* ptr = std::malloc(size);
    if (!ptr) return nullptr;
    
    std::memset(ptr, 0, size);
    
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    Allocation& a = allocations_[ptr];
    a.ptr = ptr;
    a.size = size;
    a.is_managed = false;
    total_allocated_ += size;
    total_used_ += size;
    
    return ptr;
}

void* FFIMemory::realloc(void* ptr, size_t new_size) {
    if (!ptr) return alloc_bytes(new_size);
    if (new_size == 0) { free(ptr); return nullptr; }
    
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    auto it = allocations_.find(ptr);
    if (it == allocations_.end()) {
        return std::realloc(ptr, new_size);
    }
    
    void* new_ptr = std::realloc(ptr, new_size);
    if (new_ptr && new_ptr != ptr) {
        total_used_ -= it->second.size;
        it->second.size = new_size;
        total_used_ += new_size;
    }
    return new_ptr;
}

void FFIMemory::free(void* ptr) {
    if (!ptr) return;
    
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    auto it = allocations_.find(ptr);
    if (it != allocations_.end()) {
        if (it->second.finalizer) {
            it->second.finalizer(ptr);
        }
        total_used_ -= it->second.size;
        allocations_.erase(it);
    }
    std::free(ptr);
}

void* FFIMemory::cast(void* ptr, std::shared_ptr<FFIType> new_type) {
    return ptr;
}

void FFIMemory::mark(void* ptr) {
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    auto it = allocations_.find(ptr);
    if (it != allocations_.end()) {
        it->second.gc_mark = 1;
    }
}

void FFIMemory::sweep() {
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    for (auto it = allocations_.begin(); it != allocations_.end(); ) {
        if (it->second.gc_mark == 0) {
            total_used_ -= it->second.size;
            std::free(it->first);
            it = allocations_.erase(it);
        } else {
            it->second.gc_mark = 0;
            ++it;
        }
    }
}

void FFIMemory::attach_finalizer(void* ptr, std::function<void(void*)> finalizer) {
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    auto it = allocations_.find(ptr);
    if (it != allocations_.end()) {
        it->second.finalizer = finalizer;
    }
}

void* FFIMemory::to_native(const Value& v, std::shared_ptr<FFIType> type) {
    if (!type) return nullptr;

    switch (type->kind) {
        case FFITypeKind::BOOL: {
            uint8_t* p = static_cast<uint8_t*>(alloc_bytes(1));
            if (!p) return nullptr;
            *p = v.asBool() ? 1 : 0;
            return p;
        }
        case FFITypeKind::INT8: {
            int8_t* p = static_cast<int8_t*>(alloc_bytes(1));
            if (!p) return nullptr;
            *p = static_cast<int8_t>(v.asInt64());
            return p;
        }
        case FFITypeKind::INT16: {
            int16_t* p = static_cast<int16_t*>(alloc_bytes(2));
            if (!p) return nullptr;
            *p = static_cast<int16_t>(v.asInt64());
            return p;
        }
        case FFITypeKind::INT32: {
            int32_t* p = static_cast<int32_t*>(alloc_bytes(4));
            if (!p) return nullptr;
            *p = static_cast<int32_t>(v.asInt64());
            return p;
        }
        case FFITypeKind::INT64: {
            int64_t* p = static_cast<int64_t*>(alloc_bytes(8));
            if (!p) return nullptr;
            *p = v.asInt64();
            return p;
        }
        case FFITypeKind::UINT8: {
            uint8_t* p = static_cast<uint8_t*>(alloc_bytes(1));
            if (!p) return nullptr;
            *p = static_cast<uint8_t>(v.asInt64());
            return p;
        }
        case FFITypeKind::UINT16: {
            uint16_t* p = static_cast<uint16_t*>(alloc_bytes(2));
            if (!p) return nullptr;
            *p = static_cast<uint16_t>(v.asInt64());
            return p;
        }
        case FFITypeKind::UINT32: {
            uint32_t* p = static_cast<uint32_t*>(alloc_bytes(4));
            if (!p) return nullptr;
            *p = static_cast<uint32_t>(v.asInt64());
            return p;
        }
        case FFITypeKind::UINT64: {
            uint64_t* p = static_cast<uint64_t*>(alloc_bytes(8));
            if (!p) return nullptr;
            *p = static_cast<uint64_t>(v.asInt64());
            return p;
        }
        case FFITypeKind::FLOAT32: {
            float* p = static_cast<float*>(alloc_bytes(4));
            if (!p) return nullptr;
            *p = static_cast<float>(v.asDouble());
            return p;
        }
        case FFITypeKind::FLOAT64: {
            double* p = static_cast<double*>(alloc_bytes(8));
            if (!p) return nullptr;
            *p = v.asDouble();
            return p;
        }
        case FFITypeKind::POINTER:
            return v.asPtr();
        case FFITypeKind::STRING: {
            if (!v.isStringId() && !v.isStringValId()) {
                return nullptr;
            }
            // We can't resolve the string content here because we don't have
            // access to the VM string table. The caller (FFI module) must
            // resolve strings before calling to_native, or we need to accept
            // a resolution function. For now, return nullptr for string IDs.
            return nullptr;
        }
        default:
            return nullptr;
    }
}

Value FFIMemory::to_havel(void* ptr, std::shared_ptr<FFIType> type, bool take_ownership) {
    if (!ptr || !type) return Value::makeNull();
    
    switch (type->kind) {
    case FFITypeKind::INT8:
        return Value(static_cast<int64_t>(*reinterpret_cast<int8_t*>(ptr)));
    case FFITypeKind::INT16:
        return Value(static_cast<int64_t>(*reinterpret_cast<int16_t*>(ptr)));
    case FFITypeKind::INT32:
        return Value(static_cast<int64_t>(*reinterpret_cast<int32_t*>(ptr)));
    case FFITypeKind::INT64:
        return Value(*reinterpret_cast<int64_t*>(ptr));
    case FFITypeKind::UINT8:
        return Value(static_cast<int64_t>(*reinterpret_cast<uint8_t*>(ptr)));
    case FFITypeKind::UINT16:
        return Value(static_cast<int64_t>(*reinterpret_cast<uint16_t*>(ptr)));
    case FFITypeKind::UINT32:
        return Value(static_cast<int64_t>(*reinterpret_cast<uint32_t*>(ptr)));
    case FFITypeKind::UINT64:
        return Value(static_cast<int64_t>(*reinterpret_cast<uint64_t*>(ptr)));
    case FFITypeKind::BOOL:
        return Value::makeBool(*reinterpret_cast<uint8_t*>(ptr) != 0);
    case FFITypeKind::FLOAT32:
        return Value::makeDouble(static_cast<double>(*reinterpret_cast<float*>(ptr)));
    case FFITypeKind::FLOAT64:
        return Value::makeDouble(*reinterpret_cast<double*>(ptr));
    case FFITypeKind::STRING:
        return Value::makePtr(ptr);
    case FFITypeKind::POINTER:
        return Value::makePtr(*reinterpret_cast<void**>(ptr));
    case FFITypeKind::VOID:
        return Value::makeNull();
    default:
        return Value::makePtr(ptr);
    }
}

void FFIMemory::dump_stats() {
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    std::cout << "FFI Memory Stats:\n";
    std::cout << "  Allocations: " << allocations_.size() << "\n";
    std::cout << "  Total allocated: " << total_allocated_ << " bytes\n";
    std::cout << "  Total used: " << total_used_ << " bytes\n";
}

bool FFIMemory::is_valid(void* ptr) {
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    return allocations_.find(ptr) != allocations_.end();
}

size_t FFIMemory::total_allocated() {
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    return total_allocated_;
}

size_t FFIMemory::total_used() {
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    return total_used_;
}

} // namespace havel::ffi

#endif // HAVE_LIBFFI