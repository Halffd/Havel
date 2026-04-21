#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace havel::ffi {

inline int8_t get_int8(void* ptr) { return *reinterpret_cast<int8_t*>(ptr); }
inline int16_t get_int16(void* ptr) { return *reinterpret_cast<int16_t*>(ptr); }
inline int32_t get_int32(void* ptr) { return *reinterpret_cast<int32_t*>(ptr); }
inline int64_t get_int64(void* ptr) { return *reinterpret_cast<int64_t*>(ptr); }
inline uint8_t get_uint8(void* ptr) { return *reinterpret_cast<uint8_t*>(ptr); }
inline uint16_t get_uint16(void* ptr) { return *reinterpret_cast<uint16_t*>(ptr); }
inline uint32_t get_uint32(void* ptr) { return *reinterpret_cast<uint32_t*>(ptr); }
inline uint64_t get_uint64(void* ptr) { return *reinterpret_cast<uint64_t*>(ptr); }
inline float get_float32(void* ptr) { return *reinterpret_cast<float*>(ptr); }
inline double get_float64(void* ptr) { return *reinterpret_cast<double*>(ptr); }
inline void* get_pointer(void* ptr) { return *reinterpret_cast<void**>(ptr); }

inline void set_int8(void* ptr, int8_t v) { *reinterpret_cast<int8_t*>(ptr) = v; }
inline void set_int16(void* ptr, int16_t v) { *reinterpret_cast<int16_t*>(ptr) = v; }
inline void set_int32(void* ptr, int32_t v) { *reinterpret_cast<int32_t*>(ptr) = v; }
inline void set_int64(void* ptr, int64_t v) { *reinterpret_cast<int64_t*>(ptr) = v; }
inline void set_uint8(void* ptr, uint8_t v) { *reinterpret_cast<uint8_t*>(ptr) = v; }
inline void set_uint16(void* ptr, uint16_t v) { *reinterpret_cast<uint16_t*>(ptr) = v; }
inline void set_uint32(void* ptr, uint32_t v) { *reinterpret_cast<uint32_t*>(ptr) = v; }
inline void set_uint64(void* ptr, uint64_t v) { *reinterpret_cast<uint64_t*>(ptr) = v; }
inline void set_float32(void* ptr, float v) { *reinterpret_cast<float*>(ptr) = v; }
inline void set_float64(void* ptr, double v) { *reinterpret_cast<double*>(ptr) = v; }
inline void set_pointer(void* ptr, void* v) { *reinterpret_cast<void**>(ptr) = v; }

inline void* add_offset(void* ptr, intptr_t offset) {
    return reinterpret_cast<char*>(ptr) + offset;
}

inline ptrdiff_t ptr_diff(void* ptr1, void* ptr2) {
    return reinterpret_cast<char*>(ptr1) - reinterpret_cast<char*>(ptr2);
}

}