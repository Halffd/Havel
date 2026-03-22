/*
 * MemberResolver.hpp
 *
 * Member access resolution for Havel runtime.
 * Extracted from ExprEvaluator to reduce complexity.
 */
#pragma once

#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>

// Forward declarations only - implementation includes full headers
namespace havel {

class Interpreter;
struct HavelValue;
struct HavelFunction;

namespace ast {
    struct MemberExpression;
}

/**
 * MemberResolver - Resolve member access
 * 
 * Handles:
 * - Object property lookup
 * - Array length and methods
 * - String methods
 * - Struct field access and method binding
 * - Trait method resolution
 */
class MemberResolver {
public:
    MemberResolver(Interpreter* interp);
    ~MemberResolver() = default;

    void resolveMember(const ast::MemberExpression& node);

private:
    Interpreter* interpreter;
};

} // namespace havel
