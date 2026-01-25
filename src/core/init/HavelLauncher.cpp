#include "HavelLauncher.hpp"
#include "gui/HavelApp.hpp"
#include "utils/Logger.hpp"
#include "havel-lang/common/Debug.hpp"
#include "havel-lang/lexer/Lexer.hpp"
#include "havel-lang/parser/Parser.h"
#include "havel-lang/runtime/Interpreter.hpp"
#include "havel-lang/runtime/Engine.h"
#include "core/IO.hpp"
#include "window/WindowManager.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

using namespace havel;

namespace havel::init {

int HavelLauncher::run(int argc, char* argv[]) {
    try {
        LaunchConfig cfg = parseArgs(argc, argv);
        
        switch(cfg.mode) {
            case Mode::DAEMON:
                return runDaemon(cfg, argc, argv);
            case Mode::GUI_ONLY:
                return runGuiOnly(cfg, argc, argv);
            case Mode::SCRIPT:
                return runScript(cfg);
            case Mode::REPL:
                return runRepl(cfg);
            case Mode::CLI:
                return runCli(argc, argv);
            default:
                error("Unknown mode");
                return 1;
        }
    } catch (const std::exception& e) {
        error("Fatal error: {}", e.what());
        return 1;
    }
}

HavelLauncher::LaunchConfig HavelLauncher::parseArgs(int argc, char* argv[]) {
    LaunchConfig cfg;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--startup" || arg == "-s") {
            cfg.isStartup = true;
        } else if (arg == "--debug" || arg == "-d") {
            cfg.debugMode = true;
            Logger::getInstance().setLogLevel(Logger::LOG_DEBUG);
        } else if (arg == "--debug-parser" || arg == "-dp") {
            debugging::debug_parser = true;
            cfg.debugParser = true;
        } else if (arg == "--debug-ast" || arg == "-da") {
            debugging::debug_ast = true;
            cfg.debugAst = true;
        } else if (arg == "--debug-lexer" || arg == "-dl") {
            debugging::debug_lexer = true;
            cfg.debugLexer = true;
        } else if (arg == "--repl" || arg == "-r") {
            cfg.mode = Mode::REPL;
        } else if (arg == "--gui") {
            cfg.mode = Mode::GUI_ONLY;
        } else if (arg == "--help" || arg == "-h") {
            showHelp();
            exit(0);
        } else if (arg == "lexer") {
            cfg.mode = Mode::CLI;
            return cfg;
        } else {
            // Assume it's a script file
            if (!cfg.scriptFile.empty()) {
                error("Error: Only one script file can be provided. Got {} and {}", 
                      cfg.scriptFile, arg);
                exit(1);
            }
            if (!arg.ends_with(".hv")) {
                warning("Script file {} does not end with .hv extension", arg);
            }
            cfg.scriptFile = arg;
            cfg.mode = Mode::SCRIPT;
        }
    }
    
    return cfg;
}

int HavelLauncher::runDaemon(const LaunchConfig& cfg, int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("havel");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("havel");
    app.setQuitOnLastWindowClosed(false);

    HavelApp havelApp(cfg.isStartup);
    
    if (!havelApp.isInitialized()) {
        error("Failed to initialize HavelApp");
        return 1;
    }
    
    info("Havel started successfully - running in system tray");
    return app.exec();
}

int HavelLauncher::runGuiOnly(const LaunchConfig& cfg, int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Havel GUI");
    app.setQuitOnLastWindowClosed(false);
    
    HavelApp havelApp(cfg.isStartup);
    
    if (!havelApp.isInitialized()) {
        error("Failed to initialize HavelApp");
        return 1;
    }
    
    info("Havel GUI started");
    return app.exec();
}

int HavelLauncher::runScript(const LaunchConfig& cfg) {
    info("Running Havel script: {}", cfg.scriptFile);
    
    // Read script file
    std::ifstream file(cfg.scriptFile);
    if (!file) {
        error("Cannot open script file: {}", cfg.scriptFile);
        return 2;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string code = buffer.str();
    
    int dummy_argc = 1;
    char dummy_name[] = "havel-script";
    char* dummy_argv[] = { dummy_name, nullptr };
    QApplication app(dummy_argc, dummy_argv);
    
    HavelApp havelApp(false, cfg.scriptFile);  // Don't show GUI
    
    if (!havelApp.isInitialized()) {
        error("Failed to initialize HavelApp");
        return 1;
    }
    
    // Use HavelApp's interpreter
    auto* interpreter = havelApp.getInterpreter();
    if (!interpreter) {
        error("Interpreter is not available");
        return 1;
    }
    auto result = interpreter->Execute(code);
    
    if (std::holds_alternative<havel::HavelRuntimeError>(result)) {
        error("Runtime Error: {}", std::get<havel::HavelRuntimeError>(result).what());
        return 1;
    }
    
    // Check if script contains hotkeys
    std::regex hotkeyPattern(R"(\b[A-Za-z0-9+^!#@~*$]+\s*=>)");
    bool hasHotkeys = std::regex_search(code, hotkeyPattern);
    
    if (hasHotkeys) {
        havelApp.hotkeyManager->printHotkeys();
        havelApp.hotkeyManager->updateAllConditionalHotkeys();
        info("Script loaded. Hotkeys registered. Press Ctrl+C to exit.");
        return app.exec();  // Run Qt event loop
    }
    
    return 0;
}

int HavelLauncher::runRepl(const LaunchConfig& cfg) {
    info("Starting Havel REPL...");
    int dummy_argc = 1;
    char dummy_name[] = "havel-script";
    char* dummy_argv[] = { dummy_name, nullptr };
    QApplication app(dummy_argc, dummy_argv);
    
    HavelApp havelApp(false, "", true);
    
    if (!havelApp.isInitialized()) {
        error("Failed to initialize HavelApp");
        return 1;
    }
    
    // Use HavelApp's interpreter
    auto* interpreter = havelApp.getInterpreter();
    
    std::cout << "Havel Language REPL v1.0\n";
    std::cout << "Type 'exit' or 'quit' to exit, 'help' for help\n\n";
    
    std::string line;
    std::string multiline;
    int braceCount = 0;
    
    // REPL log file
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : std::string(".");
    std::string logPath = home + "/.havel_repl.log";
    std::ofstream replLog(logPath, std::ios::app);
    
    while (true) {
        std::string prompt = (braceCount > 0) ? "... " : ">>> ";
        
#ifdef HAVE_READLINE
        char* input = readline(prompt.c_str());
        if (!input) break;
        line = std::string(input);
        free(input);
        if (!line.empty()) add_history(line.c_str());
#else
        std::cout << prompt;
        if (!std::getline(std::cin, line)) break;
#endif
        
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) {
            if (braceCount == 0) continue;
            line = "";
        } else {
            line = line.substr(start);
        }
        
        // Log input
        if (replLog.is_open()) {
            replLog << line << '\n';
            replLog.flush();
        }
        
        // Commands
        if (braceCount == 0) {
            if (line == "exit" || line == "quit") {
                std::cout << "Goodbye!\n";
                return 0;
            }
            if (line == "help") {
                std::cout << "Available commands:\n";
                std::cout << "  exit, quit  - Exit REPL\n";
                std::cout << "  help        - Show this help\n";
                std::cout << "  clear       - Clear screen\n";
                std::cout << "\nType any Havel expression or statement to evaluate.\n";
                continue;
            }
            if (line == "clear") {
                std::cout << "\033[2J\033[1;1H";
                continue;
            }
            if (line.empty()) {
                continue;
            }
        }
        
        // Track braces
        for (char c : line) {
            if (c == '{') braceCount++;
            else if (c == '}') braceCount--;
        }
        
        multiline += line + "\n";
        
        // Execute when balanced
        if (braceCount == 0 && !multiline.empty()) {
            try {
                if (interpreter) {
                    auto result = interpreter->Execute(multiline);

                    if (std::holds_alternative<havel::HavelValue>(result)) {
                        auto val = std::get<havel::HavelValue>(result);
                        if (!std::holds_alternative<std::nullptr_t>(val)) {
                            std::cout << "=> " << havel::Interpreter::ValueToString(val) << "\n";
                        }
                    } else if (std::holds_alternative<havel::HavelRuntimeError>(result)) {
                        std::cerr << "Error: " << std::get<havel::HavelRuntimeError>(result).what() << "\n";
                    }
                } else {
                    std::cerr << "Error: Interpreter is not available\n";
                }
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << "\n";
            }
            
            multiline.clear();
        }
    }
    
    return 0;
}

int HavelLauncher::runCli(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "usage: havel lexer script.hv\n";
        return 2;
    }

    const std::string subcommand = argv[1];
    if (subcommand != "lexer") {
        std::cerr << "Unknown command: " << subcommand << "\n";
        std::cerr << "usage: havel lexer script.hv\n";
        return 2;
    }

    const std::string filePath = argv[2];

    auto readFile = [](const std::string& path) -> std::string {
        std::ifstream file(path);
        if (!file) {
            throw std::runtime_error("Cannot open script file: " + path);
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    };

    auto getLine = [](const std::string& source, size_t oneBasedLine) -> std::string {
        if (oneBasedLine == 0) return "";
        size_t currentLine = 1;
        size_t start = 0;
        while (start < source.size() && currentLine < oneBasedLine) {
            size_t nl = source.find('\n', start);
            if (nl == std::string::npos) return "";
            start = nl + 1;
            currentLine++;
        }
        if (start >= source.size()) return "";
        size_t end = source.find('\n', start);
        if (end == std::string::npos) end = source.size();
        return source.substr(start, end - start);
    };

    auto printDiagnostic = [&](const std::string& kind, size_t line, size_t column, const std::string& message, const std::string& source) {
        auto countLines = [](const std::string& s) -> size_t {
            if (s.empty()) return 1;
            size_t count = 1;
            for (char c : s) {
                if (c == '\n') ++count;
            }
            return count;
        };

        const size_t totalLines = countLines(source);
        const size_t safeLine = (line == 0 ? 1 : (line > totalLines ? totalLines : line));
        const size_t safeColumn = (column == 0 ? 1 : column);

        std::cerr << filePath << ":" << safeLine << ":" << safeColumn << ": " << kind << ": " << message << "\n";

        std::string srcLine = getLine(source, safeLine);
        if (safeLine >= 1 && safeLine <= totalLines) {
            std::cerr << srcLine << "\n";

            size_t caretCol = safeColumn;
            if (column == 0) {
                caretCol = srcLine.size() + 1;
            }
            if (caretCol < 1) caretCol = 1;
            if (caretCol > srcLine.size() + 1) caretCol = srcLine.size() + 1;

            for (size_t i = 1; i < caretCol; ++i) std::cerr << ' ';
            std::cerr << "^\n";
        }
    };

    auto prettify = [](const std::vector<havel::Token>& tokens) -> std::string {
        auto needsSpaceBefore = [](havel::TokenType t) {
            return t == havel::TokenType::Identifier || t == havel::TokenType::Number || t == havel::TokenType::String ||
                   t == havel::TokenType::InterpolatedString || t == havel::TokenType::Hotkey;
        };
        auto needsSpaceAround = [](havel::TokenType t) {
            return t == havel::TokenType::Plus || t == havel::TokenType::Minus || t == havel::TokenType::Multiply ||
                   t == havel::TokenType::Divide || t == havel::TokenType::Modulo || t == havel::TokenType::Equals ||
                   t == havel::TokenType::NotEquals || t == havel::TokenType::Less || t == havel::TokenType::Greater ||
                   t == havel::TokenType::LessEquals || t == havel::TokenType::GreaterEquals || t == havel::TokenType::And ||
                   t == havel::TokenType::Or || t == havel::TokenType::Assign || t == havel::TokenType::PlusAssign ||
                   t == havel::TokenType::MinusAssign || t == havel::TokenType::MultiplyAssign || t == havel::TokenType::DivideAssign ||
                   t == havel::TokenType::Arrow || t == havel::TokenType::Pipe || t == havel::TokenType::DotDot;
        };

        std::string out;
        havel::TokenType prev = havel::TokenType::EOF_TOKEN;

        for (const auto& tok : tokens) {
            if (tok.type == havel::TokenType::EOF_TOKEN) break;

            if (tok.type == havel::TokenType::NewLine) {
                while (!out.empty() && out.back() == ' ') out.pop_back();
                out += "\n";
                prev = tok.type;
                continue;
            }

            if (tok.type == havel::TokenType::Comma) {
                while (!out.empty() && out.back() == ' ') out.pop_back();
                out += ", ";
                prev = tok.type;
                continue;
            }

            if (tok.type == havel::TokenType::Semicolon) {
                while (!out.empty() && out.back() == ' ') out.pop_back();
                out += ";\n";
                prev = tok.type;
                continue;
            }

            if (tok.type == havel::TokenType::CloseParen || tok.type == havel::TokenType::CloseBracket || tok.type == havel::TokenType::CloseBrace) {
                while (!out.empty() && out.back() == ' ') out.pop_back();
            }

            bool insertSpace = false;
            if (!out.empty() && out.back() != '\n') {
                if (needsSpaceBefore(tok.type) && (needsSpaceBefore(prev) || prev == havel::TokenType::CloseParen || prev == havel::TokenType::CloseBracket)) {
                    insertSpace = true;
                }
                if (tok.type == havel::TokenType::OpenBrace && prev != havel::TokenType::NewLine && prev != havel::TokenType::OpenBrace) {
                    insertSpace = true;
                }
            }

            if (insertSpace) out += ' ';

            if (needsSpaceAround(tok.type)) {
                while (!out.empty() && out.back() == ' ') out.pop_back();
                if (!out.empty() && out.back() != '\n') out += ' ';
                out += tok.raw;
                out += ' ';
            } else {
                out += tok.raw;
            }

            prev = tok.type;
        }

        while (!out.empty() && out.back() == ' ') out.pop_back();
        if (!out.empty() && out.back() != '\n') out += '\n';
        return out;
    };

    try {
        const std::string source = readFile(filePath);

        havel::Lexer lexer(source);
        std::vector<havel::Token> tokens = lexer.tokenize();

        havel::parser::Parser parser;
        (void)parser.produceASTStrict(source);

        std::cout << prettify(tokens);
        return 0;
    } catch (const havel::LexError& e) {
        printDiagnostic("lex error", e.line, e.column, e.what(), readFile(filePath));
        return 1;
    } catch (const havel::parser::ParseError& e) {
        printDiagnostic("parse error", e.line, e.column, e.what(), readFile(filePath));
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}

void HavelLauncher::showHelp() {
    std::cout << "Usage: havel [script.hv] [options]\n";
    std::cout << "       havel lexer script.hv\n";
    std::cout << "Options:\n";
    std::cout << "  --startup, -s       Run at system startup\n";
    std::cout << "  --debug, -d         Enable debug logging\n";
    std::cout << "  --debug-parser, -dp Enable parser debugging\n";
    std::cout << "  --debug-ast, -da    Enable AST debugging\n";
    std::cout << "  --debug-lexer, -dl  Enable lexer debugging\n";
    std::cout << "  --repl, -r          Start interactive REPL\n";
    std::cout << "  --gui               GUI-only mode (no hotkeys)\n";
    std::cout << "  --help, -h          Show this help\n";
    std::cout << "\nIf a .hv script file is provided, it will be executed.\n";
    std::cout << "If no script is provided, the GUI tray application starts.\n";
}

} // namespace havel::init