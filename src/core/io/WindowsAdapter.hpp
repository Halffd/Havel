#pragma once

#include "InputBackend.hpp"

namespace havel {

std::unique_ptr<InputBackend> CreateWindowsAdapter();

}
