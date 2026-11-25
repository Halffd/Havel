// src/havel-lang/ast/YieldAnalysis.hpp
//
// The SINGLE implementation of "does this function body reach a yield/await".
//
// This exists to hoist generator detection out of the compiler. Before, the
// compiler decided `is_generator` by re-walking each function's whole body AST
// at the END of compiling that function — once per function, so a module of n
// functions cost n walks over n subtrees. Now the parser computes it once,
// while it is already visiting the node, and the compiler reads the flag.
//
// WHY ONE FILE AND NOT A COPY IN THE PARSER AND ANOTHER IN THE COMPILER:
// `is_generator` decides whether a call returns a coroutine object (VM.cpp) or
// runs the body inline, so the two walks must agree on EVERY function for the
// whole language. Two copies would eventually disagree. The compiler's old
// private walks were DELETED rather than kept as forwarders -- they had no
// callers left, and a shim that exists only to satisfy its own declaration is
// dead weight. Everything reaches this file directly.
//
// Deliberate over-approximations, PRESERVED from the original walk, NOT fixed:
//   - TryExpression conservatively returns true, so a function whose only
//     suspicious construct is a try/catch is classified a generator.
//   - There is NO ReturnStatement case. `return <expr>` containing a yield is
//     therefore NOT detected, which is the pre-existing behavior.
// Changing either is a behavior-changing task of its own, with its own
// regression gate: scripts/tests/test_is_generator_parity.hv.
//
// The nesting rule: a nested function/lambda is a SEPARATE function and gets
// its own flag, so its yield does not make the ENCLOSING function a generator.
#pragma once

#include "AST.h"

namespace havel::ast {

namespace detail {

inline bool yieldStatementContainsYield(const Statement &stmt);
inline bool yieldExpressionContainsYield(const Expression &expr);

inline bool yieldStatementContainsYield(const Statement &stmt) {
  switch (stmt.kind) {
  case NodeType::ExpressionStatement: {
    const auto &e = static_cast<const ExpressionStatement &>(stmt);
    return e.expression && yieldExpressionContainsYield(*e.expression);
  }

  case NodeType::BlockStatement: {
    const auto &b = static_cast<const BlockStatement &>(stmt);
    for (const auto &s : b.body) {
      if (s && yieldStatementContainsYield(*s)) {
        return true;
      }
    }
    return false;
  }

  case NodeType::IfStatement: {
    const auto &i = static_cast<const IfStatement &>(stmt);
    if (i.condition && yieldExpressionContainsYield(*i.condition)) {
      return true;
    }
    if (i.consequence && yieldStatementContainsYield(*i.consequence)) {
      return true;
    }
    return i.alternative && yieldStatementContainsYield(*i.alternative);
  }

  case NodeType::WhileStatement: {
    const auto &w = static_cast<const WhileStatement &>(stmt);
    if (w.condition && yieldExpressionContainsYield(*w.condition)) {
      return true;
    }
    return w.body && yieldStatementContainsYield(*w.body);
  }

  case NodeType::DoWhileStatement: {
    const auto &d = static_cast<const DoWhileStatement &>(stmt);
    if (d.body && yieldStatementContainsYield(*d.body)) {
      return true;
    }
    return d.condition && yieldExpressionContainsYield(*d.condition);
  }

  case NodeType::ForStatement: {
    const auto &f = static_cast<const ForStatement &>(stmt);
    if (f.iterable && yieldExpressionContainsYield(*f.iterable)) {
      return true;
    }
    return f.body && yieldStatementContainsYield(*f.body);
  }

  case NodeType::LoopStatement: {
    const auto &l = static_cast<const LoopStatement &>(stmt);
    if (l.condition && yieldExpressionContainsYield(*l.condition)) {
      return true;
    }
    if (l.countExpr && yieldExpressionContainsYield(*l.countExpr)) {
      return true;
    }
    return l.body && yieldStatementContainsYield(*l.body);
  }

  case NodeType::TryExpression: {
    const auto &t = static_cast<const TryExpression &>(stmt);
    if (t.tryBody && yieldStatementContainsYield(*t.tryBody)) {
      return true;
    }
    if (t.catchBody && yieldStatementContainsYield(*t.catchBody)) {
      return true;
    }
    return t.finallyBlock && yieldStatementContainsYield(*t.finallyBlock);
  }

  case NodeType::HotkeyBinding: {
    const auto &h = static_cast<const HotkeyBinding &>(stmt);
    return h.action && yieldStatementContainsYield(*h.action);
  }

  case NodeType::WaitStatement: {
    const auto &w = static_cast<const WaitStatement &>(stmt);
    return w.condition && yieldExpressionContainsYield(*w.condition);
  }

  case NodeType::WithStatement: {
    const auto &w = static_cast<const WithStatement &>(stmt);
    for (const auto &s : w.body) {
      if (s && yieldStatementContainsYield(*s)) {
        return true;
      }
    }
    return false;
  }

    // Nested functions are compiled separately and get their own is_generator
    // flag: their yield does not make the ENCLOSING function a generator.
  case NodeType::FunctionDeclaration:
  case NodeType::ClassDeclaration:
  case NodeType::EnumDeclaration:
  case NodeType::DecoratorStatement:
    return false;

  default:
    // No ReturnStatement case, deliberately (see the file header): a
    // `return <expr with yield>` is not detected, matching the original walk.
    return false;
  }
}

inline bool yieldExpressionContainsYield(const Expression &expr) {
  switch (expr.kind) {
  case NodeType::YieldExpression:
    return true;

  case NodeType::AwaitExpression:
    return true;

  case NodeType::BinaryExpression: {
    const auto &b = static_cast<const BinaryExpression &>(expr);
    return (b.left && yieldExpressionContainsYield(*b.left)) ||
           (b.right && yieldExpressionContainsYield(*b.right));
  }

  case NodeType::UnaryExpression: {
    const auto &u = static_cast<const UnaryExpression &>(expr);
    return u.operand && yieldExpressionContainsYield(*u.operand);
  }

  case NodeType::CallExpression: {
    const auto &c = static_cast<const CallExpression &>(expr);
    if (c.callee && yieldExpressionContainsYield(*c.callee)) {
      return true;
    }
    for (const auto &a : c.args) {
      if (a && yieldExpressionContainsYield(*a)) {
        return true;
      }
    }
    return false;
  }

  case NodeType::MemberExpression: {
    const auto &m = static_cast<const MemberExpression &>(expr);
    return m.object && yieldExpressionContainsYield(*m.object);
  }

  case NodeType::IndexExpression: {
    const auto &i = static_cast<const IndexExpression &>(expr);
    return (i.object && yieldExpressionContainsYield(*i.object)) ||
           (i.index && yieldExpressionContainsYield(*i.index));
  }

  case NodeType::TryExpression:
    // Conservative over-approximation, preserved from the original walk.
    return true;

  default:
    // Lambdas and every other node kind: no yield inherited from here.
    return false;
  }
}

} // namespace detail

// Public predicate, for callers that want the answer without a node to write.
inline bool statementContainsYield(const Statement &stmt) {
  return detail::yieldStatementContainsYield(stmt);
}

inline bool expressionContainsYield(const Expression &expr) {
  return detail::yieldExpressionContainsYield(expr);
}

// Walk a function body once.
inline bool bodyContainsYield(const BlockStatement &body) {
  for (const auto &s : body.body) {
    if (s && detail::yieldStatementContainsYield(*s)) {
      return true;
    }
  }
  return false;
}

// Annotate a lambda's `contains_yield` from its body. The body may be a
// BlockStatement or an ExpressionStatement, so this takes a Statement and
// routes through the statement walk.
inline void setLambdaContainsYield(LambdaExpression &lambda) {
  if (lambda.body) {
    lambda.contains_yield = detail::yieldStatementContainsYield(*lambda.body);
  }
}

} // namespace havel::ast