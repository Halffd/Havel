/*
 * CallDispatcher.hpp
 *
 * Function call dispatch for Havel runtime.
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
    struct CallExpression;
}

/**
 * CallDispatcher - Dispatch function calls
 */
class CallDispatcher {
public:
    CallDispatcher(Interpreter* interp);
    ~CallDispatcher() = default;

    void dispatchCall(const ast::CallExpression& node);

private:
    Interpreter* interpreter;
};

} // namespace havel
