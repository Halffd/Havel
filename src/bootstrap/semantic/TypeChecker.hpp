#pragma once

#include "../../ast/AST.h"
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace havel::compiler {

enum class TypeKind {
    Builtin,
    Nominal,
    Protocol,
    Nullable,
    Dynamic,
};

struct FunctionSignature {
    std::vector<std::string> paramTypes;
    std::string returnType;
    size_t arity = 0;
};

struct TypeInfo {
  TypeKind kind = TypeKind::Dynamic;
  std::string name;
  std::unordered_map<std::string, FunctionSignature> methods;
  std::vector<std::string> protocolNames;
  std::unordered_set<std::string> requiredMethodNames;
  // Key-value metadata for generic tracking:
  //   "typeParam:T" -> "true" for each type parameter T
  //   "typeParamCount" -> "N" for the number of type parameters
  std::unordered_map<std::string, std::string> metadata;

  bool hasMethod(const std::string &m) const { return methods.count(m) > 0; }

  bool isGeneric() const {
    return metadata.find("typeParamCount") != metadata.end();
  }
  size_t typeParamCount() const {
    auto it = metadata.find("typeParamCount");
    if (it == metadata.end()) return 0;
    return static_cast<size_t>(std::stoull(it->second));
  }
  const std::vector<std::string> typeParamNames() const {
    std::vector<std::string> names;
    for (const auto &[key, val] : metadata) {
      if (key.starts_with("typeParam:") && val == "true") {
        names.push_back(key.substr(10)); // skip "typeParam:"
      }
    }
    return names;
  }
};

struct DefaultMethodInjection {
    std::string typeName;
    std::string protocolName;
    std::string methodName;
    const ast::TraitMethod *defaultBody = nullptr;
};

struct TypeCheckResult {
  mutable std::unordered_map<std::string, TypeInfo> registry;
  std::unordered_map<const ast::Identifier *, std::string> knownTypes;
  std::unordered_set<const ast::Expression *> provablyTrueIs;
  std::unordered_set<const ast::CastExpression *> provablySafeCast;
  std::vector<DefaultMethodInjection> defaultInjections;
  mutable std::vector<std::string> errors;
  mutable std::vector<std::string> warnings;
};

class TypeChecker {
public:
    TypeChecker() = default;

    TypeCheckResult check(const ast::Program &program);

    const std::vector<std::string> &errors() const { return result_.errors; }
    const std::vector<std::string> &warnings() const { return result_.warnings; }

private:
    TypeCheckResult result_;

    struct TypeEnv {
        struct Frame {
            std::unordered_map<std::string, std::string> varTypes;
        };
        std::vector<Frame> frames;

        void push() { frames.emplace_back(); }
        void pop() { if (!frames.empty()) frames.pop_back(); }

        void set(const std::string &var, const std::string &type) {
            if (!frames.empty()) frames.back().varTypes[var] = type;
        }

        std::optional<std::string> lookup(const std::string &var) const {
            for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
                auto fit = it->varTypes.find(var);
                if (fit != it->varTypes.end()) return fit->second;
            }
            return std::nullopt;
        }
    };

    TypeEnv env_;
    const ast::Program *program_ = nullptr;

    static const std::unordered_set<std::string> BUILTIN_TYPES;

    void registerBuiltins();

    void collectDeclarations(const ast::Program &program);
    void collectProtocolDeclaration(const ast::ProtocolDeclaration &decl);
    void collectTraitDeclaration(const ast::TraitDeclaration &decl);
    void collectStructDeclaration(const ast::StructDeclaration &decl);
    void collectClassDeclaration(const ast::ClassDeclaration &decl);
    void collectFunctionDeclaration(const ast::FunctionDeclaration &decl);
  void collectEnumDeclaration(const ast::EnumDeclaration &decl);
  void collectImplDeclaration(const ast::ImplDeclaration &decl);

  void verifyProtocolConformance();
    void verifyProtocolConformanceForType(const std::string &typeName,
                                           const std::string &protoName);

    FunctionSignature signatureFromMethod(const ast::TraitMethod &method) const;
    FunctionSignature signatureFromFunction(const ast::FunctionDeclaration &fn) const;
  std::optional<std::string> resolveTypeAnnotation(const ast::TypeAnnotation *ann) const;
  std::optional<std::string> resolveTypeName(const std::string &name) const;
  std::optional<std::string> resolveGenericTypeRef(const ast::GenericTypeRef &ref) const;
    bool validateTypeArgCount(const std::string &baseName, size_t argCount) const;
    bool validateTypeArgBounds(const std::string &baseName,
                               const std::vector<std::string> &argTypeNames) const;
    std::vector<std::string> getTypeParamBounds(const std::string &baseName,
                                                 const std::string &paramName) const;
    bool typeSatisfiesBound(const std::string &argTypeName,
                            const std::string &boundName) const;

    bool isNullableType(const std::string &typeStr) const;
    std::string unwrapNullable(const std::string &typeStr) const;

    void checkStatement(const ast::Statement &stmt);
    void checkExpression(const ast::Expression &expr);
    void checkBlock(const ast::BlockStatement &block);
    void checkIfStatement(const ast::IfStatement &ifStmt);
    void checkWhenStatement(const ast::WhenStatement &whenStmt);
    void checkLetDeclaration(const ast::LetDeclaration &let);
    void checkFunctionDeclaration(const ast::FunctionDeclaration &fn);
    void checkAssignment(const ast::AssignmentExpression &assign);

    void narrowFromIsCheck(const ast::Expression &condition);
    std::string exprTypeName(const ast::Expression &expr) const;

    bool typeConformsToProtocol(const std::string &typeName,
                                const std::string &protoName) const;
};

} // namespace havel::compiler
