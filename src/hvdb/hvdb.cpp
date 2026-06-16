#include "havel-lang/compiler/core/Pipeline.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
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

struct FuncBreakpointEntry {
    std::string name;
};

struct WatchEntry {
    std::string expr;
    havel::compiler::Value last_val;
};

static void printHelp() {
    std::cout << "hvdb commands:\n"
              << "  break <file:line>         Set line breakpoint\n"
              << "  break <func>              Set function entry breakpoint\n"
              << "  break list                List all breakpoints\n"
              << "  break clear <n>           Clear breakpoint N\n"
              << "  catch throw               Break on any exception throw\n"
              << "  catch uncaught            Break only on uncaught exceptions\n"
              << "  catch                     Show catch settings\n"
              << "  watch <expr>              Watch expression for changes\n"
              << "  watch list                List watch expressions\n"
              << "  watch clear <n>           Clear watch N\n"
              << "  run                       Start/continue execution\n"
              << "  step                      Step into next line\n"
              << "  next                      Step over\n"
              << "  fin                       Step out of current function\n"
              << "  continue                  Continue execution\n"
              << "  list [n]                  Show source lines around line n\n"
              << "  print <expr>              Evaluate expression\n"
              << "  locals                    Show local variables\n"
              << "  globals                   Show global variables\n"
              << "  stack                     Show call stack\n"
              << "  frame <n>                 Select frame N\n"
              << "  info functions            List known functions in script\n"
              << "  quit                      Exit debugger\n"
              << "  help                      Show this help\n";
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
        while (std::getline(stream, line)) {
            sourceLines.push_back(line);
        }
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
            if (args[i].isStringValId() || args[i].isStringId()) {
                std::cout << vm.resolveStringKey(args[i]);
            } else {
                std::cout << args[i].toString();
            }
        }
        std::cout << std::endl;
        return havel::compiler::Value::makeNull();
    };

    std::vector<BreakpointEntry> breakpoints;
    std::vector<FuncBreakpointEntry> funcBreakpoints;
    std::vector<WatchEntry> watches;
    int selected_frame = -1;
    bool running = false;
    bool exception_break = false;

    vm.attachDebugger();

    vm.setDebugBreakCallback([&]() {
        debugger_continue = false;
        running = true;

        auto info = vm.getCurrentFrameInfo();
        auto excVal = vm.currentExceptionValue();

        if (!excVal.isNull()) {
            std::cout << "\nException thrown: " << vm.toString(excVal);
        } else if (info.function_name.empty() ||
                   info.function_name.rfind("__main__", 0) == std::string::npos) {
            std::cout << "\nBreakpoint hit";
        } else {
            std::cout << "\nBreakpoint hit";
        }

        if (!info.source_file.empty()) {
            std::cout << " at " << info.source_file << ":" << info.line;
        }
        if (!info.function_name.empty()) {
            std::cout << " in " << info.function_name;
        }
        std::cout << std::endl;

        if (info.line > 0 && info.line <= sourceLines.size()) {
            std::cout << "  " << info.line << ": " << sourceLines[info.line - 1] << "\n";
        }

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
                std::cout << "Exiting debugger." << std::endl;
                _Exit(0);
            } else if (cmd == "help" || cmd == "h" || cmd == "?") {
                printHelp();
            } else if (cmd == "run" || cmd == "r") {
                debugger_continue = true;
            } else if (cmd == "continue" || cmd == "c") {
                vm.setDebugStepMode(havel::compiler::VM::DebugStepMode::Continue);
                debugger_continue = true;
            } else if (cmd == "step" || cmd == "s") {
                vm.setDebugStepMode(havel::compiler::VM::DebugStepMode::StepInto);
                debugger_continue = true;
            } else if (cmd == "next" || cmd == "n") {
                auto frames = vm.getStackFrames();
                vm.setDebugStepMode(havel::compiler::VM::DebugStepMode::StepOver);
                vm.setDebugStepFrameDepth(frames.empty() ? 0 : frames.back().frame_depth);
                debugger_continue = true;
            } else if (cmd == "fin" || cmd == "f") {
                auto frames = vm.getStackFrames();
                vm.setDebugStepMode(havel::compiler::VM::DebugStepMode::StepOut);
                vm.setDebugStepFrameDepth(frames.empty() ? 0 : frames.back().frame_depth);
                debugger_continue = true;
            } else if (cmd == "locals") {
                auto vars = vm.getLocals(selected_frame);
                if (vars.empty()) {
                    std::cout << "  (no locals)\n";
                } else {
                    for (auto& v : vars) {
                        std::cout << "  " << v.name << " : " << v.type << " = " << v.value << "\n";
                    }
                }
            } else if (cmd == "globals") {
                auto vars = vm.getDebugGlobals();
                size_t count = 0;
                for (auto& v : vars) {
                    if (count >= 50) { std::cout << "  ... (truncated)\n"; break; }
                    std::cout << "  " << v.name << " : " << v.type << " = " << v.value << "\n";
                    count++;
                }
                if (vars.empty()) std::cout << "  (no globals)\n";
            } else if (cmd == "stack" || cmd == "bt") {
                auto frames = vm.getStackFrames();
                if (frames.empty()) {
                    std::cout << "  (empty stack)\n";
                } else {
                    for (size_t i = 0; i < frames.size(); ++i) {
                        auto& f = frames[i];
                        std::cout << "  #" << i << " ";
                        if (static_cast<int>(i) == selected_frame) std::cout << "=> ";
                        else std::cout << "   ";
                        if (!f.function_name.empty()) std::cout << f.function_name;
                        else std::cout << "<top-level>";
                        if (!f.source_file.empty()) {
                            std::cout << " at " << f.source_file << ":" << f.line;
                        }
                        std::cout << "\n";
                    }
                }
            } else if (cmd == "frame" || cmd == "fr") {
                std::string arg;
                iss >> arg;
                if (arg.empty()) {
                    std::cout << "  Current frame: " << selected_frame << "\n";
                } else {
                    int n = std::stoi(arg);
                    auto frames = vm.getStackFrames();
                    if (n < 0 || static_cast<size_t>(n) >= frames.size()) {
                        std::cout << "  Invalid frame number\n";
                    } else {
                        selected_frame = n;
                        auto& f = frames[n];
                        std::cout << "  Switched to frame #" << n;
                        if (!f.function_name.empty()) std::cout << " " << f.function_name;
                        std::cout << "\n";
                    }
                }
            } else if (cmd == "print" || cmd == "p") {
                std::string expr;
                std::getline(iss >> std::ws, expr);
                if (!expr.empty()) {
                    auto val = vm.evaluateInFrame(expr, selected_frame);
                    std::cout << "  " << expr << " = " << vm.toString(val) << "\n";
                }
            } else if (cmd == "list") {
                std::string arg;
                iss >> arg;
                uint32_t center = 0;
                if (!arg.empty()) {
                    center = std::stoul(arg);
                } else {
                    auto info = vm.getCurrentFrameInfo();
                    center = info.line;
                }
                if (center == 0) center = 1;
                uint32_t start = center > 5 ? center - 5 : 1;
                uint32_t end = std::min(center + 5, (uint32_t)sourceLines.size());
                for (uint32_t i = start; i <= end; ++i) {
                    std::string marker = (i == center) ? "=>" : "  ";
                    std::cout << "  " << marker << " " << i << ": " << sourceLines[i - 1] << "\n";
                }
            } else if (cmd == "delete" || cmd == "d") {
                std::string arg;
                iss >> arg;
                if (arg == "all" || arg.empty()) {
                    vm.clearAllBreakpoints();
                    vm.clearAllFunctionBreakpoints();
                    vm.setBreakOnThrow(false);
                    vm.setBreakOnUncaught(false);
                    breakpoints.clear();
                    funcBreakpoints.clear();
                    watches.clear();
                    std::cout << "  Cleared all breakpoints\n";
                }
            } else if (cmd == "catch") {
                std::string sub;
                iss >> sub;
                if (sub == "throw") {
                    vm.setBreakOnThrow(true);
                    std::cout << "  Will break on any exception throw\n";
                } else if (sub == "uncaught") {
                    vm.setBreakOnUncaught(true);
                    std::cout << "  Will break on uncaught exceptions\n";
                } else {
                    std::cout << "  catch throw: " << (vm.breakOnThrow() ? "on" : "off") << "\n";
                    std::cout << "  catch uncaught: " << (vm.breakOnUncaught() ? "on" : "off") << "\n";
                }
            } else if (cmd == "watch") {
                std::string sub;
                std::getline(iss >> std::ws, sub);
                if (sub == "list") {
                    if (watches.empty()) {
                        std::cout << "  No watch expressions\n";
                    } else {
                        for (size_t i = 0; i < watches.size(); ++i) {
                            std::cout << "  #" << i << " " << watches[i].expr << "\n";
                        }
                    }
                } else if (sub.rfind("clear", 0) == 0) {
                    std::string arg = sub.substr(5);
                    if (!arg.empty()) {
                        int n = std::stoi(arg);
                        if (n >= 0 && static_cast<size_t>(n) < watches.size()) {
                            watches.erase(watches.begin() + n);
                            std::cout << "  Cleared watch #" << n << "\n";
                        } else {
                            std::cout << "  Invalid watch number\n";
                        }
                    }
                } else if (!sub.empty()) {
                    watches.push_back({sub, havel::compiler::Value::makeNull()});
                    std::cout << "  Watch expression: " << sub << "\n";
                }
            } else if (cmd == "info") {
                std::string sub;
                iss >> sub;
                if (sub == "breakpoints" || sub == "b") {
                    if (breakpoints.empty() && funcBreakpoints.empty()) {
                        std::cout << "  No breakpoints\n";
                    } else {
                        size_t n = 0;
                        for (auto& bp : breakpoints) {
                            std::cout << "  #" << n << " break " << bp.file << ":" << bp.line << "\n";
                            n++;
                        }
                        for (auto& fbp : funcBreakpoints) {
                            std::cout << "  #" << n << " break " << fbp.name << " (function)\n";
                            n++;
                        }
                    }
                } else if (sub == "functions" || sub == "func") {
                    if (vm.getMainChunk()) {
                        for (auto& fn : vm.getMainChunk()->getAllFunctions()) {
                            if (!fn.name.empty()) {
                                std::cout << "  " << fn.name;
                                if (!fn.source_file.empty())
                                    std::cout << " at " << fn.source_file << ":" << fn.source_line;
                                std::cout << "\n";
                            }
                        }
                    }
                } else if (sub == "watches" || sub == "w") {
                    if (watches.empty()) std::cout << "  No watch expressions\n";
                    else {
                        for (size_t i = 0; i < watches.size(); ++i)
                            std::cout << "  #" << i << " " << watches[i].expr << "\n";
                    }
                }
            } else if (cmd == "break" || cmd == "b") {
                std::string sub;
                iss >> sub;
                if (sub == "list") {
                    if (breakpoints.empty()) {
                        std::cout << "  No breakpoints set\n";
                    } else {
                        for (size_t i = 0; i < breakpoints.size(); ++i) {
                            std::cout << "  #" << i << " " << breakpoints[i].file << ":" << breakpoints[i].line << "\n";
                        }
                    }
                } else if (sub == "clear") {
                    std::string arg;
                    iss >> arg;
                    if (arg.empty()) {
                        std::cout << "  Usage: break clear <n>\n";
                    } else {
                        int n = std::stoi(arg);
                        if (n < 0 || static_cast<size_t>(n) >= breakpoints.size()) {
                            std::cout << "  Invalid breakpoint number\n";
                        } else {
                            vm.clearBreakpoint(breakpoints[n].file, breakpoints[n].line);
                            std::cout << "  Cleared breakpoint #" << n << "\n";
                            breakpoints.erase(breakpoints.begin() + n);
                        }
                    }
                } else if (!sub.empty()) {
                    auto colon = sub.find(':');
                    if (colon != std::string::npos) {
                        std::string fname = sub.substr(0, colon);
                        uint32_t lnum = std::stoul(sub.substr(colon + 1));
                        if (lnum == 0) {
                            std::cout << "  Invalid line number\n";
                        } else {
                            vm.setBreakpoint(fname, lnum);
                            breakpoints.push_back({fname, lnum});
                            std::cout << "  Breakpoint set at " << fname << ":" << lnum << "\n";
                        }
                    } else if (sub.find_first_not_of("0123456789") == std::string::npos) {
                        uint32_t lnum = std::stoul(sub);
                        vm.setBreakpoint(scriptPath, lnum);
                        breakpoints.push_back({scriptPath, lnum});
                        std::cout << "  Breakpoint set at " << scriptPath << ":" << lnum << "\n";
                    } else {
                        vm.setFunctionBreakpoint(sub);
                        funcBreakpoints.push_back({sub});
                        std::cout << "  Function breakpoint set: " << sub << "\n";
                    }
                } else {
                    std::cout << "  Usage: break <file:line> | break <func> | break list | break clear <n>\n";
                }
            } else {
                std::cout << "Unknown command: " << cmd << " (type 'help' for commands)\n";
            }
        }
    });

    vm.setDebugStepMode(havel::compiler::VM::DebugStepMode::StepInto);

    std::cout << "hvdb - Havel Debugger\n";
    std::cout << "Type 'help' for commands, 'run' to start execution\n";

    // Pre-parse breakpoints from comments: # DBG_BREAK <line>
    {
        for (size_t i = 0; i < sourceLines.size(); ++i) {
            auto pos = sourceLines[i].find("DBG_BREAK");
            if (pos != std::string::npos) {
                auto after = sourceLines[i].substr(pos + 9);
                std::istringstream as(after);
                std::string targetFile;
                uint32_t targetLine;
                if (as >> targetLine) {
                    uint32_t line32 = static_cast<uint32_t>(i + 1);
                    vm.setBreakpoint(scriptPath, line32);
                    breakpoints.push_back({scriptPath, line32});
                    std::cout << "  Auto-breakpoint at line " << (i + 1) << "\n";
                } else if (as >> targetFile >> targetLine) {
                    vm.setBreakpoint(targetFile, targetLine);
                    breakpoints.push_back({targetFile, targetLine});
                    std::cout << "  Auto-breakpoint at " << targetFile << ":" << targetLine << "\n";
                }
            }
        }
    }

    try {
        auto result = havel::compiler::runBytecodePipeline(source, "__main__", options);

        if (result.return_value.isNumber()) {
            return static_cast<int>(result.return_value.asNumber());
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
