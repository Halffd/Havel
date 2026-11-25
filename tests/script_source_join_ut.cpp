// Regression tests for the script-source join rule.
//
// The bytecode cache keys an entry by the compile unit's PATH hash, and the
// serve path validates an entry against the exact text the request would
// compile. The launcher used to append a newline to EVERY file it read
// ("code += content + \"\\n\""), so for a single script the compiled text
// was one byte longer than the file on disk. The entry the compile wrote
// then described an identity no reader asked about: it was rejected on the
// next load ("source-size-differs"), rewritten, and the cache never served.
// Every launch recompiled.
//
// The newline is a SEPARATOR between files, so it belongs between them and
// not after the last one.
//
// These tests pin the rule the launcher uses.

#include "ScriptSourceJoin.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using havel::init::joinScriptContents;

// A single file must be compiled verbatim: the text the compiler sees is
// the text the cache entry will be stamped with, and the reader validates
// the entry against that same text. Any byte added here makes the entry
// unservable.
TEST(ScriptSourceJoin, SingleFileIsVerbatim) {
  const std::string content = "print(\"hello\")\n";
  EXPECT_EQ(joinScriptContents({content}), content)
      << "a single file must not gain a byte - the cache validates the "
         "entry against the exact text that was compiled";
}

// The same holds for a file WITHOUT a trailing newline: the join must not
// "helpfully" terminate the last line, because that byte changes the
// file's identity.
TEST(ScriptSourceJoin, SingleFileWithoutTrailingNewlineIsVerbatim) {
  const std::string content = "print(\"hello\")";
  EXPECT_EQ(joinScriptContents({content}), content);
}

// Several files are separated by exactly one newline between each pair,
// and the LAST file is not followed by one.
TEST(ScriptSourceJoin, SeveralFilesAreSeparatedByOneNewline) {
  const std::string a = "x = 1\n";
  const std::string b = "y = 2\n";
  const std::string c = "z = 3\n";
  EXPECT_EQ(joinScriptContents({a, b, c}), "x = 1\n\ny = 2\n\nz = 3\n");
}

// A file without a trailing newline is still separated from the next one,
// so the two files cannot run together into one line.
TEST(ScriptSourceJoin, SeparatorIsAddedEvenWithoutTrailingNewline) {
  EXPECT_EQ(joinScriptContents({"x = 1", "y = 2"}), "x = 1\ny = 2");
}

// No files join to nothing.
TEST(ScriptSourceJoin, NoFilesJoinToEmpty) {
  EXPECT_EQ(joinScriptContents({}), "");
}

} // namespace
