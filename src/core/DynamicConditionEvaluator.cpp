#include "DynamicConditionEvaluator.hpp"
#include "core/IO.hpp"
#include "utils/Logger.hpp"
#include <chrono>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <set>
#include <regex>

namespace havel {

DynamicConditionEvaluator::DynamicConditionEvaluator(std::shared_ptr<IO> io)
    : io(io) {}

DynamicConditionEvaluator::~DynamicConditionEvaluator() = default;

void DynamicConditionEvaluator::setModeGetter(ModeGetter getter) {
    modeGetter = std::move(getter);
}

void DynamicConditionEvaluator::setPreviousModeGetter(PreviousModeGetter getter) {
    previousModeGetter = std::move(getter);
}

bool DynamicConditionEvaluator::evaluate(const std::string& condition) {
    if (condition.empty()) {
        return true;
    }

    try {
        auto tokens = tokenize(condition);
        size_t pos = 0;
        return parseExpression(tokens, pos);
    } catch (const std::exception& e) {
        error("Failed to evaluate condition '{}': {}", condition, e.what());
        return false;
    }
}

std::string DynamicConditionEvaluator::getExe() {
    return io->GetActiveWindowProcess();
}

std::string DynamicConditionEvaluator::getClass() {
    return io->GetActiveWindowClass();
}

std::string DynamicConditionEvaluator::getTitle() {
    return io->GetActiveWindowTitle();
}

int DynamicConditionEvaluator::getHour() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);
    return tm.tm_hour;
}

int DynamicConditionEvaluator::getMinute() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);
    return tm.tm_min;
}

int DynamicConditionEvaluator::getBatteryLevel() {
    // TODO: Implement battery level detection
    // For now, return 100 (full battery)
    return 100;
}

double DynamicConditionEvaluator::getCpuLoad() {
    // TODO: Implement CPU load detection
    // For now, return 0
    return 0.0;
}

std::string DynamicConditionEvaluator::getCurrentMode() {
    if (modeGetter) {
        return modeGetter();
    }
    return "default";
}

std::string DynamicConditionEvaluator::getPreviousMode() {
    if (previousModeGetter) {
        return previousModeGetter();
    }
    return "default";
}

std::string DynamicConditionEvaluator::getIdentifierValue(const std::string& identifier) {
    if (identifier == "exe") {
        return getExe();
    } else if (identifier == "class") {
        return getClass();
    } else if (identifier == "title") {
        return getTitle();
    } else if (identifier == "mode.current" || identifier == "mode") {
        return getCurrentMode();
    } else if (identifier == "mode.previous") {
        return getPreviousMode();
    }
    return "";
}

// Simple tokenizer
std::vector<DynamicConditionEvaluator::Token> DynamicConditionEvaluator::tokenize(const std::string& condition) {
    std::vector<Token> tokens;
    size_t i = 0;

    while (i < condition.size()) {
        // Skip whitespace
        if (std::isspace(condition[i])) {
            i++;
            continue;
        }

        // String literal
        if (condition[i] == '"' || condition[i] == '\'') {
            char quote = condition[i];
            i++;
            std::string value;
            while (i < condition.size() && condition[i] != quote) {
                value += condition[i];
                i++;
            }
            i++; // Skip closing quote
            tokens.push_back({TokenType::String, value});
            continue;
        }

        // Operators
        if (i + 1 < condition.size()) {
            std::string twoChar = condition.substr(i, 2);
            if (twoChar == "==" || twoChar == "!=" || twoChar == "<=" ||
                twoChar == ">=" || twoChar == "&&" || twoChar == "||") {
                tokens.push_back({TokenType::Operator, twoChar});
                i += 2;
                continue;
            }
        }

        // Regex match operator ~
        if (condition[i] == '~') {
            tokens.push_back({TokenType::Operator, "~"});
            i++;
            continue;
        }

        if (condition[i] == '<' || condition[i] == '>' || condition[i] == '!' ||
            condition[i] == '(' || condition[i] == ')' || condition[i] == '[' ||
            condition[i] == ']') {
            tokens.push_back({TokenType::Operator, std::string(1, condition[i])});
            i++;
            continue;
        }

        // Identifier or keyword (including dotted like "time.hour")
        if (std::isalpha(condition[i]) || condition[i] == '_') {
            std::string value;
            while (i < condition.size() && 
                   (std::isalnum(condition[i]) || condition[i] == '_' || condition[i] == '.')) {
                value += condition[i];
                i++;
            }
            
            // Check for logical operators
            if (value == "and" || value == "&&") {
                tokens.push_back({TokenType::LogicalOp, "&&"});
            } else if (value == "or" || value == "||") {
                tokens.push_back({TokenType::LogicalOp, "||"});
            } else if (value == "not" || value == "!") {
                tokens.push_back({TokenType::LogicalOp, "!"});
            } else if (value == "in" || value == "not") {
                tokens.push_back({TokenType::Identifier, value});
            } else {
                tokens.push_back({TokenType::Identifier, value});
            }
            continue;
        }

        // Number
        if (std::isdigit(condition[i])) {
            std::string value;
            while (i < condition.size() && (std::isdigit(condition[i]) || condition[i] == '.')) {
                value += condition[i];
                i++;
            }
            tokens.push_back({TokenType::Number, value});
            continue;
        }

        i++;
    }

    tokens.push_back({TokenType::End, ""});
    return tokens;
}

bool DynamicConditionEvaluator::parseExpression(const std::vector<Token>& tokens, size_t& pos) {
    return parseOr(tokens, pos);
}

bool DynamicConditionEvaluator::parseOr(const std::vector<Token>& tokens, size_t& pos) {
    bool left = parseAnd(tokens, pos);

    while (pos < tokens.size() && tokens[pos].type == TokenType::LogicalOp && tokens[pos].value == "||") {
        pos++;
        bool right = parseAnd(tokens, pos);
        left = left || right;
    }

    return left;
}

bool DynamicConditionEvaluator::parseAnd(const std::vector<Token>& tokens, size_t& pos) {
    bool left = parseNot(tokens, pos);

    while (pos < tokens.size() && tokens[pos].type == TokenType::LogicalOp && tokens[pos].value == "&&") {
        pos++;
        bool right = parseNot(tokens, pos);
        left = left && right;
    }

    return left;
}

bool DynamicConditionEvaluator::parseNot(const std::vector<Token>& tokens, size_t& pos) {
    if (pos < tokens.size() && tokens[pos].type == TokenType::LogicalOp && tokens[pos].value == "!") {
        pos++;
        return !parseNot(tokens, pos);
    }
    return parseComparison(tokens, pos);
}

bool DynamicConditionEvaluator::parseComparison(const std::vector<Token>& tokens, size_t& pos) {
    // Get left operand
    std::string leftStr;
    double leftNum = 0;
    bool leftIsNum = false;

    if (pos >= tokens.size()) return false;

    const Token& left = tokens[pos];
    
    if (left.type == TokenType::Identifier) {
        leftStr = getIdentifierValue(left.value);
        pos++;
    } else if (left.type == TokenType::String) {
        leftStr = left.value;
        pos++;
    } else if (left.type == TokenType::Number) {
        leftNum = std::stod(left.value);
        leftIsNum = true;
        pos++;
    } else {
        pos++;
        return false;
    }

    // Check for "in" operator
    if (pos < tokens.size() && tokens[pos].type == TokenType::Identifier && tokens[pos].value == "in") {
        pos++;

        // Expect [list]
        if (pos >= tokens.size() || tokens[pos].type != TokenType::Operator || tokens[pos].value != "[") {
            return false;
        }
        pos++;

        std::set<std::string> values;
        while (pos < tokens.size() && tokens[pos].type != TokenType::Operator) {
            if (tokens[pos].type == TokenType::String) {
                values.insert(tokens[pos].value);
            }
            pos++;
        }

        if (pos < tokens.size() && tokens[pos].type == TokenType::Operator && tokens[pos].value == "]") {
            pos++;
        }

        return values.count(leftStr) > 0;
    }

    // Get operator
    if (pos >= tokens.size() || tokens[pos].type != TokenType::Operator) {
        // No operator, return truthiness of left
        if (leftIsNum) return leftNum != 0;
        return !leftStr.empty();
    }

    std::string op = tokens[pos].value;
    pos++;

    // Handle regex match operator ~
    if (op == "~") {
        // Get right operand (pattern)
        if (pos >= tokens.size()) return false;

        const Token& right = tokens[pos];
        std::string pattern;

        if (right.type == TokenType::String) {
            pattern = right.value;
            pos++;
        } else if (right.type == TokenType::Identifier) {
            // Could be a regex literal like /pattern/
            std::string idValue = right.value;
            if (idValue.size() >= 2 && idValue.front() == '/' && idValue.back() == '/') {
                pattern = idValue.substr(1, idValue.size() - 2);
                pos++;
            } else {
                pattern = getIdentifierValue(idValue);
                pos++;
            }
        } else {
            return false;
        }

        // Perform regex match
        try {
            std::regex re(pattern);
            return std::regex_search(leftStr, re);
        } catch (const std::regex_error&) {
            return false;
        }
    }

    // Get right operand
    std::string rightStr;
    double rightNum = 0;
    bool rightIsNum = false;

    if (pos >= tokens.size()) return false;

    const Token& right = tokens[pos];

    if (right.type == TokenType::Identifier) {
        rightStr = getIdentifierValue(right.value);
        pos++;
    } else if (right.type == TokenType::String) {
        rightStr = right.value;
        pos++;
    } else if (right.type == TokenType::Number) {
        rightNum = std::stod(right.value);
        rightIsNum = true;
        pos++;
    } else {
        return false;
    }

    // Evaluate comparison
    if (leftIsNum && rightIsNum) {
        if (op == "==") return leftNum == rightNum;
        if (op == "!=") return leftNum != rightNum;
        if (op == "<") return leftNum < rightNum;
        if (op == ">") return leftNum > rightNum;
        if (op == "<=") return leftNum <= rightNum;
        if (op == ">=") return leftNum >= rightNum;
    } else {
        // String comparison
        if (op == "==") return leftStr == rightStr;
        if (op == "!=") return leftStr != rightStr;
        if (op == "<") return leftStr < rightStr;
        if (op == ">") return leftStr > rightStr;
        if (op == "<=") return leftStr <= rightStr;
        if (op == ">=") return leftStr >= rightStr;
    }

    return false;
}

} // namespace havel
