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

// Same, but also requires the diagnostic to name the real problem. `catch` and
// `finally` already had a correct failAt in the keyword switch, so asserting
// only "some error" would pass even while the guard swallowed the token and a
// misleading error surfaced instead.
bool rejectsWith(const std::string &source, const std::string &needle) {
  Parser parser;
  try {
    parser.parseStrict(source);
  } catch (const ParseError &e) {
    return std::string(e.what()).find(needle) != std::string::npos;
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

// `elif` is sugar for `else if`, so it is only valid directly after an if body.
// An `elif` at statement start is the same class of mistake as an orphan
// `else`, and used to surface much later as "Unresolved identifier 'elif'".
TEST(ParserOrphanElse, OrphanElifAfterClosedBlockIsRejected) {
  const std::string src = "fn t(h) {\n"
                          "    print \"A\"\n"
                          "    if h\n"
                          "        print \"B\"\n"
                          "}\n"
                          "elif h\n"
                          "    print \"C\"\n";
  EXPECT_TRUE(rejects(src));
}

TEST(ParserOrphanElse, OrphanElifAtTopLevelIsRejected) {
  EXPECT_TRUE(rejects("print \"A\"\nelif x > 3 {\n    print \"B\"\n}\n"));
}

// `elif` is documented to stay a plain name outside an if body. These forms
// must keep parsing, so the guard cannot just reserve the identifier.
TEST(ParserOrphanElse, ElifRemainsUsableAsAPlainVariable) {
  EXPECT_FALSE(rejects("elif = 5\n"));
  EXPECT_FALSE(rejects("elif = 5\nprint elif\n"));
  EXPECT_FALSE(rejects("x = 1\nprint x + elif\n"));
}

// `elif` directly after an if body is the supported sugar form.
TEST(ParserOrphanElse, ElifSugarAfterIfBodyStillParses) {
  const std::string src = "x = 5\n"
                          "if x > 10 {\n"
                          "    print \"huge\"\n"
                          "} elif x > 3 {\n"
                          "    print \"big\"\n"
                          "} else {\n"
                          "    print \"small\"\n"
                          "}\n";
  EXPECT_FALSE(rejects(src));
}

// Chained elifs inside a function body, plus elif with no trailing else.
TEST(ParserOrphanElse, ChainedElifInsideFunctionStillParses) {
  const std::string src = "fn check(n) {\n"
                          "    if n > 3 {\n"
                          "        1\n"
                          "    } elif n > 1 {\n"
                          "        2\n"
                          "    }\n"
                          "    0\n"
                          "}\n";
  EXPECT_FALSE(rejects(src));
}

// `catch`/`finally` belong to parseTryStatement. Reaching either as a
// statement meant no `try` owned them: the token was dropped silently and the
// mistake resurfaced as "Expected '=' or ':' after key".
TEST(ParserOrphanElse, OrphanCatchAtTopLevelIsRejected) {
  EXPECT_TRUE(rejectsWith("catch e {\n    print \"c\"\n}\n",
                          "can only appear within a 'try'"));
}

TEST(ParserOrphanElse, OrphanCatchInsideFunctionIsRejected) {
  const std::string src = "fn t() {\n"
                          "    catch e {\n"
                          "        print \"c\"\n"
                          "    }\n"
                          "}\n";
  EXPECT_TRUE(rejectsWith(src, "can only appear within a 'try'"));
}

TEST(ParserOrphanElse, OrphanFinallyInsideFunctionIsRejected) {
  const std::string src = "fn t() {\n"
                          "    finally {\n"
                          "        print \"f\"\n"
                          "    }\n"
                          "}\n";
  EXPECT_TRUE(rejectsWith(src, "can only appear within a 'try'"));
}

// try/catch/finally is the construct that owns those keywords, in both the
// bare and parenthesised catch-variable forms.
TEST(ParserOrphanElse, TryCatchFinallyFormsStillParse) {
  EXPECT_FALSE(rejects("fn t() {\n"
                       "    try {\n"
                       "        print \"body\"\n"
                       "    } catch e {\n"
                       "        print \"caught\"\n"
                       "    } finally {\n"
                       "        print \"finally\"\n"
                       "    }\n"
                       "}\n"));
  EXPECT_FALSE(rejects("fn t() {\n"
                       "    try {\n"
                       "        print \"body\"\n"
                       "    } catch (e) {\n"
                       "        print \"caught\"\n"
                       "    }\n"
                       "}\n"));
  EXPECT_FALSE(rejects("fn t() {\n"
                       "    try {\n"
                       "        print \"body\"\n"
                       "    } finally {\n"
                       "        print \"finally\"\n"
                       "    }\n"
                       "}\n"));
}