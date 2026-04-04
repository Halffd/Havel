#include "ErrorPrinter.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace havel {

void ErrorPrinter::printError(const std::string& errorType,
                              const std::string& message,
                              const std::string& filename,
                              size_t line,
                              size_t column,
                              size_t length,
                              const std::string& sourceLineContent) {
  // e.g. Error: undefined variable 'foo'
  std::cerr << "\033[1;31m" << errorType << "\033[0m: " << message << "\n";
  
  //   --> file.hv:12:8
  std::cerr << "  --> \033[36m" << filename << ":" << line << ":" << column << "\033[0m\n";
  
  if (sourceLineContent.empty() && line > 0) {
    return; // Cannot print snippet without content
  }

  // Pre-formatting line number
  std::string lineStr = std::to_string(line);
  std::string padding(lineStr.length(), ' ');

  //    |
  std::cerr << " " << padding << " |\n";
  
  // 12 |   let x = foo + 1
  std::cerr << " " << lineStr << " | " << sourceLineContent << "\n";
  
  //    |           ^^^
  size_t carets = std::max<size_t>(1, length);
  size_t spaces = column > 1 ? column - 1 : 0;
  
  std::cerr << " " << padding << " | " << std::string(spaces, ' ') << "\033[1;31m" << std::string(carets, '^') << "\033[0m\n";
}

void ErrorPrinter::printErrorFromFile(const std::string& errorType,
                                      const std::string& message,
                                      const std::string& filename,
                                      size_t line,
                                      size_t column,
                                      size_t length) {
  std::string sourceLineContent = readLineFromFile(filename, line);
  printError(errorType, message, filename, line, column, length, sourceLineContent);
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
