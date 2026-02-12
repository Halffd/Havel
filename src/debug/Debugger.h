#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <variant>
#include <chrono>

namespace havel::debugger {

// Forward declarations
class Debugger;
class Breakpoint;
class StackFrame;
class Variable;

// Debug protocol types
using DebugValue = std::variant<
    std::nullptr_t,
    bool,
    int64_t,
    double,
    std::string,
    std::vector<DebugValue>,
    std::unordered_map<std::string, DebugValue>
>;

// Stack frame information
struct StackFrame {
    std::string function_name;
    std::string file_name;
    uint32_t line_number;
    uint32_t column_number;
    std::unordered_map<std::string, DebugValue> locals;
    std::unordered_map<std::string, DebugValue> parameters;
    
    StackFrame(const std::string& func, const std::string& file, uint32_t line, uint32_t col)
        : function_name(func), file_name(file), line_number(line), column_number(col) {}
};

// Breakpoint information
struct Breakpoint {
    enum class Type {
        LINE,
        FUNCTION,
        CONDITION,
        EXCEPTION
    };
    
    Type type;
    std::string file_name;
    uint32_t line_number;
    std::string function_name;
    std::string condition;
    bool enabled;
    uint32_t hit_count;
    uint32_t id;
    
    Breakpoint(uint32_t id, Type type) : id(id), type(type), enabled(true), hit_count(0) {}
    
    bool shouldBreak(const std::string& file, uint32_t line, const std::unordered_map<std::string, DebugValue>& locals = {}) {
        if (!enabled) return false;
        
        switch (type) {
            case Type::LINE:
                return file_name == file && line_number == line;
            case Type::FUNCTION:
                return function_name == file; // file contains function name in this context
            case Type::CONDITION:
                return evaluateCondition(condition, locals);
            case Type::EXCEPTION:
                return true; // Always break on exceptions
        }
        return false;
    }
    
private:
    bool evaluateCondition(const std::string& condition, const std::unordered_map<std::string, DebugValue>& locals) {
        // Simple condition evaluation (could be enhanced)
        if (condition.empty()) return true;
        
        // Parse simple conditions like "x > 10" or "name == 'test'"
        // This is a simplified implementation
        return true; // Placeholder
    }
};

// Debug event types
enum class DebugEvent {
    BREAKPOINT_HIT,
    STEP_COMPLETE,
    EXCEPTION_THROWN,
    FUNCTION_ENTER,
    FUNCTION_EXIT,
    VARIABLE_CHANGED,
    WATCHPOINT_HIT
};

// Debug event data
struct DebugEventData {
    DebugEvent type;
    std::string file_name;
    uint32_t line_number;
    uint32_t column_number;
    std::string message;
    std::shared_ptr<Breakpoint> breakpoint;
    std::vector<StackFrame> call_stack;
    std::unordered_map<std::string, DebugValue> variables;
    
    DebugEventData(DebugEvent t) : type(t) {}
};

// Debugger interface
class Debugger {
private:
    std::vector<std::shared_ptr<Breakpoint>> breakpoints;
    std::vector<StackFrame> call_stack;
    std::unordered_map<std::string, DebugValue> watch_variables;
    bool is_paused = false;
    bool is_stepping = false;
    uint32_t next_breakpoint_id = 1;
    
    // Debug protocol handlers
    std::function<void(const DebugEventData&)> event_handler;
    std::function<std::string()> input_handler;
    
public:
    Debugger() = default;
    ~Debugger() = default;
    
    // Breakpoint management
    uint32_t setBreakpoint(const std::string& file, uint32_t line) {
        auto bp = std::make_shared<Breakpoint>(next_breakpoint_id++, Breakpoint::Type::LINE);
        bp->file_name = file;
        bp->line_number = line;
        breakpoints.push_back(bp);
        return bp->id;
    }
    
    uint32_t setFunctionBreakpoint(const std::string& function_name) {
        auto bp = std::make_shared<Breakpoint>(next_breakpoint_id++, Breakpoint::Type::FUNCTION);
        bp->function_name = function_name;
        breakpoints.push_back(bp);
        return bp->id;
    }
    
    uint32_t setConditionalBreakpoint(const std::string& condition) {
        auto bp = std::make_shared<Breakpoint>(next_breakpoint_id++, Breakpoint::Type::CONDITION);
        bp->condition = condition;
        breakpoints.push_back(bp);
        return bp->id;
    }
    
    bool removeBreakpoint(uint32_t id) {
        auto it = std::remove_if(breakpoints.begin(), breakpoints.end(),
            [id](const std::shared_ptr<Breakpoint>& bp) { return bp->id == id; });
        if (it != breakpoints.end()) {
            breakpoints.erase(it, breakpoints.end());
            return true;
        }
        return false;
    }
    
    bool toggleBreakpoint(uint32_t id) {
        for (auto& bp : breakpoints) {
            if (bp->id == id) {
                bp->enabled = !bp->enabled;
                return true;
            }
        }
        return false;
    }
    
    // Stack frame management
    void pushStackFrame(const std::string& function, const std::string& file, uint32_t line, uint32_t col) {
        call_stack.emplace_back(function, file, line, col);
    }
    
    void popStackFrame() {
        if (!call_stack.empty()) {
            call_stack.pop_back();
        }
    }
    
    void updateLocals(const std::unordered_map<std::string, DebugValue>& locals) {
        if (!call_stack.empty()) {
            call_stack.back().locals = locals;
        }
    }
    
    void updateParameters(const std::unordered_map<std::string, DebugValue>& params) {
        if (!call_stack.empty()) {
            call_stack.back().parameters = params;
        }
    }
    
    // Execution control
    void pause() {
        is_paused = true;
        notifyEvent(DebugEventData(DebugEvent::BREAKPOINT_HIT));
    }
    
    void resume() {
        is_paused = false;
        is_stepping = false;
    }
    
    void step() {
        is_paused = false;
        is_stepping = true;
    }
    
    void stepOver() {
        is_paused = false;
        is_stepping = true;
        // TODO: Implement step over logic
    }
    
    void stepOut() {
        is_paused = false;
        is_stepping = true;
        // TODO: Implement step out logic
    }
    
    // Variable inspection
    void setWatchVariable(const std::string& name, const DebugValue& value) {
        watch_variables[name] = value;
    }
    
    DebugValue getVariable(const std::string& name) {
        if (!call_stack.empty()) {
            const auto& frame = call_stack.back();
            
            // Check locals first
            auto it = frame.locals.find(name);
            if (it != frame.locals.end()) {
                return it->second;
            }
            
            // Check parameters
            it = frame.parameters.find(name);
            if (it != frame.parameters.end()) {
                return it->second;
            }
        }
        
        // Check watch variables
        auto it = watch_variables.find(name);
        if (it != watch_variables.end()) {
            return it->second;
        }
        
        return nullptr; // Variable not found
    }
    
    std::vector<StackFrame> getCallStack() const {
        return call_stack;
    }
    
    std::vector<std::shared_ptr<Breakpoint>> getBreakpoints() const {
        return breakpoints;
    }
    
    // Event handling
    void setEventHandler(std::function<void(const DebugEventData&)> handler) {
        event_handler = handler;
    }
    
    void setInputHandler(std::function<std::string()> handler) {
        input_handler = handler;
    }
    
    // Debug protocol interface
    bool shouldBreak(const std::string& file, uint32_t line) {
        if (is_paused) return true;
        if (is_stepping) {
            is_stepping = false;
            return true;
        }
        
        for (auto& bp : breakpoints) {
            if (bp->shouldBreak(file, line, call_stack.empty() ? 
                std::unordered_map<std::string, DebugValue>() : call_stack.back().locals)) {
                bp->hit_count++;
                
                DebugEventData event(DebugEvent::BREAKPOINT_HIT);
                event.file_name = file;
                event.line_number = line;
                event.breakpoint = bp;
                event.call_stack = call_stack;
                
                notifyEvent(event);
                return true;
            }
        }
        
        return false;
    }
    
    void onException(const std::string& message, const std::string& file, uint32_t line) {
        DebugEventData event(DebugEvent::EXCEPTION_THROWN);
        event.file_name = file;
        event.line_number = line;
        event.message = message;
        event.call_stack = call_stack;
        
        notifyEvent(event);
    }
    
    void onFunctionEnter(const std::string& function, const std::string& file, uint32_t line, uint32_t col) {
        pushStackFrame(function, file, line, col);
        
        DebugEventData event(DebugEvent::FUNCTION_ENTER);
        event.file_name = file;
        event.line_number = line;
        event.message = "Entered function: " + function;
        event.call_stack = call_stack;
        
        notifyEvent(event);
    }
    
    void onFunctionExit(const std::string& function) {
        DebugEventData event(DebugEvent::FUNCTION_EXIT);
        event.message = "Exited function: " + function;
        event.call_stack = call_stack;
        
        notifyEvent(event);
        popStackFrame();
    }
    
    // Status
    bool isPaused() const { return is_paused; }
    bool isStepping() const { return is_stepping; }
    
    // Debug information
    struct DebugInfo {
        uint32_t total_breakpoints;
        uint32_t active_breakpoints;
        uint32_t call_stack_depth;
        bool is_paused;
        bool is_stepping;
    };
    
    DebugInfo getDebugInfo() const {
        uint32_t active = 0;
        for (const auto& bp : breakpoints) {
            if (bp->enabled) active++;
        }
        
        return {
            static_cast<uint32_t>(breakpoints.size()),
            active,
            static_cast<uint32_t>(call_stack.size()),
            is_paused,
            is_stepping
        };
    }
    
private:
    void notifyEvent(const DebugEventData& event) {
        if (event_handler) {
            event_handler(event);
        }
    }
};

// Debug value conversion utilities
namespace DebugUtils {
    inline DebugValue fromString(const std::string& str) {
        return str;
    }
    
    inline DebugValue fromInt(int64_t value) {
        return value;
    }
    
    inline DebugValue fromDouble(double value) {
        return value;
    }
    
    inline DebugValue fromBool(bool value) {
        return value;
    }
    
    inline DebugValue fromArray(const std::vector<DebugValue>& arr) {
        return arr;
    }
    
    inline DebugValue fromObject(const std::unordered_map<std::string, DebugValue>& obj) {
        return obj;
    }
    
    inline std::string toString(const DebugValue& value) {
        if (std::holds_alternative<std::nullptr_t>(value)) return "null";
        if (std::holds_alternative<bool>(value)) return std::get<bool>(value) ? "true" : "false";
        if (std::holds_alternative<int64_t>(value)) return std::to_string(std::get<int64_t>(value));
        if (std::holds_alternative<double>(value)) return std::to_string(std::get<double>(value));
        if (std::holds_alternative<std::string>(value)) return "\"" + std::get<std::string>(value) + "\"";
        if (std::holds_alternative<std::vector<DebugValue>>(value)) {
            const auto& arr = std::get<std::vector<DebugValue>>(value);
            std::string result = "[";
            for (size_t i = 0; i < arr.size(); i++) {
                if (i > 0) result += ", ";
                result += toString(arr[i]);
            }
            result += "]";
            return result;
        }
        if (std::holds_alternative<std::unordered_map<std::string, DebugValue>>(value)) {
            const auto& obj = std::get<std::unordered_map<std::string, DebugValue>>(value);
            std::string result = "{";
            bool first = true;
            for (const auto& [key, val] : obj) {
                if (!first) result += ", ";
                result += key + ": " + toString(val);
                first = false;
            }
            result += "}";
            return result;
        }
        return "unknown";
    }
}

} // namespace havel::debugger
