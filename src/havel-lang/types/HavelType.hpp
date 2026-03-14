#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>
#include <variant>
#include <functional>

namespace havel {

// Forward declarations
class HavelType;
class HavelStructType;
class HavelEnumType;

namespace ast {
    struct StructMethodDef;  // Forward declaration
}

/**
 * Type checking modes for gradual typing
 */
enum class TypeMode {
    None,    // Ignore types entirely (purely dynamic)
    Warn,    // Print warnings on type mismatches
    Strict   // Runtime error on type mismatch
};

/**
 * Base type descriptor for gradual typing
 * 
 * Types exist for:
 * - Static checking
 * - IDE support
 * - Optional runtime validation
 * - Future compilation/optimization hints
 * 
 * Runtime ignores types unless in Strict mode.
 */
class HavelType {
public:
    enum class Kind {
        Any,       // Dynamic, no type checking
        Num,       // Numeric (double)
        Str,       // String
        Bool,      // Boolean
        Array,     // Array of values
        Object,    // Dynamic object (map)
        Struct,    // Struct instance
        Enum,      // Enum value
        Func,      // Function
        Null       // Null value
    };

    HavelType(Kind kind) : kind_(kind) {}
    virtual ~HavelType() = default;

    Kind getKind() const { return kind_; }

    virtual std::string toString() const {
        switch (kind_) {
            case Kind::Any: return "Any";
            case Kind::Num: return "Num";
            case Kind::Str: return "Str";
            case Kind::Bool: return "Bool";
            case Kind::Array: return "Array";
            case Kind::Object: return "Object";
            case Kind::Struct: return "Struct";
            case Kind::Enum: return "Enum";
            case Kind::Func: return "Func";
            case Kind::Null: return "Null";
            default: return "Unknown";
        }
    }
    
    virtual bool isCompatible(const HavelType& other) const {
        // Base implementation: types are compatible if they have the same kind
        // or if one is Any type
        return kind_ == Kind::Any || other.kind_ == Kind::Any || kind_ == other.kind_;
    }

    // Type factory methods
    static std::shared_ptr<HavelType> any();
    static std::shared_ptr<HavelType> num();
    static std::shared_ptr<HavelType> str();
    static std::shared_ptr<HavelType> boolean();
    static std::shared_ptr<HavelType> null();

private:
    Kind kind_;
};

/**
 * Simple concrete type for basic types (Num, Str, Bool, etc.)
 */
class SimpleType : public HavelType {
public:
    explicit SimpleType(Kind kind) : HavelType(kind) {}
};

/**
 * Struct field definition
 */
struct StructField {
    std::string name;
    std::optional<std::shared_ptr<HavelType>> type;  // Optional - if nullopt, untyped
    
    StructField() = default;
    StructField(const std::string& name, std::optional<std::shared_ptr<HavelType>> type = std::nullopt)
        : name(name), type(type) {}
};

/**
 * Struct type descriptor
 * 
 * Example:
 * struct Vec2 {
 *     x: Num
 *     y: Num
 * }
 */
class HavelStructType : public HavelType {
public:
    HavelStructType(const std::string& name)
        : HavelType(Kind::Struct), name_(name) {}

    const std::string& getName() const { return name_; }

    void addField(const StructField& field) {
        fields_.push_back(field);
        fieldIndex_[field.name] = fields_.size() - 1;
    }

    const std::vector<StructField>& getFields() const { return fields_; }

    size_t getFieldCount() const { return fields_.size(); }

    const StructField* getField(const std::string& name) const {
        auto it = fieldIndex_.find(name);
        if (it != fieldIndex_.end()) {
            return &fields_[it->second];
        }
        return nullptr;
    }

    std::optional<size_t> getFieldIndex(const std::string& name) const {
        auto it = fieldIndex_.find(name);
        if (it != fieldIndex_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    // Method support
    void addMethod(const std::string& name, const ast::StructMethodDef* method) {
        methods_[name] = method;
    }

    const ast::StructMethodDef* getMethod(const std::string& name) const {
        auto it = methods_.find(name);
        if (it != methods_.end()) {
            return it->second;
        }
        return nullptr;
    }

    bool hasMethod(const std::string& name) const {
        return methods_.find(name) != methods_.end();
    }
    
    // Operator support (op_add, op_sub, op_mul, op_div, op_eq, op_lt, etc.)
    void addOperator(const std::string& opName, const ast::StructMethodDef* method) {
        operators_[opName] = method;
    }
    
    const ast::StructMethodDef* getOperator(const std::string& opName) const {
        auto it = operators_.find(opName);
        if (it != operators_.end()) {
            return it->second;
        }
        return nullptr;
    }
    
    bool hasOperator(const std::string& opName) const {
        return operators_.find(opName) != operators_.end();
    }

    std::string toString() const override {
        return "Struct<" + name_ + ">";
    }

private:
    std::string name_;
    std::vector<StructField> fields_;
    std::unordered_map<std::string, size_t> fieldIndex_;
    std::unordered_map<std::string, const ast::StructMethodDef*> methods_;
    std::unordered_map<std::string, const ast::StructMethodDef*> operators_;  // op_add, op_sub, etc.
};

/**
 * Enum variant definition
 */
struct EnumVariant {
    std::string name;
    bool hasPayload = false;
    std::optional<std::shared_ptr<HavelType>> payloadType;  // Optional payload type
    
    EnumVariant() = default;
    EnumVariant(const std::string& name, bool hasPayload = false, 
                std::optional<std::shared_ptr<HavelType>> payloadType = std::nullopt)
        : name(name), hasPayload(hasPayload), payloadType(payloadType) {}
};

/**
 * Enum type descriptor
 * 
 * Example (simple):
 * enum Color { Red, Green, Blue }
 * 
 * Example (with payloads):
 * enum Result {
 *     Ok(value)
 *     Err(message)
 * }
 */
class HavelEnumType : public HavelType {
public:
    HavelEnumType(const std::string& name)
        : HavelType(Kind::Enum), name_(name) {}

    const std::string& getName() const { return name_; }
    
    void addVariant(const EnumVariant& variant) {
        variants_.push_back(variant);
        variantIndex_[variant.name] = variants_.size() - 1;
    }

    const std::vector<EnumVariant>& getVariants() const { return variants_; }
    
    const EnumVariant* getVariant(const std::string& name) const {
        auto it = variantIndex_.find(name);
        if (it != variantIndex_.end()) {
            return &variants_[it->second];
        }
        return nullptr;
    }

    std::optional<size_t> getVariantIndex(const std::string& name) const {
        auto it = variantIndex_.find(name);
        if (it != variantIndex_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    std::string toString() const override {
        return "Enum<" + name_ + ">";
    }

private:
    std::string name_;
    std::vector<EnumVariant> variants_;
    std::unordered_map<std::string, size_t> variantIndex_;
};

/**
 * Array type with optional element type
 */
class HavelArrayType : public HavelType {
public:
    HavelArrayType(std::optional<std::shared_ptr<HavelType>> elementType = std::nullopt)
        : HavelType(Kind::Array), elementType_(elementType) {}

    std::optional<std::shared_ptr<HavelType>> getElementType() const { return elementType_; }

    std::string toString() const override {
        if (elementType_) {
            return "Array<" + (*elementType_)->toString() + ">";
        }
        return "Array";
    }

private:
    std::optional<std::shared_ptr<HavelType>> elementType_;
};

/**
 * Function type with optional parameter and return types
 */
class HavelFunctionType : public HavelType {
public:
    HavelFunctionType(
        std::vector<std::shared_ptr<HavelType>> paramTypes = {},
        std::optional<std::shared_ptr<HavelType>> returnType = std::nullopt)
        : HavelType(Kind::Func), paramTypes_(std::move(paramTypes)), returnType_(returnType) {}

    const std::vector<std::shared_ptr<HavelType>>& getParamTypes() const { return paramTypes_; }
    std::optional<std::shared_ptr<HavelType>> getReturnType() const { return returnType_; }

    std::string toString() const override {
        std::string result = "Func(";
        for (size_t i = 0; i < paramTypes_.size(); ++i) {
            if (i > 0) result += ", ";
            result += paramTypes_[i]->toString();
        }
        result += ")";
        if (returnType_) {
            result += " -> " + (*returnType_)->toString();
        }
        return result;
    }

private:
    std::vector<std::shared_ptr<HavelType>> paramTypes_;
    std::optional<std::shared_ptr<HavelType>> returnType_;
};

/**
 * Union type (e.g., Result<T,E> = Ok(T) | Error(E))
 */
class HavelUnionType : public HavelType {
public:
    struct Variant {
        std::string name;
        std::shared_ptr<HavelType> type;
        
        Variant(const std::string& n, std::shared_ptr<HavelType> t) : name(n), type(t) {}
    };
    
    HavelUnionType(const std::string& name) 
        : HavelType(Kind::Any), name_(name) {}
    
    void addVariant(const std::string& name, std::shared_ptr<HavelType> type) {
        variants_.emplace_back(name, type);
    }
    
    const std::vector<Variant>& getVariants() const { return variants_; }
    const std::string& getName() const { return name_; }
    
    std::string toString() const override {
        std::string result = name_ + " = ";
        for (size_t i = 0; i < variants_.size(); ++i) {
            if (i > 0) result += " | ";
            result += variants_[i].name;
            if (variants_[i].type) {
                result += "(" + variants_[i].type->toString() + ")";
            }
        }
        return result;
    }
    
private:
    std::string name_;
    std::vector<Variant> variants_;
};

/**
 * Record type (e.g., {name: String, age: Int})
 */
class HavelRecordType : public HavelType {
public:
    struct Field {
        std::string name;
        std::shared_ptr<HavelType> type;
        
        Field(const std::string& n, std::shared_ptr<HavelType> t) : name(n), type(t) {}
    };
    
    HavelRecordType() : HavelType(Kind::Object) {}
    
    void addField(const std::string& name, std::shared_ptr<HavelType> type) {
        fields_.emplace_back(name, type);
        fieldIndex_[name] = fields_.size() - 1;
    }
    
    const std::vector<Field>& getFields() const { return fields_; }
    
    const Field* getField(const std::string& name) const {
        auto it = fieldIndex_.find(name);
        if (it != fieldIndex_.end()) {
            return &fields_[it->second];
        }
        return nullptr;
    }
    
    std::string toString() const override {
        std::string result = "{";
        for (size_t i = 0; i < fields_.size(); ++i) {
            if (i > 0) result += ", ";
            result += fields_[i].name + ": " + fields_[i].type->toString();
        }
        result += "}";
        return result;
    }
    
private:
    std::vector<Field> fields_;
    std::unordered_map<std::string, size_t> fieldIndex_;
};

/**
 * Type registry - stores all defined struct, enum, and type alias types
 */
class TypeRegistry {
public:
    static TypeRegistry& getInstance() {
        static TypeRegistry instance;
        return instance;
    }

    void registerStructType(std::shared_ptr<HavelStructType> type) {
        structTypes_[type->getName()] = type;
    }

    void registerEnumType(std::shared_ptr<HavelEnumType> type) {
        enumTypes_[type->getName()] = type;
    }
    
    void registerTypeAlias(const std::string& name, std::shared_ptr<HavelType> type) {
        typeAliases_[name] = type;
    }

    std::shared_ptr<HavelStructType> getStructType(const std::string& name) {
        auto it = structTypes_.find(name);
        return (it != structTypes_.end()) ? it->second : nullptr;
    }

    std::shared_ptr<HavelEnumType> getEnumType(const std::string& name) {
        auto it = enumTypes_.find(name);
        return (it != enumTypes_.end()) ? it->second : nullptr;
    }
    
    std::shared_ptr<HavelType> getTypeAlias(const std::string& name) {
        auto it = typeAliases_.find(name);
        return (it != typeAliases_.end()) ? it->second : nullptr;
    }

    bool hasStructType(const std::string& name) const {
        return structTypes_.find(name) != structTypes_.end();
    }

    bool hasEnumType(const std::string& name) const {
        return enumTypes_.find(name) != enumTypes_.end();
    }
    
    bool hasTypeAlias(const std::string& name) const {
        return typeAliases_.find(name) != typeAliases_.end();
    }

private:
    TypeRegistry() = default;

    std::unordered_map<std::string, std::shared_ptr<HavelStructType>> structTypes_;
    std::unordered_map<std::string, std::shared_ptr<HavelEnumType>> enumTypes_;
    std::unordered_map<std::string, std::shared_ptr<HavelType>> typeAliases_;
};

/**
 * Global type checking mode
 */
class TypeChecker {
public:
    static TypeChecker& getInstance() {
        static TypeChecker instance;
        return instance;
    }

    TypeMode getMode() const { return mode_; }
    void setMode(TypeMode mode) { mode_ = mode; }

    // Type compatibility check - implemented in Interpreter.cpp
    // bool checkCompatibility(const HavelValue& value,
    //                        const std::shared_ptr<HavelType>& expectedType,
    //                        const std::string& context = "");

    // Validate object/struct has required fields - implemented in Interpreter.cpp
    // bool validateStructFields(const HavelValue& value,
    //                          const HavelStructType& structType,
    //                          const std::string& context = "");

private:
    TypeChecker() = default;
    TypeMode mode_ = TypeMode::None;
};

// ============================================================================
// Inline factory method implementations
// ============================================================================

inline std::shared_ptr<HavelType> HavelType::any() {
    return std::make_shared<SimpleType>(Kind::Any);
}

inline std::shared_ptr<HavelType> HavelType::num() {
    return std::make_shared<SimpleType>(Kind::Num);
}

inline std::shared_ptr<HavelType> HavelType::str() {
    return std::make_shared<SimpleType>(Kind::Str);
}

inline std::shared_ptr<HavelType> HavelType::boolean() {
    return std::make_shared<SimpleType>(Kind::Bool);
}

inline std::shared_ptr<HavelType> HavelType::null() {
    return std::make_shared<SimpleType>(Kind::Null);
}

} // namespace havel
