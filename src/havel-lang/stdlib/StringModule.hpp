#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace havel::stdlib {

void registerStringModule(havel::compiler::VMApi &api);

} // namespace havel::stdlib
