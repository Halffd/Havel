// Lexer.hpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>

namespace havel {

    enum class TokenType {
        Let,
        If,
        Else,
        Identifier,
        Number,
        String,
        Hotkey,
        Equals,
        Arrow,
        BinaryOp,
        OpenParen,
        CloseParen,
        OpenBrace,
        CloseBrace,
        Dot,
        Comma,
        Semicolon,
        Pipe,
        Comment,
        NewLine,
        EOF_TOKEN
    };

    struct Token {
        std::string value;
        TokenType type;
        std::string raw;
        size_t line;
        size_t column;

        Token(const std::string& value, TokenType type, const std::string& raw, size_t line, size_t column)
            : value(value), type(type), raw(raw), line(line), column(column) {}

        std::string toString() const {
            return "Token(type=" + std::to_string(static_cast<int>(type)) +
                   ", value=\"" + value + "\", raw=\"" + raw + "\", line=" +
                   std::to_string(line) + ", column=" + std::to_string(column) + ")";
        }
    };

    class Lexer {
    public:
        Lexer(const std::string& sourceCode);
        std::vector<Token> tokenize();
        void printTokens(const std::vector<Token>& tokens) const;

    private:
        std::string source;
        size_t position = 0;
        size_t line = 1;
        size_t column = 1;

        static const std::unordered_map<std::string, TokenType> KEYWORDS;
        static const std::unordered_map<char, TokenType> SINGLE_CHAR_TOKENS;

        bool isAtEnd() const;
        char peek(size_t offset = 0) const;
        char advance();

        bool isAlpha(char c) const;
        bool isDigit(char c) const;
        bool isAlphaNumeric(char c) const;
        bool isSkippable(char c) const;
        bool isHotkeyChar(char c) const;

        void skipWhitespace();
        void skipComment();

        Token makeToken(const std::string& value, TokenType type, const std::string& raw = "");

        Token scanNumber();
        Token scanString();
        Token scanIdentifier();
        Token scanHotkey();
    };

} // namespace havel
