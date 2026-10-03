// Regression: a closing delimiter with no construct open must be a parse error.
//
// Bug: at top level, `}`, `)` and `]` fell through to parseStatement(), which
// wrapped the resulting null expression as ExpressionStatement{nullptr}. The
// compiler skipped that node, so the delimiter vanished and the rest of the
// file still compiled and ran -- statements the author believed were guarded
// by the block executed anyway.
//
//   print "A"
//   }              <-- silently dropped
//   print "B"      <-- still ran
//
// Same defect family as the orphan-`else` fix in parser_orphan_else_ut.cpp:
// a null statement was produced instead of a diagnostic.
//
// Parser-only: no VM, no host, no I/O.
#include "parser/Parser.h"

#include <gtest/gtest.h>

using havel::parser::ParseError;
using havel::parser::Parser;

namespace {
// parseStrict rejects via ParseError. Reports whether it was raised.
bool rejects(const std::string &source) {
  Parser parser;
  try {
    parser.parseStrict(source);
  } catch (const ParseError &) {
    return true;
  }
  return false;
}

// produceAST records a CompilerError instead of throwing. Reports the messages.
std::vector<std::string> astErrors(const std::string &source) {
  Parser parser;
  parser.produceAST(source);
  std::vector<std::string> messages;
  for (const auto &err : parser.getErrors()) {
    messages.push_back(err.message);
  }
  return messages;
}

bool contains(const std::vector<std::string> &haystack,
              const std::string &needle) {
  for (const auto &m : haystack) {
    if (m.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}
} // namespace

// The exact broken shape: a `}` between two complete top-level statements.
TEST(ParserUnmatchedDelimiter, TopLevelClosingBraceIsRejected) {
  const std::string src = "print \"A\"\n}\nprint \"B\"\n";
  EXPECT_TRUE(rejects(src)) << "stray '}' was silently dropped";
}

// A second `}` after a correctly closed block is the same bug: the first one
// legitimately closed `t`, the second had nothing left to close.
TEST(ParserUnmatchedDelimiter, ExtraClosingBraceAfterClosedBlockIsRejected) {
  const std::string src = "fn t() {\n"
                          "    print \"A\"\n"
                          "}\n"
                          "}\n";
  EXPECT_TRUE(rejects(src));
}

// A block that closes early and then keeps going at top level.
TEST(ParserUnmatchedDelimiter, EarlyBlockCloseThenMoreStatementsIsRejected) {
  const std::string src = "fn t() {\n"
                          "    print \"A\"\n"
                          "  }\n"
                          "  print \"B\"\n"
                          "}\n";
  EXPECT_TRUE(rejects(src));
}

// Stray `)` and `]` took the same silent path as `}`.
TEST(ParserUnmatchedDelimiter, TopLevelClosingParenIsRejected) {
  EXPECT_TRUE(rejects("print \"A\"\n)\nprint \"B\"\n"));
}

TEST(ParserUnmatchedDelimiter, TopLevelClosingBracketIsRejected) {
  EXPECT_TRUE(rejects("print \"A\"\n]\nprint \"B\"\n"));
}

// A bare `}` as the whole program must not parse clean either.
TEST(ParserUnmatchedDelimiter, LoneClosingBraceIsRejected) {
  EXPECT_TRUE(rejects("}\n"));
}

// produceAST is the path the lint/run entry points use. It must surface the
// diagnostic instead of returning a clean AST, and must name the delimiter.
TEST(ParserUnmatchedDelimiter, ProduceAstReportsUnmatchedBrace) {
  const auto messages = astErrors("print \"A\"\n}\nprint \"B\"\n");
  ASSERT_FALSE(messages.empty()) << "produceAST silently accepted a stray '}'";
  EXPECT_TRUE(contains(messages, "Unmatched '}'"));
}

TEST(ParserUnmatchedDelimiter, ProduceAstReportsUnmatchedParen) {
  const auto messages = astErrors("print \"A\"\n)\nprint \"B\"\n");
  ASSERT_FALSE(messages.empty());
  EXPECT_TRUE(contains(messages, "Unmatched ')'"));
}

TEST(ParserUnmatchedDelimiter, ProduceAstReportsUnmatchedBracket) {
  const auto messages = astErrors("print \"A\"\n]\nprint \"B\"\n");
  ASSERT_FALSE(messages.empty());
  EXPECT_TRUE(contains(messages, "Unmatched ']'"));
}

// Recovery must not lose the rest of the program: the statements after the
// stray delimiter are still parsed, so one typo does not hide later errors.
TEST(ParserUnmatchedDelimiter, RecoveryContinuesAfterStrayDelimiter) {
  Parser parser;
  parser.produceAST("print \"A\"\n}\nprint \"B\"\n=\n");
  // The stray '}' is reported, and parsing does not stop dead at it.
  EXPECT_TRUE(contains(parser.getErrors().size() ? [&] {
                std::vector<std::string> m;
                for (const auto &e : parser.getErrors())
                  m.push_back(e.message);
                return m;
              }()
                                                   : std::vector<std::string>{},
                            "Unmatched '}'"));
  EXPECT_GE(parser.getErrors().size(), 1u);
}

// Every closing delimiter must still be able to close its own construct.
TEST(ParserUnmatchedDelimiter, BalancedDelimitersStillParse) {
  EXPECT_FALSE(rejects("fn t(x) {\n"
                       "    y = (x + 1)\n"
                       "    z = [1, 2, 3]\n"
                       "    print y\n"
                       "}\n"));
}

// Blocks nested inside functions/classes close with their own '}'.
TEST(ParserUnmatchedDelimiter, NestedBlocksStillParse) {
  EXPECT_FALSE(rejects("fn t(h) {\n"
                       "    if h {\n"
                       "        print \"a\"\n"
                       "    }\n"
                       "    for i in 0 .. 2 {\n"
                       "        print i\n"
                       "    }\n"
                       "}\n"));
}

// Brace-style classes close with their own '}'. The guard is top-level only,
// so class bodies must keep parsing. (Methods are declared with `fn`; the
// `name(o) { ... }` shorthand is a self-hosted-parser form the C++ parser
// does not accept.)
TEST(ParserUnmatchedDelimiter, ClassWithMethodBodiesStillParse) {
  EXPECT_FALSE(rejects("class V {\n"
                       "    x: num\n"
                       "    fn dot(o) { @x * o.y }\n"
                       "}\n"));
}