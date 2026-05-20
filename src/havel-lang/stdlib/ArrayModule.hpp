#pragma once

namespace havel::compiler {
class VMApi;
}

namespace havel::stdlib {

void registerArrayModule(const compiler::VMApi &api);

} // namespace havel::stdlib
