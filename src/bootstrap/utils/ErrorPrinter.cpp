#include "ErrorPrinter.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace havel {

std::string ErrorPrinter::formatError(const std::string& errorType,
                                      const std::string& message,
                                      const std::string& filename,
                                      size_t line,
                                      size_t column,
                                      size_t length,
                                      const std::string& sourceLineContent) {
  std::ostringstream out;

  // e.g. \033[1;31mError\033[0m: undefined variable 'foo'
  out << "\033[1;31m" << errorType << "\033[0m: " << message << "\n";

  if (sourceLineContent.empty() && line > 0) {
    // Still show location even without source line
    out << "  --> \033[36m" << filename << ":" << line << ":" << column << "\033[0m\n";
    return out.str();
  }

  // Pre-formatting line number
  std::string lineStr = std::to_string(line);
  std::string padding(lineStr.length(), ' ');

  //   --> file.hv:12:8
  out << "  --> \033[36m" << filename << ":" << line << ":" << column << "\033[0m\n";

  //    |
  out << "   |\n";

  // 12 |   let x = foo + 1
  out << " " << lineStr << " | " << sourceLineContent << "\n";

  //    |           ^^^
  size_t carets = std::max<size_t>(1, length);
  size_t spaces = column > 1 ? column - 1 : 0;

  out << "   | " << std::string(spaces, ' ') << "\033[1;31m" << std::string(carets, '^') << "\033[0m\n";

  return out.str();
}

std::string ErrorPrinter::formatErrorFromFile(const std::string& errorType,
                                              const std::string& message,
                                              const std::string& filename,
                                              size_t line,
                                              size_t column,
                                              size_t length) {
  std::string sourceLineContent = readLineFromFile(filename, line);
  return formatError(errorType, message, filename, line, column, length, sourceLineContent);
}

std::string ErrorPrinter::readLineFromFile(const std::string& filename, size_t lineNum) {
  if (filename.empty() || filename == "<memory>" || filename == "<stdin>") {
    return "";
  }

  std::ifstream file(filename);
  if (!file.is_open()) {
    return "";
  }

  std::string lineContent;
  for (size_t i = 0; i < lineNum; ++i) {
    if (!std::getline(file, lineContent)) {
      return "";
    }
  }
  return lineContent;
}

} // namespace havel
