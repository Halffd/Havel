#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace havel::ffi {

enum class FFITypeKind : uint8_t {
    VOID,
    BOOL,
    INT8, INT16, INT32, INT64,
    UINT8, UINT16, UINT32, UINT64,
    FLOAT32, FLOAT64,
    POINTER,
    ARRAY,
    STRUCT,
    UNION,
    FUNCTION,
    STRING
};

struct FFIType {
    FFITypeKind kind;
    size_t size = 0;
    size_t alignment = 1;
    std::string name;
    
    std::shared_ptr<FFIType> pointee;
    size_t array_length = 0;
    std::shared_ptr<FFIType> element_type;
    std::vector<std::pair<std::string, std::shared_ptr<FFIType>>> fields;
    std::unordered_map<std::string, size_t> field_offsets;
    std::shared_ptr<FFIType> return_type;
    std::vector<std::shared_ptr<FFIType>> param_types;
    bool is_variadic = false;
    
    bool isScalar() const {
        return kind != FFITypeKind::POINTER && 
               kind != FFITypeKind::ARRAY &&
               kind != FFITypeKind::STRUCT &&
               kind != FFITypeKind::UNION &&
               kind != FFITypeKind::FUNCTION &&
               kind != FFITypeKind::STRING;
    }
};

class FFITypeRegistry {
public:
    static std::shared_ptr<FFIType> void_type();
    static std::shared_ptr<FFIType> bool_type();
    static std::shared_ptr<FFIType> int8_type();
    static std::shared_ptr<FFIType> int16_type();
    static std::shared_ptr<FFIType> int32_type();
    static std::shared_ptr<FFIType> int64_type();
    static std::shared_ptr<FFIType> uint8_type();
    static std::shared_ptr<FFIType> uint16_type();
    static std::shared_ptr<FFIType> uint32_type();
    static std::shared_ptr<FFIType> uint64_type();
    static std::shared_ptr<FFIType> float32_type();
    static std::shared_ptr<FFIType> float64_type();
    static std::shared_ptr<FFIType> string_type();
    static std::shared_ptr<FFIType> pointer_type(std::shared_ptr<FFIType> pointee);
    static std::shared_ptr<FFIType> array_type(std::shared_ptr<FFIType> element, size_t length);
    static std::shared_ptr<FFIType> struct_type(const std::string& name);
    static std::shared_ptr<FFIType> function_type(std::shared_ptr<FFIType> ret,
                                             std::vector<std::shared_ptr<FFIType>> params,
                                             bool variadic = false);
    
    static size_t size_of(std::shared_ptr<FFIType> type);
    static size_t align_of(std::shared_ptr<FFIType> type);
    static bool is_complete(std::shared_ptr<FFIType> type);
    static uint32_t type_id(std::shared_ptr<FFIType> type);
    static std::shared_ptr<FFIType> from_type_id(uint32_t id);
    static std::shared_ptr<FFIType> from_name(const std::string& name);
    
    static void add_struct_field(std::shared_ptr<FFIType> type, const std::string& name, std::shared_ptr<FFIType> field_type);
    static void compute_layout(std::shared_ptr<FFIType> type);
    
private:
    static std::unordered_map<std::string, std::shared_ptr<FFIType>> named_types_;
    static std::vector<std::shared_ptr<FFIType>> type_registry_;
    static bool initialized_;
};

}