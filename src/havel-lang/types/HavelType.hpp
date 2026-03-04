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

    virtual std::string toString() const = 0;
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

    std::string toString() const override {
        return "Struct<" + name_ + ">";
    }

private:
    std::string name_;
    std::vector<StructField> fields_;
    std::unordered_map<std::string, size_t> fieldIndex_;
    std::unordered_map<std::string, const ast::StructMethodDef*> methods_;
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
 * Type registry - stores all defined struct and enum types
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

    std::shared_ptr<HavelStructType> getStructType(const std::string& name) {
        auto it = structTypes_.find(name);
        return (it != structTypes_.end()) ? it->second : nullptr;
    }

    std::shared_ptr<HavelEnumType> getEnumType(const std::string& name) {
        auto it = enumTypes_.find(name);
        return (it != enumTypes_.end()) ? it->second : nullptr;
    }

    bool hasStructType(const std::string& name) const {
        return structTypes_.find(name) != structTypes_.end();
    }

    bool hasEnumType(const std::string& name) const {
        return enumTypes_.find(name) != enumTypes_.end();
    }

private:
    TypeRegistry() = default;
    
    std::unordered_map<std::string, std::shared_ptr<HavelStructType>> structTypes_;
    std::unordered_map<std::string, std::shared_ptr<HavelEnumType>> enumTypes_;
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

} // namespace havel
