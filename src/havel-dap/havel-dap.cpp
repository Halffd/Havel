#include "havel-lang/compiler/core/Pipeline.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/runtime/Modules.hpp"
#include "havel-lang/runtime/HostContext.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <map>
#include <set>

using json = nlohmann::json;

static std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

struct DAPState {
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> running{false};
    std::atomic<bool> stopped{false};
    std::string next_action = "continue";
    int seq = 1;
    int var_ref_counter = 1;
    std::string script_path;
    havel::compiler::VM* vm_ptr = nullptr;
    std::map<std::string, std::vector<int>> breakpoints;
    std::set<std::string> func_breakpoints;
    bool break_on_throw = false;
    bool break_on_uncaught = false;
    int selected_frame = -1;
} dap;

static json varToDAP(const havel::compiler::Value& val, havel::compiler::VM& vm, int& next_ref) {
    json result;
    result["variablesReference"] = 0;

    if (val.isNull()) {
        result["value"] = "null";
        result["type"] = "null";
    } else if (val.isBool()) {
        result["value"] = val.asBool() ? "true" : "false";
        result["type"] = "bool";
    } else if (val.isInt()) {
        result["value"] = std::to_string(val.asInt());
        result["type"] = "int";
    } else if (val.isDouble()) {
        result["value"] = std::to_string(val.asDouble());
        result["type"] = "double";
    } else if (val.isStringId() || val.isStringValId()) {
        result["value"] = "\"" + vm.resolveStringKey(val) + "\"";
        result["type"] = "string";
    } else if (val.isArrayId()) {
        result["value"] = "[array]";
        result["type"] = "array";
    } else if (val.isObjectId()) {
        result["value"] = "{object}";
        result["type"] = "object";
    } else if (val.isClosureId() || val.isFunctionObjId() || val.isHostFuncId()) {
        result["value"] = "<function>";
        result["type"] = "function";
    } else {
        result["value"] = "<unknown>";
        result["type"] = "unknown";
    }
    return result;
}

static json getVariableChildren(int var_ref, havel::compiler::VM& vm, int& next_ref) {
    // var_ref encodes: positive = stack index tracking
    // We store a mapping from var_ref -> (frame, slot) in a simple map
    // For now, return empty (clients will re-fetch on each request)
    return json::array();
}

static void sendEvent(const std::string& event, const json& body = {}) {
    json msg;
    msg["type"] = "event";
    msg["seq"] = dap.seq++;
    msg["event"] = event;
    if (!body.empty()) msg["body"] = body;
    std::cout << msg.dump() << "\r\n" << std::flush;
}

static void sendResponse(int reqSeq, const std::string& command, bool success, const json& body = {}) {
    json msg;
    msg["type"] = "response";
    msg["seq"] = dap.seq++;
    msg["request_seq"] = reqSeq;
    msg["command"] = command;
    msg["success"] = success;
    if (!body.empty()) msg["body"] = body;
    std::cout << msg.dump() << "\r\n" << std::flush;
}

// var_ref tracking for variable hierarchy in DAP
// Maps variablesReference -> {frameIndex, variableName}
struct VarRefEntry {
    int frame_index = 0;
    std::string name;
    bool is_scope = false;
    std::string scope_type; // "locals" or "globals"
};
static std::map<int, VarRefEntry> var_ref_map;
static int next_var_ref = 1;

static void runScript() {
    std::string source = readFile(dap.script_path);
    if (source.empty()) {
        sendEvent("terminated");
        return;
    }

    havel::HostContext ctx;
    havel::compiler::VM vm(ctx);
    dap.vm_ptr = &vm;
    ctx.vm = &vm;
    havel::registerPureStdLib(vm);

    havel::compiler::PipelineOptions options;
    options.compile_unit_name = dap.script_path;
    options.vm_override = &vm;

    options.host_functions["print"] = [&vm](const std::vector<havel::compiler::Value>& args) {
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) std::cerr << " ";
            if (args[i].isStringValId() || args[i].isStringId()) {
                std::cerr << vm.resolveStringKey(args[i]);
            } else {
                std::cerr << args[i].toString();
            }
        }
        std::cerr << std::endl;
        return havel::compiler::Value::makeNull();
    };

    vm.attachDebugger();

    for (auto& [file, lines] : dap.breakpoints) {
        for (int line : lines) {
            vm.setBreakpoint(file, line);
        }
    }
    for (auto& fn : dap.func_breakpoints) {
        vm.setFunctionBreakpoint(fn);
    }
    if (dap.break_on_throw) vm.setBreakOnThrow(true);
    if (dap.break_on_uncaught) vm.setBreakOnUncaught(true);

    vm.setDebugBreakCallback([&]() {
        auto info = vm.getCurrentFrameInfo();
        auto excVal = vm.currentExceptionValue();

        json body;
        body["threadId"] = 1;
        body["allThreadsStopped"] = true;

        if (!excVal.isNull()) {
            body["reason"] = "exception";
            body["description"] = "Exception: " + vm.toString(excVal);
            body["text"] = vm.toString(excVal);
        } else {
            body["reason"] = "breakpoint";
            if (!info.function_name.empty()) {
                body["description"] = info.function_name;
            }
        }
        if (!info.source_file.empty()) {
            body["hitBreakpointIds"] = json::array();
        }

        sendEvent("stopped", body);

        std::unique_lock<std::mutex> lock(dap.mtx);
        dap.stopped = true;
        dap.cv.wait(lock, [&]{ return !dap.stopped.load(); });

        std::string action = dap.next_action;
        lock.unlock();

        if (action == "continue") {
            vm.setDebugStepMode(havel::compiler::VM::DebugStepMode::Continue);
        } else if (action == "stepIn") {
            vm.setDebugStepMode(havel::compiler::VM::DebugStepMode::StepInto);
        } else if (action == "stepOver") {
            auto frames = vm.getStackFrames();
            vm.setDebugStepMode(havel::compiler::VM::DebugStepMode::StepOver);
            vm.setDebugStepFrameDepth(frames.empty() ? 0 : frames.back().frame_depth);
        } else if (action == "stepOut") {
            auto frames = vm.getStackFrames();
            vm.setDebugStepMode(havel::compiler::VM::DebugStepMode::StepOut);
            vm.setDebugStepFrameDepth(frames.empty() ? 0 : frames.back().frame_depth);
        }
    });

    vm.setDebugStepMode(havel::compiler::VM::DebugStepMode::StepInto);

    try {
        havel::compiler::runBytecodePipeline(source, "__main__", options);
    } catch (const std::exception& e) {
        json body;
        body["reason"] = "exception";
        body["threadId"] = 1;
        body["allThreadsStopped"] = true;
        body["description"] = e.what();
        sendEvent("stopped", body);
    }

    sendEvent("terminated");
    dap.running = false;
}

int main() {
    std::thread vm_thread;
    bool vm_started = false;

    dap.seq = 1;
    dap.running = false;
    next_var_ref = 1;

    json capabilities;
    capabilities["supportsConfigurationDoneRequest"] = true;
    capabilities["supportsConditionalBreakpoints"] = false;
    capabilities["supportsFunctionBreakpoints"] = true;
    capabilities["supportsEvaluateForHovers"] = true;
    capabilities["supportsStepBack"] = false;
    capabilities["supportsSetVariable"] = false;
    capabilities["supportsRestartFrame"] = false;
    capabilities["supportsGotoTargetsRequest"] = false;
    capabilities["supportsStepInTargetsRequest"] = false;
    capabilities["supportsCompletionsRequest"] = false;
    capabilities["supportsModulesRequest"] = false;
    capabilities["supportsRestartRequest"] = false;
    capabilities["supportsExceptionOptions"] = true;
    capabilities["supportsValueFormattingOptions"] = false;
    capabilities["supportsExceptionInfoRequest"] = true;
    capabilities["supportsDelayedStackTraceLoading"] = true;

    for (std::string line; std::getline(std::cin, line); ) {
        if (line.empty() || line == "\r") continue;

        json msg;
        try {
            msg = json::parse(line);
        } catch (...) {
            continue;
        }

        if (msg["type"] != "request") continue;

        std::string command = msg["command"];
        int reqSeq = msg["seq"];

        if (command == "initialize") {
            sendResponse(reqSeq, command, true, {{"capabilities", capabilities}});
            sendEvent("initialized");
        } else if (command == "configurationDone") {
            sendResponse(reqSeq, command, true);
            if (!vm_started) {
                vm_started = true;
                dap.running = true;
                vm_thread = std::thread(runScript);
                vm_thread.detach();
            }
        } else if (command == "launch") {
            auto args = msg["arguments"];
            dap.script_path = args["program"].get<std::string>();
            sendResponse(reqSeq, command, true);
        } else if (command == "setBreakpoints") {
            auto args = msg["arguments"];
            std::string file = args["source"]["path"].get<std::string>();
            json bpJson = args["breakpoints"];

            dap.breakpoints[file].clear();
            json respBps = json::array();
            if (bpJson.is_array()) {
                for (auto& bp : bpJson) {
                    int line = bp["line"].get<int>();
                    dap.breakpoints[file].push_back(line);
                    json verified;
                    verified["verified"] = true;
                    verified["line"] = line;
                    respBps.push_back(verified);
                }
            }
            sendResponse(reqSeq, command, true, {{"breakpoints", respBps}});
        } else if (command == "setFunctionBreakpoints") {
            auto args = msg["arguments"];
            json bpJson = args["breakpoints"];
            dap.func_breakpoints.clear();
            json respBps = json::array();
            if (bpJson.is_array()) {
                for (auto& bp : bpJson) {
                    std::string name = bp["name"].get<std::string>();
                    dap.func_breakpoints.insert(name);
                    json verified;
                    verified["verified"] = true;
                    verified["name"] = name;
                    respBps.push_back(verified);
                }
            }
            sendResponse(reqSeq, command, true, {{"breakpoints", respBps}});
        } else if (command == "setExceptionBreakpoints") {
            auto args = msg["arguments"];
            json filters = args["filters"];
            dap.break_on_throw = false;
            dap.break_on_uncaught = false;
            if (filters.is_array()) {
                for (auto& f : filters) {
                    std::string name = f.get<std::string>();
                    if (name == "thrown") dap.break_on_throw = true;
                    if (name == "uncaught") dap.break_on_uncaught = true;
                    // Common DAP exception filter names:
                    if (name == "all") dap.break_on_throw = true;
                }
            }
            if (dap.vm_ptr) {
                dap.vm_ptr->setBreakOnThrow(dap.break_on_throw);
                dap.vm_ptr->setBreakOnUncaught(dap.break_on_uncaught);
            }
            json exceptionOptions = args.value("exceptionOptions", json::object());
            sendResponse(reqSeq, command, true);
        } else if (command == "stackTrace") {
            auto args = msg["arguments"];
            int threadId = args["threadId"];
            if (!dap.running || !dap.vm_ptr) {
                sendResponse(reqSeq, command, true, {{"stackFrames", json::array()}});
                continue;
            }
            auto frames = dap.vm_ptr->getStackFrames();
            json stackFrames = json::array();
            for (size_t i = 0; i < frames.size(); ++i) {
                auto& f = frames[i];
                json sf;
                sf["id"] = (int)i;
                sf["name"] = f.function_name.empty() ? "<top-level>" : f.function_name;
                sf["line"] = f.line > 0 ? f.line : 1;
                sf["column"] = f.column > 0 ? f.column : 1;
                if (!f.source_file.empty()) {
                    sf["source"]["path"] = f.source_file;
                    sf["source"]["name"] = f.source_file.substr(f.source_file.find_last_of('/') + 1);
                }
                stackFrames.push_back(sf);
            }
            sendResponse(reqSeq, command, true, {{"stackFrames", stackFrames}});
        } else if (command == "scopes") {
            auto args = msg["arguments"];
            int frameId = args["frameId"].get<int>();

            json scopes = json::array();

            // Locals scope
            json localScope;
            localScope["name"] = "Locals";
            localScope["presentationHint"] = "locals";
            localScope["variablesReference"] = 1000000 + frameId;
            localScope["expensive"] = false;
            scopes.push_back(localScope);

            // Globals scope
            json globalScope;
            globalScope["name"] = "Globals";
            globalScope["presentationHint"] = "globals";
            globalScope["variablesReference"] = 2000000 + frameId;
            globalScope["expensive"] = true;
            scopes.push_back(globalScope);

            sendResponse(reqSeq, command, true, {{"scopes", scopes}});
        } else if (command == "variables") {
            auto args = msg["arguments"];
            int varRef = args["variablesReference"].get<int>();

            json vars = json::array();

            if (varRef >= 2000000) {
                // Globals reference: 2000000 + frameId
                if (dap.vm_ptr) {
                    auto globals = dap.vm_ptr->getDebugGlobals();
                    for (auto& g : globals) {
                        json v;
                        v["name"] = g.name;
                        v["value"] = g.value;
                        v["type"] = g.type;
                        v["variablesReference"] = 0;
                        vars.push_back(v);
                    }
                }
            } else if (varRef >= 1000000) {
                // Locals reference: 1000000 + frameId
                int frameIdx = varRef - 1000000;
                if (dap.vm_ptr) {
                    auto locals = dap.vm_ptr->getLocals(frameIdx);
                    for (auto& l : locals) {
                        json v;
                        v["name"] = l.name;
                        v["value"] = l.value;
                        v["type"] = l.type;
                        v["variablesReference"] = 0;
                        vars.push_back(v);
                    }
                }
            }

            sendResponse(reqSeq, command, true, {{"variables", vars}});
        } else if (command == "continue") {
            {
                std::lock_guard<std::mutex> lock(dap.mtx);
                dap.next_action = "continue";
                dap.stopped = false;
            }
            dap.cv.notify_one();
            sendResponse(reqSeq, command, true, {{"allThreadsContinued", true}});
        } else if (command == "next") {
            {
                std::lock_guard<std::mutex> lock(dap.mtx);
                dap.next_action = "stepOver";
                dap.stopped = false;
            }
            dap.cv.notify_one();
            sendResponse(reqSeq, command, true);
        } else if (command == "stepIn") {
            {
                std::lock_guard<std::mutex> lock(dap.mtx);
                dap.next_action = "stepIn";
                dap.stopped = false;
            }
            dap.cv.notify_one();
            sendResponse(reqSeq, command, true);
        } else if (command == "stepOut") {
            {
                std::lock_guard<std::mutex> lock(dap.mtx);
                dap.next_action = "stepOut";
                dap.stopped = false;
            }
            dap.cv.notify_one();
            sendResponse(reqSeq, command, true);
        } else if (command == "evaluate") {
            auto args = msg["arguments"];
            std::string expr = args["expression"].get<std::string>();
            int frameId = args.value("frameId", -1);
            if (dap.vm_ptr && !expr.empty()) {
                auto val = dap.vm_ptr->evaluateInFrame(expr, frameId);
                auto result = varToDAP(val, *dap.vm_ptr, next_var_ref);
                sendResponse(reqSeq, command, true, result);
            } else {
                sendResponse(reqSeq, command, true, {{"result", ""}, {"type", "string"}, {"variablesReference", 0}});
            }
        } else if (command == "exceptionInfo") {
            json body;
            body["exceptionId"] = "runtime error";
            body["description"] = "Script exception";
            body["breakMode"] = "unhandled";
            if (dap.vm_ptr) {
                auto excVal = dap.vm_ptr->currentExceptionValue();
                if (!excVal.isNull()) {
                    body["description"] = dap.vm_ptr->toString(excVal);
                }
            }
            sendResponse(reqSeq, command, true, body);
        } else if (command == "threads") {
            json threads = json::array();
            json t;
            t["id"] = 1;
            t["name"] = "main";
            threads.push_back(t);
            sendResponse(reqSeq, command, true, {{"threads", threads}});
        } else if (command == "loadedSources") {
            sendResponse(reqSeq, command, true, {{"sources", json::array()}});
        } else if (command == "disconnect" || command == "terminate") {
            sendResponse(reqSeq, command, true);
            {
                std::lock_guard<std::mutex> lock(dap.mtx);
                dap.running = false;
                dap.next_action = "continue";
                dap.stopped = false;
            }
            dap.cv.notify_one();
            break;
        } else {
            sendResponse(reqSeq, command, true);
        }
    }

    return 0;
}
