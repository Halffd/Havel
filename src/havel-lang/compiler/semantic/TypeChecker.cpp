#include "TypeChecker.hpp"
#include "../../errors/ErrorSystem.h"
#include <algorithm>
#include <iostream>

namespace havel::compiler {

const std::unordered_set<std::string> TypeChecker::BUILTIN_TYPES = {
    "int", "integer", "number", "float", "double", "decimal",
    "string", "str", "bool", "boolean", "array", "list",
    "object", "map", "function", "fn", "closure", "class",
    "struct", "range", "set", "null"};

void TypeChecker::registerBuiltins() {
  for (const auto &name : {"int", "number", "string", "bool", "array",
                           "object", "function", "class", "struct",
                           "range", "set", "null"}) {
    TypeInfo info;
    info.kind = TypeKind::Builtin;
    info.name = name;
    result_.registry[name] = info;
  }
}

TypeCheckResult TypeChecker::check(const ast::Program &program) {
  result_ = TypeCheckResult();
  program_ = &program;
  env_ = TypeEnv();
  registerBuiltins();
  collectDeclarations(program);
  verifyProtocolConformance();

  env_.push();
  for (const auto &stmt : program.body) {
    if (stmt) checkStatement(*stmt);
  }
  env_.pop();

  return result_;
}

void TypeChecker::collectDeclarations(const ast::Program &program) {
  for (const auto &stmt : program.body) {
    if (!stmt) continue;
    switch (stmt->kind) {
    case ast::NodeType::ProtocolDeclaration:
      collectProtocolDeclaration(
          static_cast<const ast::ProtocolDeclaration &>(*stmt));
      break;
    case ast::NodeType::TraitDeclaration:
      collectTraitDeclaration(
          static_cast<const ast::TraitDeclaration &>(*stmt));
      break;
    case ast::NodeType::StructDeclaration:
      collectStructDeclaration(
          static_cast<const ast::StructDeclaration &>(*stmt));
      break;
    case ast::NodeType::ClassDeclaration:
      collectClassDeclaration(
          static_cast<const ast::ClassDeclaration &>(*stmt));
      break;
    case ast::NodeType::FunctionDeclaration:
      collectFunctionDeclaration(
          static_cast<const ast::FunctionDeclaration &>(*stmt));
      break;
    case ast::NodeType::ImplDeclaration:
      collectImplDeclaration(static_cast<const ast::ImplDeclaration &>(*stmt));
      break;
    default:
      break;
    }
  }
}

void TypeChecker::collectProtocolDeclaration(
    const ast::ProtocolDeclaration &decl) {
  TypeInfo info;
  info.kind = TypeKind::Protocol;
  info.name = decl.name ? decl.name->symbol : "";
  if (info.name.empty()) return;

  for (const auto &method : decl.methods) {
    if (!method || !method->name) continue;
    std::string mname = method->name->symbol;
    info.requiredMethodNames.insert(mname);
    info.methods[mname] = signatureFromMethod(*method);
  }

  auto existing = result_.registry.find(info.name);
  if (existing != result_.registry.end() &&
      existing->second.kind != TypeKind::Protocol) {
    result_.errors.push_back("type '" + info.name +
                             "' is already defined as a " +
                             (existing->second.kind == TypeKind::Nominal
                                  ? "nominal type"
                                  : "builtin type"));
    return;
  }
  result_.registry[info.name] = info;
}

void TypeChecker::collectTraitDeclaration(const ast::TraitDeclaration &decl) {
  TypeInfo info;
  info.kind = TypeKind::Protocol;
  info.name = decl.name ? decl.name->symbol : "";
  if (info.name.empty()) return;

  for (const auto &method : decl.methods) {
    if (!method || !method->name) continue;
    std::string mname = method->name->symbol;
    info.requiredMethodNames.insert(mname);
    info.methods[mname] = signatureFromMethod(*method);
  }

  result_.registry[info.name] = info;
}

void TypeChecker::collectStructDeclaration(
    const ast::StructDeclaration &decl) {
  TypeInfo info;
  info.kind = TypeKind::Nominal;
  info.name = decl.name;
  if (info.name.empty()) return;

  info.protocolNames = decl.protocolNames;

  for (const auto &method : decl.definition.methods) {
    if (!method) continue;
    std::string mname = method->name;
    FunctionSignature sig;
    sig.arity = method->parameters.size();
    info.methods[mname] = sig;
  }

  result_.registry[info.name] = info;
}

void TypeChecker::collectClassDeclaration(const ast::ClassDeclaration &decl) {
  TypeInfo info;
  info.kind = TypeKind::Nominal;
  info.name = decl.name;
  if (info.name.empty()) return;

  for (const auto &method : decl.definition.methods) {
    if (!method) continue;
    std::string mname = method->name;
    FunctionSignature sig;
    sig.arity = method->parameters.size();
    info.methods[mname] = sig;
  }

  result_.registry[info.name] = info;
}

void TypeChecker::collectFunctionDeclaration(
    const ast::FunctionDeclaration &fn) {
  if (!fn.name) return;
  TypeInfo info;
  info.kind = TypeKind::Builtin;
  info.name = fn.name->symbol;
  FunctionSignature sig;
  sig.arity = fn.parameters.size();
  info.methods[fn.name->symbol] = sig;
}

void TypeChecker::collectImplDeclaration(const ast::ImplDeclaration &impl) {
  std::string traitName =
      impl.traitName ? impl.traitName->symbol : "";
  std::string typeName =
      impl.typeName ? impl.typeName->symbol : "";
  if (traitName.empty() || typeName.empty()) return;

  auto typeIt = result_.registry.find(typeName);
  if (typeIt != result_.registry.end()) {
    if (std::find(typeIt->second.protocolNames.begin(),
                  typeIt->second.protocolNames.end(),
                  traitName) == typeIt->second.protocolNames.end()) {
      typeIt->second.protocolNames.push_back(traitName);
    }
  }

  for (const auto &func : impl.funcs) {
    if (!func || !func->name) continue;
    if (typeIt != result_.registry.end()) {
      FunctionSignature sig;
      sig.arity = func->parameters.size();
      typeIt->second.methods[func->name->symbol] = sig;
    }
  }
}

void TypeChecker::verifyProtocolConformance() {
  for (const auto &[typeName, info] : result_.registry) {
    if (info.kind != TypeKind::Nominal) continue;
    for (const auto &protoName : info.protocolNames) {
      verifyProtocolConformanceForType(typeName, protoName);
    }
  }
}

void TypeChecker::verifyProtocolConformanceForType(
    const std::string &typeName, const std::string &protoName) {
  auto protoIt = result_.registry.find(protoName);
  if (protoIt == result_.registry.end()) {
    result_.errors.push_back("type '" + typeName +
                             "' declares conformance to protocol '" +
                             protoName + "', which is not defined");
    return;
  }
  if (protoIt->second.kind != TypeKind::Protocol) {
    result_.errors.push_back("'" + protoName +
                             "' is not a protocol");
    return;
  }

  const auto &protoInfo = protoIt->second;
  auto typeIt = result_.registry.find(typeName);
  if (typeIt == result_.registry.end()) return;
  const auto &typeInfo = typeIt->second;

  for (const auto &requiredMethod : protoInfo.requiredMethodNames) {
    if (typeInfo.hasMethod(requiredMethod)) {
      const auto &protoSig = protoInfo.methods.at(requiredMethod);
      const auto &typeSig = typeInfo.methods.at(requiredMethod);
      if (protoSig.arity > 0 && typeSig.arity != protoSig.arity) {
        result_.warnings.push_back(
            "method '" + requiredMethod + "' on '" + typeName +
            "' has " + std::to_string(typeSig.arity) +
            " parameter(s) but protocol '" + protoName +
            "' declares " + std::to_string(protoSig.arity));
      }
      continue;
    }

    auto methodIt = protoInfo.methods.find(requiredMethod);
    if (methodIt != protoInfo.methods.end()) {
      const auto *protoDecl = program_;
      const ast::TraitMethod *defaultMethod = nullptr;
      for (const auto &stmt : protoDecl->body) {
        if (!stmt || stmt->kind != ast::NodeType::ProtocolDeclaration)
          continue;
        const auto &pd =
            static_cast<const ast::ProtocolDeclaration &>(*stmt);
        if (pd.name && pd.name->symbol == protoName) {
          for (const auto &m : pd.methods) {
            if (m && m->name && m->name->symbol == requiredMethod) {
              defaultMethod = m.get();
              break;
            }
          }
        }
      }
      if (defaultMethod && defaultMethod->defaultBody) {
        result_.defaultInjections.push_back(
            {typeName, protoName, requiredMethod, defaultMethod});
      } else {
        result_.errors.push_back(
            "type '" + typeName + "' does not implement required method '" +
            requiredMethod + "' from protocol '" + protoName + "'");
      }
    } else {
      result_.errors.push_back(
          "type '" + typeName + "' does not implement required method '" +
          requiredMethod + "' from protocol '" + protoName + "'");
    }
  }
}

FunctionSignature
TypeChecker::signatureFromMethod(const ast::TraitMethod &method) const {
  FunctionSignature sig;
  sig.arity = method.parameters.size();
  for (const auto &param : method.parameters) {
    if (param && param->typeAnnotation) {
      auto resolved = resolveTypeAnnotation((*param->typeAnnotation).get());
      sig.paramTypes.push_back(resolved.value_or(""));
    } else {
      sig.paramTypes.push_back("");
    }
  }
  return sig;
}

FunctionSignature
TypeChecker::signatureFromFunction(const ast::FunctionDeclaration &fn) const {
  FunctionSignature sig;
  sig.arity = fn.parameters.size();
  for (const auto &param : fn.parameters) {
    if (param && param->typeAnnotation) {
      auto resolved = resolveTypeAnnotation((*param->typeAnnotation).get());
      sig.paramTypes.push_back(resolved.value_or(""));
    } else {
      sig.paramTypes.push_back("");
    }
  }
  if (fn.returnType) {
    auto resolved = resolveTypeAnnotation((*fn.returnType).get());
    sig.returnType = resolved.value_or("");
  }
  return sig;
}

std::optional<std::string> TypeChecker::resolveTypeAnnotation(
    const ast::TypeAnnotation *ann) const {
  if (!ann || !ann->type) return std::nullopt;
  const auto *ref =
      dynamic_cast<const ast::TypeReference *>(ann->type.get());
  if (!ref) return std::nullopt;
  std::string name = ref->name;
  std::string lowered = name;
  for (char &ch : lowered)
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

  if (lowered == "int" || lowered == "integer") return "int";
  if (lowered == "num" || lowered == "number" || lowered == "float" ||
      lowered == "double" || lowered == "decimal")
    return "number";
  if (lowered == "str" || lowered == "string") return "string";
  if (lowered == "bool" || lowered == "boolean") return "bool";
  if (lowered == "list" || lowered == "array" || lowered == "vector")
    return "array";
  if (lowered == "obj" || lowered == "object" || lowered == "map")
    return "object";
  if (lowered == "fn" || lowered == "function" || lowered == "closure")
    return "function";
  if (lowered == "class") return "class";
  if (lowered == "struct") return "struct";
  if (lowered == "any" || lowered == "auto" || lowered == "unknown")
    return std::nullopt;

  if (result_.registry.count(name) > 0) return name;

  return std::nullopt;
}

void TypeChecker::checkStatement(const ast::Statement &stmt) {
  switch (stmt.kind) {
  case ast::NodeType::LetDeclaration:
    checkLetDeclaration(static_cast<const ast::LetDeclaration &>(stmt));
    break;
  case ast::NodeType::FunctionDeclaration:
    checkFunctionDeclaration(
        static_cast<const ast::FunctionDeclaration &>(stmt));
    break;
  case ast::NodeType::IfStatement:
    checkIfStatement(static_cast<const ast::IfStatement &>(stmt));
    break;
  case ast::NodeType::WhenStatement:
    checkWhenStatement(static_cast<const ast::WhenStatement &>(stmt));
    break;
  case ast::NodeType::BlockStatement:
    checkBlock(static_cast<const ast::BlockStatement &>(stmt));
    break;
  case ast::NodeType::ExpressionStatement:
    if (auto *es = dynamic_cast<const ast::ExpressionStatement *>(&stmt)) {
      if (es->expression) checkExpression(*es->expression);
    }
    break;
  default:
    break;
  }
}

void TypeChecker::checkExpression(const ast::Expression &expr) {
  switch (expr.kind) {
  case ast::NodeType::AssignmentExpression:
    if (auto *assign =
        dynamic_cast<const ast::AssignmentExpression *>(&expr)) {
      checkAssignment(*assign);
    }
    break;
  case ast::NodeType::BinaryExpression:
    if (auto *bin =
        dynamic_cast<const ast::BinaryExpression *>(&expr)) {
      if (bin->operator_ == ast::BinaryOperator::Is) {
        narrowFromIsCheck(expr);
      }
    }
    break;
  case ast::NodeType::CastExpression: {
    auto *cast = dynamic_cast<const ast::CastExpression *>(&expr);
    if (cast && cast->expr) {
      std::string srcType = exprTypeName(*cast->expr);
      if (!srcType.empty() && !cast->targetType.empty()) {
        if (typeConformsToProtocol(srcType, cast->targetType) ||
            srcType == cast->targetType) {
          result_.provablySafeCast.insert(cast);
        }
      }
    }
    break;
  }
  case ast::NodeType::CallExpression:
    break;
  default:
    break;
  }
}

void TypeChecker::checkBlock(const ast::BlockStatement &block) {
  env_.push();
  for (const auto &stmt : block.body) {
    if (stmt) checkStatement(*stmt);
  }
  env_.pop();
}

void TypeChecker::checkIfStatement(const ast::IfStatement &ifStmt) {
  if (ifStmt.condition) {
    checkExpression(*ifStmt.condition);
  }
  env_.push();
  if (ifStmt.condition) {
    narrowFromIsCheck(*ifStmt.condition);
  }
  if (ifStmt.consequence) checkStatement(*ifStmt.consequence);
  env_.pop();
  if (ifStmt.alternative) checkStatement(*ifStmt.alternative);
}

void TypeChecker::checkWhenStatement(const ast::WhenStatement &whenStmt) {
  if (whenStmt.trigger) checkExpression(*whenStmt.trigger);
  if (whenStmt.body) checkStatement(*whenStmt.body);
}

void TypeChecker::checkLetDeclaration(const ast::LetDeclaration &let) {
  std::string varName;
  if (let.pattern &&
      let.pattern->kind == ast::NodeType::Identifier) {
    varName = static_cast<const ast::Identifier &>(*let.pattern).symbol;
  }

  if (let.typeAnnotation) {
    auto resolved =
      resolveTypeAnnotation((*let.typeAnnotation).get());
    if (resolved && !varName.empty()) {
      env_.set(varName, *resolved);
      if (let.value) {
        std::string valType = exprTypeName(*let.value);
        if (!valType.empty() && valType != *resolved) {
          auto expectedIt = result_.registry.find(*resolved);
          auto valIt = result_.registry.find(valType);
          if (expectedIt != result_.registry.end() &&
              expectedIt->second.kind == TypeKind::Nominal &&
              valIt != result_.registry.end() &&
              valIt->second.kind == TypeKind::Nominal) {
            if (valType != *resolved) {
              result_.errors.push_back(
                  "cannot assign value of type '" + valType +
                  "' to variable '" + varName + "' of type '" +
                  *resolved + "'");
            }
          }
        }
      }
} else if (!resolved && let.typeAnnotation) {
    const auto *ref =
      dynamic_cast<const ast::TypeReference *>((*let.typeAnnotation)->type.get());
    if (ref && !BUILTIN_TYPES.count(ref->name) &&
        result_.registry.find(ref->name) == result_.registry.end()) {
        result_.errors.push_back("unknown type '" + ref->name + "'");
    }
}
  } else if (!varName.empty() && let.value) {
    std::string valType = exprTypeName(*let.value);
    if (!valType.empty()) {
      env_.set(varName, valType);
    }
  }
}

void TypeChecker::checkFunctionDeclaration(
    const ast::FunctionDeclaration &fn) {
  if (!fn.name) return;
  std::string fnName = fn.name->symbol;
  env_.set(fnName, "function");

  env_.push();
  for (const auto &param : fn.parameters) {
    if (!param) continue;
    auto *ident =
        dynamic_cast<const ast::Identifier *>(param->pattern.get());
    if (!ident) continue;
    if (param->typeAnnotation) {
      auto resolved = resolveTypeAnnotation((*param->typeAnnotation).get());
      if (resolved) {
        env_.set(ident->symbol, *resolved);
      }
    }
  }

  if (fn.body) {
    if (fn.body->kind == ast::NodeType::BlockStatement) {
      checkBlock(static_cast<const ast::BlockStatement &>(*fn.body));
    }
  }
  env_.pop();
}

void TypeChecker::checkAssignment(const ast::AssignmentExpression &assign) {
  if (!assign.target) return;
  if (assign.target->kind != ast::NodeType::Identifier) return;
  std::string varName =
      static_cast<const ast::Identifier &>(*assign.target).symbol;
  auto knownType = env_.lookup(varName);
  if (!knownType) return;

  if (assign.value) {
    std::string valType = exprTypeName(*assign.value);
    if (!valType.empty() && valType != *knownType) {
      auto expectedIt = result_.registry.find(*knownType);
      auto valIt = result_.registry.find(valType);
      if (expectedIt != result_.registry.end() &&
          expectedIt->second.kind == TypeKind::Nominal &&
          valIt != result_.registry.end() &&
          valIt->second.kind == TypeKind::Nominal) {
        result_.errors.push_back(
            "cannot assign value of type '" + valType + "' to variable '" +
            varName + "' of type '" + *knownType + "'");
      }
    }
  }
}

void TypeChecker::narrowFromIsCheck(const ast::Expression &condition) {
  if (condition.kind == ast::NodeType::BinaryExpression) {
    auto *bin =
        dynamic_cast<const ast::BinaryExpression *>(&condition);
    if (bin && bin->operator_ == ast::BinaryOperator::Is) {
      if (bin->left && bin->left->kind == ast::NodeType::Identifier &&
          bin->right &&
          bin->right->kind == ast::NodeType::Identifier) {
        std::string varName =
            static_cast<const ast::Identifier &>(*bin->left).symbol;
        std::string protoName =
            static_cast<const ast::Identifier &>(*bin->right).symbol;
        if (result_.registry.count(protoName) > 0) {
          env_.set(varName, protoName);
        }
      }
    }
  }
}

std::string TypeChecker::exprTypeName(const ast::Expression &expr) const {
    switch (expr.kind) {
    case ast::NodeType::NumberLiteral:
        return "number";
    case ast::NodeType::StringLiteral:
        return "string";
    case ast::NodeType::BooleanLiteral:
        return "bool";
  case ast::NodeType::ArrayLiteral:
    return "array";
  case ast::NodeType::NullLiteral:
    return "null";
  case ast::NodeType::Identifier: {
    const auto &ident = static_cast<const ast::Identifier &>(expr);
    auto known = env_.lookup(ident.symbol);
    if (known) return *known;
    auto it = result_.registry.find(ident.symbol);
    if (it != result_.registry.end() &&
        it->second.kind == TypeKind::Nominal) {
      return ident.symbol;
    }
    return "";
  }
  case ast::NodeType::CallExpression: {
    const auto &call =
        static_cast<const ast::CallExpression &>(expr);
    if (call.callee &&
        call.callee->kind == ast::NodeType::Identifier) {
      std::string calleeName =
          static_cast<const ast::Identifier &>(*call.callee).symbol;
      auto it = result_.registry.find(calleeName);
      if (it != result_.registry.end() &&
          it->second.kind == TypeKind::Nominal) {
        return calleeName;
      }
    }
    return "";
  }
  default:
    return "";
  }
}

bool TypeChecker::typeConformsToProtocol(const std::string &typeName,
                                         const std::string &protoName) const {
  auto typeIt = result_.registry.find(typeName);
  if (typeIt == result_.registry.end()) return false;

  const auto &protocols = typeIt->second.protocolNames;
  if (std::find(protocols.begin(), protocols.end(), protoName) !=
      protocols.end())
    return true;

  auto protoIt = result_.registry.find(protoName);
  if (protoIt == result_.registry.end()) return false;
  const auto &requiredMethods = protoIt->second.requiredMethodNames;
  for (const auto &m : requiredMethods) {
    if (!typeIt->second.hasMethod(m)) return false;
  }
  return true;
}

} // namespace havel::compiler
