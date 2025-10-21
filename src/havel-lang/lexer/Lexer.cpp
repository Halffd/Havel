#include "Lexer.hpp"
#include <regex>
#include <iostream>

namespace havel {

// Static member definitions
const std::unordered_map<std::string, TokenType> Lexer::KEYWORDS = {
    {"let", TokenType::Let},
    {"if", TokenType::If},
    {"else", TokenType::Else},
    {"fn", TokenType::Fn},
    {"return", TokenType::Return},
    {"send", TokenType::Identifier},      // Built-in function
    {"clipboard", TokenType::Identifier}, // Built-in module
    {"text", TokenType::Identifier},      // Built-in module
    {"window", TokenType::Identifier},    // Built-in module
    {"import", TokenType::Import},
    {"from", TokenType::From},
    {"as", TokenType::As}
};

const std::unordered_map<char, TokenType> Lexer::SINGLE_CHAR_TOKENS = {
    {'(', TokenType::OpenParen},
    {')', TokenType::CloseParen},
    {'{', TokenType::OpenBrace},
    {'}', TokenType::CloseBrace},
    {'.', TokenType::Dot},
    {',', TokenType::Comma},
    {';', TokenType::Semicolon},
    {'|', TokenType::Pipe},
    {'+', TokenType::Plus},
    {'-', TokenType::Minus},
    {'*', TokenType::Multiply},
    {'/', TokenType::Divide},
    {'%', TokenType::Modulo},
    {'\n', TokenType::NewLine}
};

Lexer::Lexer(const std::string& sourceCode) : source(sourceCode) {}

char Lexer::peek(size_t offset) const {
    size_t pos = position + offset;
    if (pos >= source.length()) return '\0';
    return source[pos];
}

char Lexer::advance() {
    if (isAtEnd()) return '\0';
    
    char current = source[position++];
    
    if (current == '\n') {
        line++;
        column = 1;
    } else {
        column++;
    }
    
    return current;
}

bool Lexer::isAtEnd() const {
    return position >= source.length();
}

bool Lexer::isAlpha(char c) const {
    return std::isalpha(c) || c == '_';
}

bool Lexer::isDigit(char c) const {
    return std::isdigit(c);
}

bool Lexer::isAlphaNumeric(char c) const {
    return isAlpha(c) || isDigit(c);
}

bool Lexer::isSkippable(char c) const {
    return c == ' ' || c == '\t' || c == '\r';
}

bool Lexer::isHotkeyChar(char c) const {
    return isAlphaNumeric(c) || c == '+' || c == '-' || c == '^' || c == '!' || c == '#';
}

Token Lexer::makeToken(const std::string& value, TokenType type, const std::string& raw) {
    return Token(value, type, raw.empty() ? value : raw, line, column - value.length());
}

void Lexer::skipWhitespace() {
    while (!isAtEnd() && isSkippable(peek())) {
        advance();
    }
}

void Lexer::skipComment() {
    // Single line comment //
    if (peek() == '/' && peek(1) == '/') {
        while (!isAtEnd() && peek() != '\n') {
            advance();
        }
    }
    // Multi-line comment /* */
    else if (peek() == '/' && peek(1) == '*') {
        advance(); // /
        advance(); // *
        
        while (!isAtEnd()) {
            if (peek() == '*' && peek(1) == '/') {
                advance(); // *
                advance(); // /
                break;
            }
            advance();
        }
    }
}

Token Lexer::scanNumber() {
    size_t start = position - 1; // Include the first digit we already consumed
    std::string number;
    number += source[start];
    
    // Handle negative numbers
    bool isNegative = (start > 0 && source[start - 1] == '-');
    if (isNegative) {
        number = "-" + number;
        start--;
    }
    
    // Scan integer part
    while (!isAtEnd() && isDigit(peek())) {
        number += advance();
    }
    
    // Scan fractional part
    if (!isAtEnd() && peek() == '.' && isDigit(peek(1))) {
        number += advance(); // consume '.'
        while (!isAtEnd() && isDigit(peek())) {
            number += advance();
        }
    }
    
    return makeToken(number, TokenType::Number);
}

Token Lexer::scanString() {
    std::string value;
    std::string raw;
    
    // Skip opening quote
    char quote = source[position - 1];
    
    while (!isAtEnd() && peek() != quote) {
        char c = peek();
        raw += c;
        
        if (c == '\\' && !isAtEnd()) {
            advance(); // consume backslash
            raw += peek();
            
            char escaped = advance();
            switch (escaped) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case 'r': value += '\r'; break;
                case '\\': value += '\\'; break;
                case '"': value += '"'; break;
                case '\'': value += '\''; break;
                default: 
                    value += '\\';
                    value += escaped;
                    break;
            }
        } else {
            value += advance();
        }
    }
    
    if (isAtEnd()) {
        throw std::runtime_error("Unterminated string at line " + std::to_string(line));
    }
    
    // Consume closing quote
    advance();
    
    return makeToken(value, TokenType::String, raw);
}

Token Lexer::scanIdentifier() {
    std::string identifier;
    
    // First character (already consumed)
    identifier += source[position - 1];
    
    // Subsequent characters
    while (!isAtEnd() && isAlphaNumeric(peek())) {
        identifier += advance();
    }
    
    // Check if it's a keyword
    auto keywordIt = KEYWORDS.find(identifier);
    TokenType type = (keywordIt != KEYWORDS.end()) ? keywordIt->second : TokenType::Identifier;
    
    return makeToken(identifier, type);
}

Token Lexer::scanHotkey() {
    std::string hotkey;

    // Include the already consumed character
    hotkey += source[position - 1];

    // Continue consuming characters that are part of a hotkey until a terminator
    while (!isAtEnd()) {
        char c = peek();
        // Stop at whitespace or special characters that end hotkeys or start other tokens
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '=' || c == '{' || c == '(' || c == '|' ) {
            break;
        }
        if (!isHotkeyChar(c)) break;
        hotkey += advance();
    }

    // Special handling for plain F-keys (F1..F12)
    if (hotkey.size() >= 2 && hotkey[0] == 'F') {
        bool allDigits = true;
        for (size_t i = 1; i < hotkey.size(); ++i) allDigits &= std::isdigit(static_cast<unsigned char>(hotkey[i]));
        if (allDigits) {
            try {
                int fnum = std::stoi(hotkey.substr(1));
                if (fnum >= 1 && fnum <= 12) return makeToken(hotkey, TokenType::Hotkey);
            } catch (...) {}
        }
    }

    // Accept raw modifier-based forms like ^+!F12 as Hotkey
    if (!hotkey.empty() && (hotkey.find('^') != std::string::npos || hotkey.find('!') != std::string::npos || hotkey.find('+') != std::string::npos || hotkey[0] == 'F')) {
        return makeToken(hotkey, TokenType::Hotkey);
    }

    // Fallback: not a recognizable hotkey, rewind and treat as identifier
    position -= (hotkey.size() - 1);
    return scanIdentifier();
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    
    while (!isAtEnd()) {
        skipWhitespace();
        
        if (isAtEnd()) break;
        
        char c = advance();
        
        // Handle comments
        if (c == '/' && (peek() == '/' || peek() == '*')) {
            position--; // Put back the '/'
            advance(); // Re-consume it
            skipComment();
            continue;
        }
        
        // Handle numbers (including negative numbers in certain contexts)
        if (isDigit(c) || (c == '-' && isDigit(peek()))) {
            tokens.push_back(scanNumber());
            continue;
        }
        
        // Handle strings
        if (c == '"' || c == '\'') {
            tokens.push_back(scanString());
            continue;
        }
        
        // Handle arrow operator =>
        if (c == '=' && peek() == '>') {
            advance(); // consume '>'
            tokens.push_back(makeToken("=>", TokenType::Arrow));
            continue;
        }
        
        // Handle equals
        if (c == '=') {
            tokens.push_back(makeToken("=", TokenType::Equals));
            continue;
        }
        
        // Handle single character tokens
        auto singleCharIt = SINGLE_CHAR_TOKENS.find(c);
        if (singleCharIt != SINGLE_CHAR_TOKENS.end()) {
            tokens.push_back(makeToken(std::string(1, c), singleCharIt->second));
            continue;
        }
        
// Handle identifiers and potential hotkeys
        if (isAlpha(c) || c == 'F') {
            // Check if this might be a hotkey starting with F
            if (c == 'F' && isDigit(peek())) {
                tokens.push_back(scanHotkey());
            } else {
                tokens.push_back(scanIdentifier());
            }
            continue;
        }
        
        // Handle modifier-based hotkeys starting with special characters like ^ + ! #
        if (c == '^' || c == '!' || c == '+' || c == '#') {
            tokens.push_back(scanHotkey());
            continue;
        }

        // Handle unrecognized characters
        throw std::runtime_error("Unrecognized character '" + std::string(1, c) +
                                "' at line " + std::to_string(line) +
                                ", column " + std::to_string(column));
    }

    // Add EOF token
    tokens.push_back(makeToken("EndOfFile", TokenType::EOF_TOKEN));

    return tokens;
}

void Lexer::printTokens(const std::vector<Token>& tokens) const {
    std::cout << "=== HAVEL TOKENS ===" << std::endl;
    for (size_t i = 0; i < tokens.size(); ++i) {
        std::cout << "[" << i << "] " << tokens[i].toString() << std::endl;
    }
    std::cout << "===================" << std::endl;
}

} // namespace havel