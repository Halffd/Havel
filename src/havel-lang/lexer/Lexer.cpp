// havel_lexer.cpp
#include "havel_lexer.hpp"
#include <regex>

namespace havel {

// Static member definitions
const std::unordered_map<std::string, TokenType> HavelLexer::KEYWORDS = {
    {"let", TokenType::Let},
    {"if", TokenType::If},
    {"else", TokenType::Else},
    {"send", TokenType::Identifier},      // Built-in function
    {"clipboard", TokenType::Identifier}, // Built-in module
    {"text", TokenType::Identifier},      // Built-in module
    {"window", TokenType::Identifier}     // Built-in module
};

const std::unordered_map<char, TokenType> HavelLexer::SINGLE_CHAR_TOKENS = {
    {'(', TokenType::OpenParen},
    {')', TokenType::CloseParen},
    {'{', TokenType::OpenBrace},
    {'}', TokenType::CloseBrace},
    {'.', TokenType::Dot},
    {',', TokenType::Comma},
    {';', TokenType::Semicolon},
    {'|', TokenType::Pipe},
    {'+', TokenType::BinaryOp},
    {'-', TokenType::BinaryOp},
    {'*', TokenType::BinaryOp},
    {'/', TokenType::BinaryOp},
    {'%', TokenType::BinaryOp},
    {'\n', TokenType::NewLine}
};

HavelLexer::HavelLexer(const std::string& sourceCode) : source(sourceCode) {}

char HavelLexer::peek(size_t offset) const {
    size_t pos = position + offset;
    if (pos >= source.length()) return '\0';
    return source[pos];
}

char HavelLexer::advance() {
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

bool HavelLexer::isAtEnd() const {
    return position >= source.length();
}

bool HavelLexer::isAlpha(char c) const {
    return std::isalpha(c) || c == '_';
}

bool HavelLexer::isDigit(char c) const {
    return std::isdigit(c);
}

bool HavelLexer::isAlphaNumeric(char c) const {
    return isAlpha(c) || isDigit(c);
}

bool HavelLexer::isSkippable(char c) const {
    return c == ' ' || c == '\t' || c == '\r';
}

bool HavelLexer::isHotkeyChar(char c) const {
    return isAlphaNumeric(c) || c == '+' || c == '-';
}

Token HavelLexer::makeToken(const std::string& value, TokenType type, const std::string& raw) {
    return Token(value, type, raw.empty() ? value : raw, line, column - value.length());
}

void HavelLexer::skipWhitespace() {
    while (!isAtEnd() && isSkippable(peek())) {
        advance();
    }
}

void HavelLexer::skipComment() {
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

Token HavelLexer::scanNumber() {
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

Token HavelLexer::scanString() {
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

Token HavelLexer::scanIdentifier() {
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

Token HavelLexer::scanHotkey() {
    std::string hotkey;
    
    // Add the character we already consumed
    hotkey += source[position - 1];
    
    // Scan for F1-F12 keys
    if (hotkey[0] == 'F' && !isAtEnd() && isDigit(peek())) {
        while (!isAtEnd() && isDigit(peek())) {
            hotkey += advance();
        }
        
        // Validate F-key range (F1-F12)
        int fkeyNum = std::stoi(hotkey.substr(1));
        if (fkeyNum >= 1 && fkeyNum <= 12) {
            return makeToken(hotkey, TokenType::Hotkey);
        }
    }
    
    // Scan for modifier combinations (Ctrl+, Alt+, Shift+, Win+)
    while (!isAtEnd() && isHotkeyChar(peek())) {
        char c = peek();
        hotkey += advance();
        
        // Stop at whitespace or special characters that end hotkeys
        if (c == ' ' || c == '=' || c == '{' || c == '(' || c == '|') {
            position--; // Put back the character
            hotkey.pop_back();
            break;
        }
    }
    
    // Check if this looks like a hotkey pattern
    std::regex hotkeyPattern(R"((Ctrl|Alt|Shift|Win)\+\w+|F[1-9]|F1[0-2])");
    if (std::regex_match(hotkey, hotkeyPattern)) {
        return makeToken(hotkey, TokenType::Hotkey);
    }
    
    // Not a hotkey, treat as identifier
    // Reset position to scan as identifier
    position -= hotkey.length() - 1;
    return scanIdentifier();
}

std::vector<Token> HavelLexer::tokenize() {
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
        
        // Handle potential modifier hotkeys (Ctrl, Alt, Shift, Win)
        if (c == 'C' || c == 'A' || c == 'S' || c == 'W') {
            size_t saved_pos = position - 1;
            std::string word;