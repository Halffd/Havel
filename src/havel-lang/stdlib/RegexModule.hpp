#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

#include <regex>

namespace havel::stdlib {

void registerRegexModule(const compiler::VMApi &api);

} // namespace havel::stdlib
