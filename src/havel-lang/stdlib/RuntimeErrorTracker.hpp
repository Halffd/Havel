#pragma once

#include "../common/Export.hpp"
#include <string>
#include <vector>
#include <cstddef>

namespace havel::stdlib {

HAVEL_EXPORT size_t runtimeErrorCount();
HAVEL_EXPORT const std::vector<std::string> &runtimeErrorsList();
HAVEL_EXPORT void notifyRuntimeError(const std::string &msg);

}
