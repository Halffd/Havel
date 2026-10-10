// IO::ParseKeyString - the pure tokenizer behind io.send's key syntax.
//
// Tests the {word} brace forms: the fromBraces flag the sender uses to fall
// back to literal text for unknown names ({string}), known keys, shorthand
// modifiers, and the modifier/special forms. The tokenizer is a pure
// string -> tokens mapping, so the silent-swallow regression (an unknown
// {word} dropped from the sent text instead of typed as literal characters)
// is testable without an IO backend or a live session.
#include "core/io/IO.hpp"

#include <gtest/gtest.h>

using havel::IO;
using havel::KeyToken;

TEST(ParseKeyString, KnownKeyWordIsKeyTokenFromBraces) {
  auto tokens = IO::ParseKeyString("{home}");
  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].type, KeyToken::Key);
  EXPECT_EQ(tokens[0].value, "home");
  EXPECT_TRUE(tokens[0].fromBraces);
}

TEST(ParseKeyString, UnknownKeyWordCarriesFromBracesForLiteralFallback) {
  // {string} resolves to no key; the sender must see fromBraces and type the
  // literal "{string}" text instead of silently dropping the token.
  auto tokens = IO::ParseKeyString("{string}");
  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].type, KeyToken::Key);
  EXPECT_EQ(tokens[0].value, "string");
  EXPECT_TRUE(tokens[0].fromBraces);
}

TEST(ParseKeyString, PlainCharactersAreNotFromBraces) {
  auto tokens = IO::ParseKeyString("abc");
  ASSERT_EQ(tokens.size(), 3u);
  for (const auto &t : tokens) {
    EXPECT_EQ(t.type, KeyToken::Key);
    EXPECT_FALSE(t.fromBraces);
  }
}

TEST(ParseKeyString, ShorthandModifierPrefixesKey) {
  auto tokens = IO::ParseKeyString("^{Down}");
  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].type, KeyToken::Modifier);
  EXPECT_EQ(tokens[0].value, "ctrl");
  EXPECT_EQ(tokens[1].type, KeyToken::Key);
  EXPECT_EQ(tokens[1].value, "down"); // {word} forms are lowercased
  EXPECT_TRUE(tokens[1].fromBraces);
}

TEST(ParseKeyString, ModifierDownForm) {
  auto tokens = IO::ParseKeyString("{ctrl down}");
  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].type, KeyToken::ModifierDown);
  EXPECT_EQ(tokens[0].value, "ctrl");
  EXPECT_FALSE(tokens[0].fromBraces);
}

TEST(ParseKeyString, SpecialFormsStaySpecial) {
  auto tokens = IO::ParseKeyString("{emergency_release}");
  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].type, KeyToken::Special);
  EXPECT_EQ(tokens[0].value, "emergency_release");
}
