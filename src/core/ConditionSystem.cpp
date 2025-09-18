#include "ConditionSystem.hpp"
#include <sstream>
#include <algorithm>
#include "utils/Logger.hpp"

namespace havel {
void Condition::compile() {
    if (isCompiled) return;
    
    if (op == ConditionOperator::MATCHES || op == ConditionOperator::NOT_MATCHES) {
        try {
            regexPattern = std::regex(value, std::regex_constants::icase);
        } catch (const std::regex_error& e) {
            throw std::runtime_error("Invalid regex pattern: " + value);
        }
    }
    
    if (op == ConditionOperator::IN_LIST || op == ConditionOperator::NOT_IN_LIST) {
        std::istringstream ss(value);
        std::string item;
        listValue.clear();
        while (std::getline(ss, item, ',')) {
            // Trim whitespace
            item.erase(0, item.find_first_not_of(" \t"));
            item.erase(item.find_last_not_of(" \t") + 1);
            if (!item.empty()) {
                listValue.push_back(item);
            }
        }
    }
    
    isCompiled = true;
}

bool Condition::evaluate(const std::map<std::string, Property>& properties) {
    if (!isCompiled) compile();
    
    auto propIt = properties.find(propertyName);
    if (propIt == properties.end()) {
        return false; // Property not found
    }
    
    const Property& prop = propIt->second;
    
    switch (prop.type) {
        case PropertyType::STRING: {
            std::string propValue = prop.getter();
            std::string testValue = value;
            
            switch (op) {
                case ConditionOperator::EQUALS:
                    return propValue == testValue;
                case ConditionOperator::NOT_EQUALS:
                    return propValue != testValue;
                case ConditionOperator::CONTAINS:
                    return propValue.find(testValue) != std::string::npos;
                case ConditionOperator::NOT_CONTAINS:
                    return propValue.find(testValue) == std::string::npos;
                case ConditionOperator::MATCHES:
                    return std::regex_search(propValue, regexPattern);
                case ConditionOperator::NOT_MATCHES:
                    return !std::regex_search(propValue, regexPattern);
                case ConditionOperator::IN_LIST:
                    return std::find(listValue.begin(), listValue.end(), propValue) != listValue.end();
                case ConditionOperator::NOT_IN_LIST:
                    return std::find(listValue.begin(), listValue.end(), propValue) == listValue.end();
                default:
                    return false;
            }
        }
        case PropertyType::INTEGER: {
            int propValue = prop.intGetter();
            int testValue = std::stoi(value);
            
            switch (op) {
                case ConditionOperator::EQUALS:
                    return propValue == testValue;
                case ConditionOperator::NOT_EQUALS:
                    return propValue != testValue;
                case ConditionOperator::GREATER_THAN:
                    return propValue > testValue;
                case ConditionOperator::LESS_THAN:
                    return propValue < testValue;
                default:
                    return false;
            }
        }
        case PropertyType::BOOLEAN: {
            bool propValue = prop.boolGetter();
            bool testValue = (value == "true" || value == "1" || value == "yes");
            
            switch (op) {
                case ConditionOperator::EQUALS:
                    return propValue == testValue;
                case ConditionOperator::NOT_EQUALS:
                    return propValue != testValue;
                default:
                    return false;
            }
        }
        default:
            return false;
    }
}

void ConditionEngine::registerProperty(const std::string& name, PropertyType type, 
                                      std::function<std::string()> getter) {
    properties[name] = {name, type, getter, nullptr, nullptr, nullptr};
}

void ConditionEngine::registerIntProperty(const std::string& name, std::function<int()> getter) {
    properties[name] = {name, PropertyType::INTEGER, nullptr, getter, nullptr, nullptr};
}

void ConditionEngine::registerBoolProperty(const std::string& name, std::function<bool()> getter) {
    properties[name] = {name, PropertyType::BOOLEAN, nullptr, nullptr, getter, nullptr};
}

void ConditionEngine::registerListProperty(const std::string& name, std::function<std::vector<std::string>()> getter) {
    properties[name] = {name, PropertyType::LIST, nullptr, nullptr, nullptr, getter};
}

Condition ConditionEngine::parseCondition(const std::string& conditionStr) {
    Condition cond;
    
    // Parse operators (order matters - longer operators first)
    std::vector<std::pair<std::string, ConditionOperator>> operators = {
        {"!=", ConditionOperator::NOT_EQUALS}, {"==", ConditionOperator::EQUALS},
        {"!~", ConditionOperator::NOT_CONTAINS}, {"~", ConditionOperator::CONTAINS},
        {"!matches", ConditionOperator::NOT_MATCHES}, {"matches", ConditionOperator::MATCHES},
        {"!in", ConditionOperator::NOT_IN_LIST}, {"in", ConditionOperator::IN_LIST},
        {">", ConditionOperator::GREATER_THAN}, {"<", ConditionOperator::LESS_THAN},
        {"=", ConditionOperator::EQUALS}
    };
    
    for (const auto& [opStr, op] : operators) {
        size_t pos = conditionStr.find(opStr);
        if (pos != std::string::npos) {
            cond.propertyName = conditionStr.substr(0, pos);
            cond.op = op;
            cond.value = conditionStr.substr(pos + opStr.length());
            
            // Trim whitespace
            cond.propertyName.erase(0, cond.propertyName.find_first_not_of(" \t"));
            cond.propertyName.erase(cond.propertyName.find_last_not_of(" \t") + 1);
            cond.value.erase(0, cond.value.find_first_not_of(" \t"));
            cond.value.erase(cond.value.find_last_not_of(" \t") + 1);
            
            // Remove quotes from value
            if (!cond.value.empty() && cond.value.front() == '\'' && cond.value.back() == '\'') {
                cond.value = cond.value.substr(1, cond.value.length() - 2);
            }
            
            break;
        }
    }
    
    if (cond.propertyName.empty()) {
        // Handle boolean properties like "gaming.active" which don't have an operator
        cond.propertyName = conditionStr;
        cond.op = ConditionOperator::EQUALS;
        cond.value = "true";
    }
    
    return cond;
}


bool ConditionEngine::evaluateCondition(const std::string& conditionStr) {
    // Check cache first
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = conditionCache.find(conditionStr);
        if (it != conditionCache.end()) {
            return it->second;
        }
    }
    
    bool result = false;
    
    try {
        // Simple NOT prefix handling
        bool negated = !conditionStr.empty() && conditionStr[0] == '!';
        std::string actualCondition = negated ? conditionStr.substr(1) : conditionStr;

        Condition cond = parseCondition(actualCondition);
        result = cond.evaluate(properties);

        if (negated) {
            result = !result;
        }

    } catch (const std::exception& e) {
        // Log error but don't crash
        error("Condition evaluation error: " + std::string(e.what()));
        result = false;
    }
    
    // Cache result
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        conditionCache[conditionStr] = result;
    }
    
    return result;
}

void ConditionEngine::invalidateCache() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    conditionCache.clear();
    cacheGeneration++;
}
} // namespace havel
