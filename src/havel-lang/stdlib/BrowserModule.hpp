#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "havel-lang/runtime/concurrency/Scheduler.hpp"

namespace havel::stdlib {

void registerBrowserModule(havel::compiler::VMApi &api);

} // namespace havel::stdlib
