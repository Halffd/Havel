#include "havel-lang/compiler/core/Pipeline.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/compiler/runtime/DebugUtils.hpp"
#include "havel-lang/runtime/concurrency/Scheduler.hpp"
#include "havel-lang/runtime/Modules.hpp"
#include "havel-lang/runtime/HostContext.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <csignal>
#include <unordered_map>
#include <iomanip>

static volatile bool debugger_continue = false;

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

struct BreakpointEntry {
    std::string file;
    uint32_t line;
};
struct FuncBreakpointEntry { std::string name; };
struct WatchEntry {
    std::string expr;
    havel::compiler::Value last_val;
};

static void printHelp() {
    std::cout <<
      "  break <file:line>         Set line breakpoint\n"
      "  break <func>              Set function entry breakpoint\n"
      "  break list                List all breakpoints\n"
      "  break clear <n>           Clear breakpoint N\n"
      "  catch throw/uncaught      Exception breakpoint control\n"
      "  continue                  Continue execution\n"
      "  delete [all]              Delete all breakpoints\n"
      "  dis [func]                Disassemble bytecode\n"
      "  eval <expr>               Evaluate expression\n"
      "  fin                       Step out of current function\n"
      "  frame <n>                 Select frame N\n"
      "  funcs                     List known functions in script\n"
      "  globals                   Show global variables\n"
      "  heap                      Show GC/heap stats\n"
      "  help                      Show this help\n"
      "  hotkeys                   Show registered hotkeys\n"
      "  info [b/funcs/watches]    Info display\n"
      "  list [n]                  Show source lines\n"
      "  locals                    Show local variables\n"
      "  modules                   Show loaded modules\n"
      "  next                      Step over\n"
      "  pause                     Pause execution at next opportunity\n"
      "  print <expr>              Evaluate expression\n"
      "  quit                      Exit debugger\n"
      "  regs                      Show VM registers/state\n"
      "  run                       Start/continue execution\n"
      "  stack                     Show call stack\n"
      "  stack-values              Show operand stack values\n"
      "  step                      Step into next line\n"
      "  threads                   Show goroutines/fibers\n"
      "  watch <expr>              Watch expression\n"
      "  watch list/clear <n>      Watch management\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: hvdb <script.hv> [args...]" << std::endl;
        return 1;
    }

    std::string scriptPath = argv[1];
    std::string source = readFile(scriptPath);
    if (source.empty()) {
        std::cerr << "Failed to read file: " << scriptPath << std::endl;
        return 1;
    }

    std::vector<std::string> sourceLines;
    {
        std::istringstream stream(source);
        std::string line;
        while (std::getline(stream, line)) sourceLines.push_back(line);
    }

    havel::HostContext ctx;
    havel::compiler::VM vm(ctx);
    ctx.vm = &vm;
    havel::registerPureStdLib(vm);

    havel::compiler::PipelineOptions options;
    options.compile_unit_name = scriptPath;
    options.vm_override = &vm;

    options.host_functions["print"] = [&vm](const std::vector<havel::compiler::Value>& args) {
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) std::cout << " ";
            if (args[i].isStringValId() || args[i].isStringId())
                std::cout << vm.resolveStringKey(args[i]);
            else std::cout << args[i].toString();
        }
        std::cout << std::endl;
        return havel::compiler::Value::makeNull();
    };

    std::vector<BreakpointEntry> breakpoints;
    std::vector<FuncBreakpointEntry> funcBreakpoints;
    std::vector<WatchEntry> watches;
    int selected_frame = -1;
    bool running = false;

    vm.attachDebugger();

    vm.setDebugBreakCallback([&]() {
        debugger_continue = false;
        running = true;

        auto info = vm.getCurrentFrameInfo();
        auto excVal = vm.currentExceptionValue();

        if (!excVal.isNull())
            std::cout << "\nException: " << vm.toString(excVal);
        else if (vm.isPauseRequested())
            std::cout << "\nExecution paused";
        else
            std::cout << "\nBreakpoint hit";

        if (!info.source_file.empty())
            std::cout << " at " << info.source_file << ":" << info.line;
        if (!info.function_name.empty())
            std::cout << " in " << info.function_name;
        std::cout << std::endl;

        if (info.line > 0 && info.line <= sourceLines.size())
            std::cout << "  " << info.line << ": " << sourceLines[info.line - 1] << "\n";

        while (!debugger_continue) {
            std::cout << "(hvdb) " << std::flush;
            std::string line;
            if (!std::getline(std::cin, line)) {
                debugger_continue = true;
                break;
            }
            if (line.empty()) continue;

            std::istringstream iss(line);
            std::string cmd;
            iss >> cmd;

            if (cmd == "quit" || cmd == "q" || cmd == "exit") {
                std::cout << "Exiting." << std::endl;
                _Exit(0);
            } else if (cmd == "help" || cmd == "h" || cmd == "?") {
                printHelp();
            } else if (cmd == "run" || cmd == "r" || cmd == "go") {
                debugger_continue = true;
            } else if (cmd == "continue" || cmd == "c") {
                vm.setDebugStepMode(havel::compiler::VM::DebugStepMode::Continue);
                debugger_continue = true;
            } else if (cmd == "step" || cmd == "s" || cmd == "into") {
                vm.setDebugStepMode(havel::compiler::VM::DebugStepMode::StepInto);
                debugger_continue = true;
            } else if (cmd == "next" || cmd == "n" || cmd == "over") {
                auto frames = vm.getStackFrames();
                vm.setDebugStepMode(havel::compiler::VM::DebugStepMode::StepOver);
                vm.setDebugStepFrameDepth(frames.empty() ? 0 : frames.back().frame_depth);
                debugger_continue = true;
            } else if (cmd == "fin" || cmd == "f" || cmd == "out") {
                auto frames = vm.getStackFrames();
                vm.setDebugStepMode(havel::compiler::VM::DebugStepMode::StepOut);
                vm.setDebugStepFrameDepth(frames.empty() ? 0 : frames.back().frame_depth);
                debugger_continue = true;
            } else if (cmd == "pause" || cmd == "stop") {
                vm.requestPause();
                vm.setDebugStepMode(havel::compiler::VM::DebugStepMode::StepInto);
                debugger_continue = true;
            }
            // --- Display ---
            else if (cmd == "locals") {
                auto vars = vm.getLocals(selected_frame);
                if (vars.empty()) std::cout << "  (no locals)\n";
                else for (auto& v : vars)
                    std::cout << "  " << v.name << " : " << v.type << " = " << v.value << "\n";
            } else if (cmd == "globals" || cmd == "g") {
                auto vars = vm.getDebugGlobals();
                size_t count = 0;
                for (auto& v : vars) {
                    if (count >= 50) { std::cout << "  ... (truncated)\n"; break; }
                    std::cout << "  " << v.name << " : " << v.type << " = " << v.value << "\n";
                    count++;
                }
                if (vars.empty()) std::cout << "  (no globals)\n";
            } else if (cmd == "stack" || cmd == "bt" || cmd == "trace") {
                auto frames = vm.getStackFrames();
                if (frames.empty()) std::cout << "  (empty stack)\n";
                else for (size_t i = 0; i < frames.size(); ++i) {
                    auto& f = frames[i];
                    std::cout << "  #" << i << " "
                              << (static_cast<int>(i) == selected_frame ? "=> " : "   ");
                    if (!f.function_name.empty()) std::cout << f.function_name;
                    else std::cout << "<top-level>";
                    if (!f.source_file.empty())
                        std::cout << " at " << f.source_file << ":" << f.line;
                    std::cout << "\n";
                }
            } else if (cmd == "frame" || cmd == "fr") {
                std::string arg; iss >> arg;
                if (arg.empty())
                    std::cout << "  Current frame: " << selected_frame << "\n";
                else {
                    int n = std::stoi(arg);
                    auto frames = vm.getStackFrames();
                    if (n < 0 || (size_t)n >= frames.size())
                        std::cout << "  Invalid frame number\n";
                    else {
                        selected_frame = n;
                        std::cout << "  Switched to frame #" << n;
                        if (!frames[n].function_name.empty())
                            std::cout << " " << frames[n].function_name;
                        std::cout << "\n";
                    }
                }
            } else if (cmd == "print" || cmd == "p" || cmd == "eval") {
                std::string expr; std::getline(iss >> std::ws, expr);
                if (!expr.empty()) {
                    auto val = vm.evaluateInFrame(expr, selected_frame);
                    std::cout << "  " << expr << " = " << vm.toString(val) << "\n";
                }
            } else if (cmd == "list") {
                std::string arg; iss >> arg;
                uint32_t center = 0;
                if (!arg.empty()) center = std::stoul(arg);
                else center = vm.getCurrentFrameInfo().line;
                if (center == 0) center = 1;
                uint32_t start = center > 5 ? center - 5 : 1;
                uint32_t end = std::min(center + 5, (uint32_t)sourceLines.size());
                for (uint32_t i = start; i <= end; ++i)
                    std::cout << "  " << (i == center ? "=>" : "  ") << " " << i << ": " << sourceLines[i-1] << "\n";
            }
            // --- Breakpoints ---
            else if (cmd == "break" || cmd == "b") {
                std::string sub; iss >> sub;
                if (sub == "list" || sub == "l") {
                    if (breakpoints.empty() && funcBreakpoints.empty())
                        std::cout << "  No breakpoints\n";
                    else {
                        size_t n = 0;
                        for (auto& bp : breakpoints)
                            std::cout << "  #" << n++ << " " << bp.file << ":" << bp.line << "\n";
                        for (auto& fbp : funcBreakpoints)
                            std::cout << "  #" << n++ << " break " << fbp.name << " (function)\n";
                    }
                } else if (sub == "clear") {
                    std::string arg; iss >> arg;
                    if (arg.empty()) std::cout << "  Usage: break clear <n>\n";
                    else {
                        int n = std::stoi(arg);
                        if (n < 0 || (size_t)n >= breakpoints.size() + funcBreakpoints.size())
                            std::cout << "  Invalid number\n";
                        else if ((size_t)n < breakpoints.size()) {
                            vm.clearBreakpoint(breakpoints[n].file, breakpoints[n].line);
                            breakpoints.erase(breakpoints.begin() + n);
                            std::cout << "  Cleared #" << n << "\n";
                        } else {
                            int fn = n - (int)breakpoints.size();
                            vm.clearFunctionBreakpoint(funcBreakpoints[fn].name);
                            funcBreakpoints.erase(funcBreakpoints.begin() + fn);
                            std::cout << "  Cleared #" << n << "\n";
                        }
                    }
                } else if (!sub.empty()) {
                    auto colon = sub.find(':');
                    if (colon != std::string::npos) {
                        std::string fname = sub.substr(0, colon);
                        uint32_t lnum = std::stoul(sub.substr(colon + 1));
                        if (lnum == 0) { std::cout << "  Invalid line\n"; }
                        else {
                            vm.setBreakpoint(fname, lnum);
                            breakpoints.push_back({fname, lnum});
                            std::cout << "  Breakpoint at " << fname << ":" << lnum << "\n";
                        }
                    } else if (sub.find_first_not_of("0123456789") == std::string::npos) {
                        uint32_t lnum = std::stoul(sub);
                        vm.setBreakpoint(scriptPath, lnum);
                        breakpoints.push_back({scriptPath, lnum});
                        std::cout << "  Breakpoint at " << scriptPath << ":" << lnum << "\n";
                    } else {
                        vm.setFunctionBreakpoint(sub);
                        funcBreakpoints.push_back({sub});
                        std::cout << "  Function breakpoint: " << sub << "\n";
                    }
                } else std::cout << "  Usage: break <file:line> | <func> | list | clear <n>\n";
            } else if (cmd == "delete" || cmd == "del") {
                std::string arg; iss >> arg;
                if (arg == "all" || arg.empty()) {
                    vm.clearAllBreakpoints(); vm.clearAllFunctionBreakpoints();
                    vm.setBreakOnThrow(false); vm.setBreakOnUncaught(false);
                    breakpoints.clear(); funcBreakpoints.clear(); watches.clear();
                    std::cout << "  Cleared all\n";
                }
            } else if (cmd == "catch") {
                std::string sub; iss >> sub;
                if (sub == "throw") { vm.setBreakOnThrow(true); std::cout << "  Break on throw\n"; }
                else if (sub == "uncaught") { vm.setBreakOnUncaught(true); std::cout << "  Break on uncaught\n"; }
                else {
                    std::cout << "  catch throw: " << (vm.breakOnThrow() ? "on" : "off") << "\n";
                    std::cout << "  catch uncaught: " << (vm.breakOnUncaught() ? "on" : "off") << "\n";
                }
            } else if (cmd == "watch") {
                std::string sub; std::getline(iss >> std::ws, sub);
                if (sub == "list") {
                    if (watches.empty()) std::cout << "  No watches\n";
                    else for (size_t i = 0; i < watches.size(); ++i)
                        std::cout << "  #" << i << " " << watches[i].expr << "\n";
                } else if (sub.rfind("clear", 0) == 0) {
                    std::string arg = sub.substr(5);
                    if (!arg.empty()) {
                        int n = std::stoi(arg);
                        if (n >= 0 && (size_t)n < watches.size()) {
                            watches.erase(watches.begin() + n);
                            std::cout << "  Cleared watch #" << n << "\n";
                        }
                    }
                } else if (!sub.empty()) {
                    watches.push_back({sub, havel::compiler::Value::makeNull()});
                    std::cout << "  Watch: " << sub << "\n";
                }
            }
            // --- Info ---
            else if (cmd == "info" || cmd == "i") {
                std::string sub; iss >> sub;
                if (sub == "breakpoints" || sub == "b") {
                    size_t n = 0;
                    for (auto& bp : breakpoints)
                        std::cout << "  #" << n++ << " break " << bp.file << ":" << bp.line << "\n";
                    for (auto& fbp : funcBreakpoints)
                        std::cout << "  #" << n++ << " break " << fbp.name << " (function)\n";
                    if (n == 0) std::cout << "  No breakpoints\n";
                } else if (sub == "functions" || sub == "func" || sub == "funcs") {
                    auto chunk = vm.getMainChunk();
                    if (chunk) for (auto& fn : chunk->getAllFunctions())
                        if (!fn.name.empty()) {
                            std::cout << "  " << fn.name;
                            if (!fn.source_file.empty())
                                std::cout << " at " << fn.source_file << ":" << fn.source_line;
                            std::cout << "\n";
                        }
                } else if (sub == "watches" || sub == "w") {
                    if (watches.empty()) std::cout << "  No watches\n";
                    else for (size_t i = 0; i < watches.size(); ++i)
                        std::cout << "  #" << i << " " << watches[i].expr << "\n";
                } else std::cout << "  Usage: info [breakpoints|functions|watches]\n";
            }
            // --- Advanced ---
            else if (cmd == "dis" || cmd == "disassemble") {
                std::string funcName; iss >> funcName;
                auto chunk = vm.getMainChunk();
                if (!chunk) { std::cout << "  No bytecode chunk\n"; continue; }
                try {
                    havel::compiler::BytecodeDisassembler d(*chunk);
                    havel::compiler::BytecodeDisassembler::Options opts;
                    opts.showLineNumbers = true;
                    opts.showSourceLocations = true;
                    opts.showConstantPool = false;
                    opts.showFunctionInfo = true;
                    if (funcName.empty()) {
                        std::cout << d.disassemble(opts) << "\n";
                    } else {
                        auto* fn = chunk->getFunction(funcName);
                        if (!fn) { std::cout << "  Function not found: " << funcName << "\n"; continue; }
                        size_t idx = 0;
                        for (auto& f : chunk->getAllFunctions()) {
                            if (f.name == funcName) break;
                            idx++;
                        }
                        std::cout << d.disassembleFunction((uint32_t)idx, opts) << "\n";
                    }
                } catch (const std::exception& e) {
                    std::cout << "  Disassemble error: " << e.what() << "\n";
                }
            } else if (cmd == "funcs" || cmd == "functions") {
                auto chunk = vm.getMainChunk();
                if (chunk) for (auto& fn : chunk->getAllFunctions())
                    if (!fn.name.empty())
                        std::cout << "  " << fn.name << " (" << fn.param_count << " params, "
                                  << fn.local_count << " locals, " << fn.instructions.size() << " insns)\n";
            } else if (cmd == "regs" || cmd == "registers") {
                std::cout << "  Frame count: " << vm.frameCountPublic() << "\n"
                          << "  Stack depth: " << vm.stack.size() << "\n"
                          << "  Locals size: " << vm.locals.size() << "\n"
                          << "  Debug step: ";
                switch (vm.debugStepMode()) {
                    case havel::compiler::VM::DebugStepMode::Continue: std::cout << "Continue"; break;
                    case havel::compiler::VM::DebugStepMode::StepInto: std::cout << "StepInto"; break;
                    case havel::compiler::VM::DebugStepMode::StepOver: std::cout << "StepOver"; break;
                    case havel::compiler::VM::DebugStepMode::StepOut: std::cout << "StepOut"; break;
                }
                std::cout << "\n  Debugger: " << (vm.isDebuggerAttached() ? "attached" : "detached") << "\n"
                          << "  Instructions: " << vm.executedInstructionCount() << "\n"
                          << "  JIT: " << (vm.getJITCompiler() ? "enabled" : "none") << "\n"
                          << "  GC suspensions: " << (vm.gcSuspended() ? "yes" : "no") << "\n"
                          << "  Max instructions: " << vm.maxInstructions() << "\n"
                          << "  Memory usage: " << vm.getMemoryUsage() << " bytes\n";
            } else if (cmd == "heap" || cmd == "gc") {
                auto stats = vm.gcStats();
                std::cout << "  Heap size: " << stats.heap_size << " bytes\n"
                          << "  Objects: " << stats.object_count << "\n"
                          << "  Collections: " << stats.collections << "\n"
                          << "  Recovered: " << stats.total_recovered << " bytes\n"
                          << "  Last pause: " << stats.last_pause_ns << " ns\n"
                          << "  Memory: " << vm.getMemoryUsage() << " bytes\n"
                          << "  GC suspended: " << (vm.gcSuspended() ? "yes" : "no") << "\n";
            } else if (cmd == "threads" || cmd == "goroutines") {
                auto* sched = vm.getScheduler();
                if (!sched) { std::cout << "  No scheduler\n"; continue; }
                size_t total = sched->goroutineCount();
                size_t runnable = sched->runnableCount();
                size_t suspended = sched->suspendedCount();
                std::cout << "  Total goroutines: " << total << "\n"
                          << "  Runnable: " << runnable << "\n"
                          << "  Suspended: " << suspended << "\n"
                          << "  Waiting threads: " << vm.getWaitingThreadIds().size() << "\n";
                // Thread IDs
                auto tids = vm.getWaitingThreadIds();
                if (!tids.empty()) {
                    std::cout << "  Waiting threads:";
                    for (auto tid : tids) std::cout << " " << tid;
                    std::cout << "\n";
                }
            } else if (cmd == "hotkeys") {
                auto* sched = vm.getScheduler();
                if (!sched) { std::cout << "  No scheduler\n"; continue; }
                auto aliases = sched->getHotkeyAliases();
                std::cout << "  Total: " << sched->hotkeyCount()
                          << " (active: " << sched->activeHotkeyCount()
                          << ", suspended: " << sched->suspendedHotkeyCount() << ")\n";
                for (auto& a : aliases) std::cout << "    " << a << "\n";
            } else if (cmd == "modules") {
                auto& loader = vm.moduleLoader();
                auto cached = loader.cachedValues();
                auto paths = loader.getSearchPaths();
                std::cout << "  Loader search paths: " << paths.size() << "\n";
                for (auto& p : paths) std::cout << "    " << p << "\n";
                auto cachedList = loader.list();
                std::cout << "  Cached modules: " << cachedList.size() << "\n";
                for (auto& c : cachedList) std::cout << "    " << c << "\n";
            } else {
                std::cout << "Unknown command. Type 'help'.\n";
            }
        }
    });

    vm.setDebugStepMode(havel::compiler::VM::DebugStepMode::StepInto);
    std::cout << "hvdb - Havel Debugger\n";
    std::cout << "Type 'help' for commands, 'run' to start\n";

    // Pre-parse DBG_BREAK comments
    for (size_t i = 0; i < sourceLines.size(); ++i) {
        auto pos = sourceLines[i].find("DBG_BREAK");
        if (pos != std::string::npos) {
            auto after = sourceLines[i].substr(pos + 9);
            std::istringstream as(after);
            std::string targetFile; uint32_t targetLine;
            if (as >> targetLine) {
                uint32_t line32 = (uint32_t)(i + 1);
                vm.setBreakpoint(scriptPath, line32);
                breakpoints.push_back({scriptPath, line32});
                std::cout << "  Auto-breakpoint at line " << (i+1) << "\n";
            } else if (as >> targetFile >> targetLine) {
                vm.setBreakpoint(targetFile, targetLine);
                breakpoints.push_back({targetFile, targetLine});
                std::cout << "  Auto-breakpoint at " << targetFile << ":" << targetLine << "\n";
            }
        }
    }

    try {
        auto result = havel::compiler::runBytecodePipeline(source, "__main__", options);
        if (result.return_value.isNumber())
            return (int)result.return_value.asNumber();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
