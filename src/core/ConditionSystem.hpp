#pragma once
#include <string>
#include <functional>
#include <map>
#include <vector>
#include <memory>
#include <regex>
#include <mutex>
#include <atomic>
#include "types.hpp"
namespace havel {
enum class ConditionOperator {
    EQUALS,
    NOT_EQUALS,
    CONTAINS,
    NOT_CONTAINS,
    MATCHES,        // regex
    NOT_MATCHES,    // regex
    GREATER_THAN,
    LESS_THAN,
    IN_LIST,
    NOT_IN_LIST
};

enum class PropertyType {
    STRING,
    INTEGER,
    BOOLEAN,
    LIST
};

struct Property {
    std::string name;
    PropertyType type;
    std::function<std::string()> getter;
    std::function<int()> intGetter;
    std::function<bool()> boolGetter;
    std::function<std::vector<std::string>()> listGetter;
};

struct Condition {
    std::string propertyName;
    ConditionOperator op;
    std::string value;
    std::regex regexPattern;
    std::vector<std::string> listValue;
    bool isCompiled = false;
    
    void compile();
    bool evaluate(const std::map<std::string, Property>& properties);
};

class ConditionEngine {
private:
    std::map<std::string, Property> properties;
    std::map<std::string, bool> conditionCache;
    std::mutex cacheMutex;
    std::atomic<uint64_t> cacheGeneration{0};
    
public:
    void registerProperty(const std::string& name, PropertyType type, 
                         std::function<std::string()> getter);
    void registerIntProperty(const std::string& name, std::function<int()> getter);
    void registerBoolProperty(const std::string& name, std::function<bool()> getter);
    void registerListProperty(const std::string& name, std::function<std::vector<std::string>()> getter);
    
    bool evaluateCondition(const std::string& conditionStr);
    void invalidateCache();
    
private:
    Condition parseCondition(const std::string& conditionStr);
    std::vector<Condition> parseComplexCondition(const std::string& conditionStr);
    bool evaluateComplexCondition(const std::vector<Condition>& conditions, const std::string& logic);
};
} // namespace havel