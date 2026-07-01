#pragma once
#include <string>
#include <vector>
#include <cstddef>

namespace havel::stdlib {

size_t runtimeErrorCount();
const std::vector<std::string> &runtimeErrorsList();
void notifyRuntimeError(const std::string &msg);

}
