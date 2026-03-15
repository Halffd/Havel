/*
 * Environment.cpp
 *
 * Variable scoping and environment management.
 */
#include "Environment.hpp"

namespace havel {

void Environment::clear() {
    values.clear();
    constVars.clear();
    parent.reset();
}

} // namespace havel
