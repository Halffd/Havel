// src/bootstrap/errors/ErrorSystem.cpp
//
// Unified error reporting system for the entire Havel pipeline.
// All compiler stages (lexer, parser, semantic, bytecode, VM, runtime)
// report through this single infrastructure while maintaining backward
// compatibility with existing error types.

#include "ErrorSystem.h"
#include "../../utils/Logger.hpp"

namespace havel::errors {

ErrorReporter& ErrorReporter::instance() {
    static ErrorReporter inst;
    return inst;
}

void ErrorReporter::report(const HavelError& error) {
    errors_.push_back(error);
    if (error.severity == ErrorSeverity::Error)   errorCount_++;
}

void ErrorReporter::error(ErrorStage stage, const std::string& msg) {
    report(HavelError(ErrorSeverity::Error, stage, msg));
}

void ErrorReporter::errorAt(ErrorStage stage, const std::string& msg, size_t line, size_t col) {
    report(HavelError(ErrorSeverity::Error, stage, msg).at(line, col));
}

void ErrorReporter::warning(ErrorStage stage, const std::string& msg) {
    report(HavelError(ErrorSeverity::Warning, stage, msg));
}

void ErrorReporter::info(ErrorStage stage, const std::string& msg) {
    report(HavelError(ErrorSeverity::Info, stage, msg));
}

const std::vector<HavelError>& ErrorReporter::errors() const {
    return errors_;
}

bool ErrorReporter::hasErrors() const {
    return errorCount_ > 0;
}

size_t ErrorReporter::errorCount() const {
    return errorCount_;
}

void ErrorReporter::clear() {
    errors_.clear();
    errorCount_ = 0;
}

std::string ErrorReporter::format() const {
    std::ostringstream oss;
    for (const auto& e : errors_) {
        oss << e.what() << "\n\n";
    }
    return oss.str();
}

void ErrorReporter::print() const {
    for (const auto& e : errors_) {
        if (e.severity == ErrorSeverity::Error) {
            Logger::error() << e.what();
        } else if (e.severity == ErrorSeverity::Warning) {
            Logger::warn() << e.what();
        } else {
            Logger::info() << e.what();
        }
    }
}

} // namespace havel::errors
