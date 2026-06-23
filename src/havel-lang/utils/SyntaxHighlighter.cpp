#include "SyntaxHighlighter.hpp"

namespace havel {

HighlightType SyntaxHighlighter::getHighlightType(TokenType type) {
    switch (type) {
        case TokenType::Let:
        case TokenType::Val:
        case TokenType::Const:
        case TokenType::If:
        case TokenType::Else:
        case TokenType::While:
        case TokenType::For:
        case TokenType::In:
        case TokenType::Fn:
        case TokenType::Return:
        case TokenType::Import:
        case TokenType::From:
        case TokenType::As:
        case TokenType::Use:
        case TokenType::True:
        case TokenType::False:
        case TokenType::Null:
            return HighlightType::Keyword;
        
        case TokenType::String:
        case TokenType::MultilineString:
        case TokenType::InterpolatedString:
        case TokenType::InterpolatedBacktick:
            return HighlightType::String;

        case TokenType::Number:
            return HighlightType::Number;

        case TokenType::Comment:
            return HighlightType::Comment;

        case TokenType::Identifier:
            return HighlightType::Identifier;
        
        case TokenType::BinaryOp:
        case TokenType::Plus:
        case TokenType::Minus:
        case TokenType::Multiply:
        case TokenType::Divide:
        case TokenType::Assign:
            return HighlightType::Operator;

        default:
            return HighlightType::None;
    }
}

std::string SyntaxHighlighter::highlight(const std::string& source, const std::vector<Token>& tokens) {
    std::string result;
    size_t lastPos = 0;

    for (const auto& token : tokens) {
        // Add non-token text (whitespace, etc.)
        if (token.column - 1 > lastPos) {
            // This is a naive implementation, needs adjustment based on source structure
        }
        
        std::string color;
        HighlightType hType = getHighlightType(token.type);
        
        switch (hType) {
            case HighlightType::Keyword: color = config.keywordColor; break;
            case HighlightType::String: color = config.stringColor; break;
            case HighlightType::Number: color = config.numberColor; break;
            case HighlightType::Comment: color = config.commentColor; break;
            case HighlightType::Identifier: color = config.identifierColor; break;
            case HighlightType::Operator: color = config.operatorColor; break;
            default: color = "";
        }

        if (!color.empty()) {
            result += color + token.raw + config.resetCode;
        } else {
            result += token.raw;
        }
        
        lastPos = token.column - 1 + token.length;
    }
    
    return result;
}

} // namespace havel
