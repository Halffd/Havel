// Regression: an `else` orphaned by an unbraced if-body must be a parse error.
//
// Bug: when an `if` body is a single unbraced statement, the enclosing block's
// `}` is reached before `else`. parseStatement()/parseInlineStatement() matched
// TokenType::Else and returned nullptr *silently*, so the else body was
// mis-compiled and ran BEFORE the statements it should have followed.
//
//   fn t(h) {
//       print "A"
//       if h
//           print "B"
//     } else {
//       print "C"     <-- executed before "A"
//   }
//
// The observable symptom was a hotkey handler whose else branch ran before the
// handler's own setup, so the brightness call never reached the host.
//
// Parser-only: no VM, no host, no I/O.
#include "parser/Parser.h"

#include <gtest/gtest.h>

using havel::parser::ParseError;
using havel::parser::Parser;

namespace {
// Parses and returns true if a ParseError was raised, false if it parsed clean.
bool rejects(const std::string &source) {
  Parser parser;
  try {
    parser.parseStrict(source);
  } catch (const ParseError &) {
    return true;
  }
  return false;
}
} // namespace

// The exact broken shape: inline if-body, then the enclosing `}`, then `else`.
// Must be rejected rather than silently reordered.
TEST(ParserOrphanElse, InlineIfBodyThenClosingBraceThenElse) {
  const std::string src = "fn t(h) {\n"
                          "    print \"A\"\n"
                          "    if h\n"
                          "        print \"B\"\n"
                          "  } else {\n"
                          "    print \"C\"\n"
                          "  }\n"
                          "}\n";
  EXPECT_TRUE(rejects(src)) << "orphaned else was silently mis-compiled";
}

// Same defect reached at a different indent: `if` outdented relative to the
// closing brace. Indentation must not change the accept/reject decision.
TEST(ParserOrphanElse, OutdentedIfThenElse) {
  const std::string src = "fn t(h) {\n"
                          "    print \"A\"\n"
                          " if h\n"
                          "    print \"B\"\n"
                          "  } else {\n"
                          "    print \"C\"\n"
                          "  }\n"
                          "}\n";
  EXPECT_TRUE(rejects(src));
}

// A braced if body is the documented fix; it must keep parsing.
TEST(ParserOrphanElse, BracedIfBodyWithElseStillParses) {
  const std::string src = "fn t(h) {\n"
                          "    print \"A\"\n"
                          "    if h { print \"B\" } else { print \"C\" }\n"
                          "}\n";
  EXPECT_FALSE(rejects(src)) << "braced if/else must remain valid";
}

// `else` with no preceding `}` must still attach to the inline if-body.
TEST(ParserOrphanElse, InlineIfBodyWithAdjacentElseStillParses) {
  const std::string src = "fn t(h) {\n"
                          "    print \"A\"\n"
                          "    if h\n"
                          "        print \"B\"\n"
                          "    else\n"
                          "        print \"C\"\n"
                          "}\n";
  EXPECT_FALSE(rejects(src));
}

// else-if chains must keep parsing (the `elif`/nested-if paths).
TEST(ParserOrphanElse, ElseIfChainStillParses) {
  const std::string src = "fn t(x) {\n"
                          "    if x > 10\n"
                          "        print \"big\"\n"
                          "    else if x > 5\n"
                          "        print \"mid\"\n"
                          "    else\n"
                          "        print \"small\"\n"
                          "}\n";
  EXPECT_FALSE(rejects(src));
}

// catch/finally share the guard this fix touched; they must stay lenient.
TEST(ParserOrphanElse, CatchAndFinallyStillParse) {
  const std::string src = "fn t() {\n"
                          "    try {\n"
                          "        print \"body\"\n"
                          "    } catch e {\n"
                          "        print \"caught\"\n"
                          "    } finally {\n"
                          "        print \"finally\"\n"
                          "    }\n"
                          "}\n";
  EXPECT_FALSE(rejects(src));
}