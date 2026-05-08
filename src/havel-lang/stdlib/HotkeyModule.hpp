#pragma once

#include "havel-lang/compiler/vm/VMApi.hpp"
#include <string>

namespace havel::stdlib {

class HotkeyModule {
public:
static void install(havel::compiler::VMApi &api);

static havel::compiler::Value
createHotkeyContext(havel::compiler::VM *vm, const std::string &hotkeyId,
const std::string &alias, const std::string &key,
const std::string &condition, const std::string &info,
havel::compiler::CallbackId callback);
};

void registerHotkeyModule(havel::compiler::VMApi &api);

} // namespace havel::stdlib
