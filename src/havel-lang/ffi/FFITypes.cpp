#include "FFITypes.hpp"

#ifdef HAVE_LIBFFI

namespace havel::ffi {

std::unordered_map<std::string, std::shared_ptr<FFIType>> FFITypeRegistry::named_types_;
std::vector<std::shared_ptr<FFIType>> FFITypeRegistry::type_registry_;
bool FFITypeRegistry::initialized_ = false;

void FFITypeRegistry::init_builtins() {
    if (initialized_) return;
    
    auto v = std::make_shared<FFIType>();
    v->kind = FFITypeKind::VOID; v->name = "void";
    v->size = 0; v->alignment = 1;
    type_registry_.push_back(v); named_types_["void"] = v;
    
    auto b = std::make_shared<FFIType>();
    b->kind = FFITypeKind::BOOL; b->name = "bool";
    b->size = 1; b->alignment = 1;
    type_registry_.push_back(b); named_types_["bool"] = b;
    
    auto i8 = std::make_shared<FFIType>();
    i8->kind = FFITypeKind::INT8; i8->name = "int8_t";
    i8->size = 1; i8->alignment = 1;
    type_registry_.push_back(i8); named_types_["int8_t"] = i8;
    named_types_["int8"] = i8;
    
    auto i16 = std::make_shared<FFIType>();
    i16->kind = FFITypeKind::INT16; i16->name = "int16_t";
    i16->size = 2; i16->alignment = 2;
    type_registry_.push_back(i16); named_types_["int16_t"] = i16;
    named_types_["int16"] = i16;
    
    auto i32 = std::make_shared<FFIType>();
    i32->kind = FFITypeKind::INT32; i32->name = "int32_t";
    i32->size = 4; i32->alignment = 4;
    type_registry_.push_back(i32); named_types_["int32_t"] = i32;
    named_types_["int32"] = i32;
    
    auto i64 = std::make_shared<FFIType>();
    i64->kind = FFITypeKind::INT64; i64->name = "int64_t";
    i64->size = 8; i64->alignment = 8;
    type_registry_.push_back(i64); named_types_["int64_t"] = i64;
    named_types_["int64"] = i64;
    
    auto u8 = std::make_shared<FFIType>();
    u8->kind = FFITypeKind::UINT8; u8->name = "uint8_t";
    u8->size = 1; u8->alignment = 1;
    type_registry_.push_back(u8); named_types_["uint8_t"] = u8;
    named_types_["uint8"] = u8;
    
    auto u16 = std::make_shared<FFIType>();
    u16->kind = FFITypeKind::UINT16; u16->name = "uint16_t";
    u16->size = 2; u16->alignment = 2;
    type_registry_.push_back(u16); named_types_["uint16_t"] = u16;
    named_types_["uint16"] = u16;
    
    auto u32 = std::make_shared<FFIType>();
    u32->kind = FFITypeKind::UINT32; u32->name = "uint32_t";
    u32->size = 4; u32->alignment = 4;
    type_registry_.push_back(u32); named_types_["uint32_t"] = u32;
    named_types_["uint32"] = u32;
    
    auto u64 = std::make_shared<FFIType>();
    u64->kind = FFITypeKind::UINT64; u64->name = "uint64_t";
    u64->size = 8; u64->alignment = 8;
    type_registry_.push_back(u64); named_types_["uint64_t"] = u64;
    named_types_["uint64"] = u64;
    
    auto f32 = std::make_shared<FFIType>();
    f32->kind = FFITypeKind::FLOAT32; f32->name = "float";
    f32->size = 4; f32->alignment = 4;
    type_registry_.push_back(f32); named_types_["float"] = f32;
    named_types_["f32"] = f32;
    
    auto f64 = std::make_shared<FFIType>();
    f64->kind = FFITypeKind::FLOAT64; f64->name = "double";
    f64->size = 8; f64->alignment = 8;
    type_registry_.push_back(f64); named_types_["double"] = f64;
    named_types_["f64"] = f64;
    
    auto str = std::make_shared<FFIType>();
    str->kind = FFITypeKind::STRING; str->name = "char*";
    str->pointee = i8; str->size = 8; str->alignment = 8;
    type_registry_.push_back(str); named_types_["char*"] = str;
    named_types_["string"] = str;
    named_types_["char"] = i8;
    
    auto ptr = std::make_shared<FFIType>();
    ptr->kind = FFITypeKind::POINTER; ptr->name = "void*";
    ptr->pointee = nullptr; ptr->size = 8; ptr->alignment = 8;
    type_registry_.push_back(ptr); named_types_["void*"] = ptr;
    named_types_["pointer"] = ptr;
    
    initialized_ = true;
}

std::shared_ptr<FFIType> FFITypeRegistry::void_type() { init_builtins(); return named_types_.at("void"); }
std::shared_ptr<FFIType> FFITypeRegistry::bool_type() { init_builtins(); return named_types_.at("bool"); }
std::shared_ptr<FFIType> FFITypeRegistry::int8_type() { init_builtins(); return named_types_.at("int8_t"); }
std::shared_ptr<FFIType> FFITypeRegistry::int16_type() { init_builtins(); return named_types_.at("int16_t"); }
std::shared_ptr<FFIType> FFITypeRegistry::int32_type() { init_builtins(); return named_types_.at("int32_t"); }
std::shared_ptr<FFIType> FFITypeRegistry::int64_type() { init_builtins(); return named_types_.at("int64_t"); }
std::shared_ptr<FFIType> FFITypeRegistry::uint8_type() { init_builtins(); return named_types_.at("uint8_t"); }
std::shared_ptr<FFIType> FFITypeRegistry::uint16_type() { init_builtins(); return named_types_.at("uint16_t"); }
std::shared_ptr<FFIType> FFITypeRegistry::uint32_type() { init_builtins(); return named_types_.at("uint32_t"); }
std::shared_ptr<FFIType> FFITypeRegistry::uint64_type() { init_builtins(); return named_types_.at("uint64_t"); }
std::shared_ptr<FFIType> FFITypeRegistry::float32_type() { init_builtins(); return named_types_.at("float"); }
std::shared_ptr<FFIType> FFITypeRegistry::float64_type() { init_builtins(); return named_types_.at("double"); }
std::shared_ptr<FFIType> FFITypeRegistry::string_type() { init_builtins(); return named_types_.at("char*"); }

std::shared_ptr<FFIType> FFITypeRegistry::pointer_type(std::shared_ptr<FFIType> pointee) {
    init_builtins();
    auto t = std::make_shared<FFIType>();
    t->kind = FFITypeKind::POINTER;
    t->name = pointee ? pointee->name + "*" : "void*";
    t->pointee = pointee;
    t->size = 8; t->alignment = 8;
    return t;
}

std::shared_ptr<FFIType> FFITypeRegistry::array_type(std::shared_ptr<FFIType> element, size_t length) {
    auto t = std::make_shared<FFIType>();
    t->kind = FFITypeKind::ARRAY;
    t->name = element->name + "[" + std::to_string(length) + "]";
    t->element_type = element;
    t->array_length = length;
    t->size = element->size * length;
    t->alignment = element->alignment;
    return t;
}

std::shared_ptr<FFIType> FFITypeRegistry::struct_type(const std::string& name) {
    auto t = std::make_shared<FFIType>();
    t->kind = FFITypeKind::STRUCT;
    t->name = name;
    t->size = 0; t->alignment = 1;
    named_types_[name] = t;
    type_registry_.push_back(t);
    return t;
}

std::shared_ptr<FFIType> FFITypeRegistry::function_type(std::shared_ptr<FFIType> ret,
                                                     std::vector<std::shared_ptr<FFIType>> params,
                                                     bool variadic) {
    auto t = std::make_shared<FFIType>();
    t->kind = FFITypeKind::FUNCTION;
    t->return_type = ret;
    t->param_types = params;
    t->is_variadic = variadic;
    t->size = 8; t->alignment = 8;
    t->name = "function";
    return t;
}

size_t FFITypeRegistry::size_of(std::shared_ptr<FFIType> type) {
    if (!type) return 0;
    if (type->size == 0 && type->kind == FFITypeKind::STRUCT) {
        compute_layout(type);
    }
    return type->size;
}

size_t FFITypeRegistry::align_of(std::shared_ptr<FFIType> type) {
    if (!type) return 1;
    return type->alignment;
}

bool FFITypeRegistry::is_complete(std::shared_ptr<FFIType> type) {
    if (!type) return false;
    if (type->kind == FFITypeKind::VOID) return true;
    if (type->size > 0) return true;
    if (type->kind == FFITypeKind::STRUCT) {
        return type->size > 0;
    }
    return false;
}

uint32_t FFITypeRegistry::type_id(std::shared_ptr<FFIType> type) {
    if (!type) return 0;
    for (uint32_t i = 0; i < type_registry_.size(); i++) {
        if (type_registry_[i] == type) return i + 1;
    }
    type_registry_.push_back(type);
    return static_cast<uint32_t>(type_registry_.size());
}

std::shared_ptr<FFIType> FFITypeRegistry::from_type_id(uint32_t id) {
    if (id == 0 || id > type_registry_.size()) return nullptr;
    return type_registry_[id - 1];
}

std::shared_ptr<FFIType> FFITypeRegistry::from_name(const std::string& name) {
    init_builtins();
    auto it = named_types_.find(name);
    if (it != named_types_.end()) return it->second;
    
    size_t star_pos = name.rfind('*');
    if (star_pos != std::string::npos && star_pos > 0) {
        std::string pointee_name = name.substr(0, star_pos);
        auto pointee = from_name(pointee_name);
        if (pointee) return pointer_type(pointee);
    }
    
    size_t bracket_pos = name.find('[');
    if (bracket_pos != std::string::npos) {
        std::string element_name = name.substr(0, bracket_pos);
        auto element = from_name(element_name);
        if (element) {
            std::string len_str = name.substr(bracket_pos + 1);
            size_t len = std::stoi(len_str);
            return array_type(element, len);
        }
    }
    
    return nullptr;
}

void FFITypeRegistry::add_struct_field(std::shared_ptr<FFIType> type, const std::string& name, std::shared_ptr<FFIType> field_type) {
    if (!type || type->kind != FFITypeKind::STRUCT) return;
    type->fields.emplace_back(name, field_type);
}

void FFITypeRegistry::compute_layout(std::shared_ptr<FFIType> type) {
    if (!type || type->kind != FFITypeKind::STRUCT) return;
    if (type->size > 0) return;

    size_t offset = 0;
    size_t max_align = 1;
    for (auto& [name, field_type] : type->fields) {
        size_t align = field_type->alignment;
        if (offset % align != 0) {
            offset = (offset / align + 1) * align;
        }
        type->field_offsets[name] = offset;
        offset += size_of(field_type);
        if (align > max_align) max_align = align;
    }
    type->size = offset;
    if (offset % max_align != 0) {
        type->size = (offset / max_align + 1) * max_align;
    }
    type->alignment = max_align;
}

void FFITypeRegistry::register_typedef(const std::string& name, std::shared_ptr<FFIType> type) {
    init_builtins();
    named_types_[name] = type;
}

} // namespace havel::ffi

#endif // HAVE_LIBFFI