/*
 * FileSystemModule.hpp
 *
 * File system module for Havel VM
 * Provides file and directory operations using the new VMApi system.
 */
#pragma once

#include "../compiler/bytecode/VMApi.hpp"

namespace havel::stdlib {

/**
 * Register file system module functions with VMApi
 */
void registerFileSystemModule(havel::compiler::VMApi &api);

} // namespace havel::stdlib
