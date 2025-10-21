#include "havel-lang/runtime/Engine.h"
#include "core/IO.hpp"
#include "window/WindowManager.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

int main() {
    try {
        havel::IO io;
        havel::WindowManager wm;
        havel::engine::EngineConfig cfg;
        cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
        cfg.verboseOutput = false;
        cfg.enableProfiler = true;
        havel::engine::Engine engine(io, wm, cfg);

        std::ifstream in("scripts/example.hv");
        if (!in) {
            std::cerr << "Failed to open scripts/example.hv" << std::endl;
            return 2;
        }
        std::stringstream buf; buf << in.rdbuf();
        std::string code = buf.str();

        std::cout << "Running interpreter on scripts/example.hv (length=" << code.size() << ")" << std::endl;
        auto value = engine.ExecuteCode(code);
        (void)value;
        auto stats = engine.GetPerformanceStats();
        std::cout << "Done. Exec time(us)=" << stats.executionTime.count() << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Interpreter error: " << e.what() << std::endl;
        return 1;
    }
}
