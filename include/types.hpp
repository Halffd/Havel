#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

// Type aliases
using str = std::string;
using cstr = const std::string&;
using wID = unsigned long;
using pID = unsigned long;

// Group type definition
using group = std::unordered_map<std::string, std::vector<std::string>>;

// Common typedefs for key handling
#ifndef Key_typedef
#define Key_typedef
typedef unsigned long Key;
#endif