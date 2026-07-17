#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "lexer/Lexer.hpp"

namespace havel {

enum class HighlightType {
    None,
    Keyword,
    String,
    Number,
    Comment,
    Identifier,
    Operator,
    Function,
    Type
};

struct HighlightConfig {
    std::string keywordColor;
    std::string stringColor;
    std::string numberColor;
    std::string commentColor;
    std::string identifierColor;
    std::string operatorColor;
    std::string functionColor;
    std::string typeColor;
    std::string resetCode = "\033[0m";

    static HighlightConfig Default() {
        return {
            "\033[35m", // Magenta (Keywords)
            "\033[32m", // Green (Strings)
            "\033[36m", // Cyan (Numbers)
            "\033[90m", // Gray (Comments)
            "\033[37m", // White (Identifiers)
            "\033[33m", // Yellow (Operators)
            "\033[34m", // Blue (Functions)
            "\033[95m"  // Light Magenta (Types)
        };
    }
};

class SyntaxHighlighter {
public:
    SyntaxHighlighter(const HighlightConfig& config) : config(config) {}

    std::string highlight(const std::string& source, const std::vector<Token>& tokens);
    HighlightType getHighlightType(TokenType type);

private:
    HighlightConfig config;
};

} // namespace havel
