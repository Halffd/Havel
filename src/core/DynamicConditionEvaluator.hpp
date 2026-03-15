#pragma once
#include <string>
#include <functional>
#include <memory>
#include <vector>

namespace havel {

class IO;

/**
 * DynamicConditionEvaluator - Evaluates mode conditions from script
 * 
 * Supports:
 * - exe == "process.exe"
 * - class == "WindowClass" || class in ["Class1", "Class2"]
 * - title == "Window Title" || title ~ "Regex.*"
 * - time.hour >= 22 || time.hour < 6
 * - time.minute == 0
 * - battery.level < 20
 * - cpu.load > 50
 * - mode.current == "gaming"
 * - mode.previous == "coding"
 * - Logical operators: &&, ||, !
 * - Comparison operators: ==, !=, <, >, <=, >=
 * - Collection operators: in, not in
 */
class DynamicConditionEvaluator {
public:
    explicit DynamicConditionEvaluator(std::shared_ptr<IO> io);
    ~DynamicConditionEvaluator();

    // Evaluate a condition string
    bool evaluate(const std::string& condition);

    // Get current mode (for mode.current, mode.previous)
    using ModeGetter = std::function<std::string()>;
    void setModeGetter(ModeGetter getter);

    // Get previous mode
    using PreviousModeGetter = std::function<std::string()>;
    void setPreviousModeGetter(PreviousModeGetter getter);

private:
    std::shared_ptr<IO> io;
    ModeGetter modeGetter;
    PreviousModeGetter previousModeGetter;

    // Token types for condition parsing
    enum class TokenType {
        Identifier,
        String,
        Number,
        Operator,
        LogicalOp,
        LParen,
        RParen,
        LBracket,
        RBracket,
        End
    };

    struct Token {
        TokenType type;
        std::string value;
    };

    // Lexer
    std::vector<Token> tokenize(const std::string& condition);
    
    // Parser
    bool parseExpression(const std::vector<Token>& tokens, size_t& pos);
    bool parseOr(const std::vector<Token>& tokens, size_t& pos);
    bool parseAnd(const std::vector<Token>& tokens, size_t& pos);
    bool parseNot(const std::vector<Token>& tokens, size_t& pos);
    bool parseComparison(const std::vector<Token>& tokens, size_t& pos);
    bool parsePrimary(const std::vector<Token>& tokens, size_t& pos);

    // Value extraction
    std::string getExe();
    std::string getClass();
    std::string getTitle();
    int getHour();
    int getMinute();
    int getBatteryLevel();
    double getCpuLoad();
    std::string getCurrentMode();
    std::string getPreviousMode();

    // Helper to get identifier value
    std::string getIdentifierValue(const std::string& identifier);
};

} // namespace havel
