#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace havel::stdlib {

void registerTypeModule(compiler::VMApi &api);

} // namespace havel::stdlib
