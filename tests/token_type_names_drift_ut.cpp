// Drift guard: TokenType <-> TOKEN_TYPE_NAMES <-> lexer.hv TID map.
//
// _nativeTokenize (VMHostFunctions.cpp) converts each C++ token into a
// {type, value, line, col} object using TOKEN_TYPE_NAMES, falling back to
// the string "Unknown" when the TokenType has no row. tokenize() in
// modules/lang/lexer.hv then tags every token with TID[t.type] -- a name
// missing from the TID map yields tid=null, which reaches the Pratt
// parser's binding-power lookup as a null object key and kills the parse.
// That is exactly the `emit` regression: TokenType::Emit shipped with the
// lexer and parser but no table row, so `emit ...` in any script broke
// --format/--lint with type=Unknown tid=null.
//
// Three hand-maintained lists must stay in sync; this test pins all pairs:
//   1. every TokenType enum member has a TOKEN_TYPE_NAMES row,
//   2. every TOKEN_TYPE_NAMES name is a key in the TID map in
//      modules/lang/lexer.hv (so tokenize() can give tokens a tid).
// AGENTS.md rule 11: a hardcoded name list must ship with an automated
// drift check; this is that check.
#include <gtest/gtest.h>

#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

// LEXER_HV_PATH is defined by CMake as an absolute path so the test works
// from any working directory (add_test runs without a defined cwd).
#ifndef LEXER_HV_PATH
#error "LEXER_HV_PATH must be defined by CMake (-DLEXER_HV_PATH=...)"
#endif

#include "lexer/Lexer.hpp"
#include "stdlib/TokenTypeNames.hpp"

namespace {

// The TID map block from modules/lang/lexer.hv ("TID = {" up to the closing
// brace): the self-hosted parser's integer token-type ids, keyed by the
// string names TOKEN_TYPE_NAMES produces.
std::string loadTidBlock() {
  std::ifstream in(LEXER_HV_PATH);
  if (!in)
    throw std::runtime_error("cannot open " + std::string(LEXER_HV_PATH));
  std::stringstream buf;
  buf << in.rdbuf();
  std::string src = buf.str();
  auto tid = src.find("TID = {");
  if (tid == std::string::npos)
    throw std::runtime_error("TID map not found in lexer.hv");
  auto end = src.find("\n}", tid);
  if (end == std::string::npos)
    throw std::runtime_error("TID map end not found in lexer.hv");
  return src.substr(tid, end - tid);
}

} // namespace

// Every TokenType member must have a TOKEN_TYPE_NAMES row. The enum is
// sequential (no explicit values), so iterating 0..EOF_TOKEN visits every
// member; a member without a row fails here instead of silently producing
// "Unknown" tokens at runtime.
TEST(TokenTypeNamesDrift, EnumCoversTable) {
  const int last = static_cast<int>(havel::TokenType::EOF_TOKEN);
  for (int t = 0; t <= last; ++t) {
    auto type = static_cast<havel::TokenType>(t);
    EXPECT_NE(havel::stdlib::TOKEN_TYPE_NAMES.count(type), 0u)
        << "TokenType value " << t
        << " has no TOKEN_TYPE_NAMES row (src/havel-lang/stdlib/TokenTypeNames.hpp)";
  }
}

// Every name TOKEN_TYPE_NAMES produces must be a key in the lexer.hv TID
// map, or tokenize() tags real tokens with tid=null and the parser's
// binding-power lookup degrades. Word-boundary regex so "In" does not
// match inside "This:" etc.
TEST(TokenTypeNamesDrift, TableNamesCoverHvTidMap) {
  const std::string tidBlock = loadTidBlock();
  for (const auto &kv : havel::stdlib::TOKEN_TYPE_NAMES) {
    std::regex re("\\b" + kv.second + ":");
    EXPECT_TRUE(std::regex_search(tidBlock, re))
        << "TID map in modules/lang/lexer.hv has no entry for token name \""
        << kv.second << "\"";
  }
}
