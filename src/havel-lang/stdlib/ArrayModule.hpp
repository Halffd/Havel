#pragma once

namespace havel::compiler {
struct VMApi;
}

namespace havel::stdlib {

void registerArrayModule(const compiler::VMApi &api);

} // namespace havel::stdlib
