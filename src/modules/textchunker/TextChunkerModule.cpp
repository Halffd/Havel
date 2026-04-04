/*
 * TextChunkerModule.cpp - Text chunking/splitting for Havel bytecode VM
 */
#include "TextChunkerModule.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

// ============================================================================
// Helper Functions
// ============================================================================

static std::string toString(const Value &v) {
  if (v.isStringValId()) {
    return "<string>"; // TODO: string pool lookup
  }
  if (v.isInt())
    return std::to_string(v.asInt());
  if (v.isDouble())
    return std::to_string(v.asDouble());
  if (v.isBool())
    return v.asBool() ? "true" : "false";
  return "";
}

static int toInt(const Value &v) {
  if (v.isInt())
    return static_cast<int>(v.asInt());
  if (v.isDouble())
    return static_cast<int>(v.asDouble());
  return 0;
}

// ============================================================================
// Basic Chunking Functions
// ============================================================================

// textChunker.split(text, delimiter) -> array
static Value textChunkerSplit(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 2) {
    return api.makeArray();
  }
  
  std::string text = toString(args[0]);
  std::string delimiter = toString(args[1]);
  
  auto arr = api.makeArray();
  
  if (delimiter.empty()) {
    // Split by characters
    for (char c : text) {
      std::string s(1, c);
      api.push(arr, Value::makeNull()); // TODO: string pool
    }
    return arr;
  }
  
  size_t pos = 0;
  while (pos < text.length()) {
    size_t found = text.find(delimiter, pos);
    if (found == std::string::npos) {
      // Last chunk
      api.push(arr, Value::makeNull()); // TODO: string pool
      break;
    }
    api.push(arr, Value::makeNull()); // TODO: string pool
    pos = found + delimiter.length();
  }
  
  return arr;
}

// textChunker.lines(text) -> array (split by newlines)
static Value textChunkerLines(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 1) {
    return api.makeArray();
  }
  
  std::string text = toString(args[0]);
  auto arr = api.makeArray();
  
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    api.push(arr, Value::makeNull()); // TODO: string pool
  }
  
  return arr;
}

// textChunker.words(text) -> array (split by whitespace)
static Value textChunkerWords(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 1) {
    return api.makeArray();
  }
  
  std::string text = toString(args[0]);
  auto arr = api.makeArray();
  
  std::istringstream stream(text);
  std::string word;
  while (stream >> word) {
    api.push(arr, Value::makeNull()); // TODO: string pool
  }
  
  return arr;
}

// ============================================================================
// Chunking by Size
// ============================================================================

// textChunker.chars(text, count) -> array of character chunks
static Value textChunkerChars(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 2) {
    return api.makeArray();
  }
  
  std::string text = toString(args[0]);
  int count = toInt(args[1]);
  if (count <= 0) count = 1;
  
  auto arr = api.makeArray();
  
  for (size_t i = 0; i < text.length(); i += count) {
    std::string chunk = text.substr(i, count);
    api.push(arr, Value::makeNull()); // TODO: string pool
  }
  
  return arr;
}

// textChunker.fixed(text, size) -> array of fixed-size chunks
static Value textChunkerFixed(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 2) {
    return api.makeArray();
  }
  
  std::string text = toString(args[0]);
  int size = toInt(args[1]);
  if (size <= 0) size = 100;
  
  auto arr = api.makeArray();
  
  for (size_t i = 0; i < text.length(); i += size) {
    std::string chunk = text.substr(i, size);
    api.push(arr, Value::makeNull()); // TODO: string pool
  }
  
  return arr;
}

// ============================================================================
// Smart Chunking
// ============================================================================

// textChunker.sentences(text) -> array (split by sentence endings)
static Value textChunkerSentences(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 1) {
    return api.makeArray();
  }
  
  std::string text = toString(args[0]);
  auto arr = api.makeArray();
  
  size_t pos = 0;
  while (pos < text.length()) {
    size_t found = text.find_first_of(".!?", pos);
    if (found == std::string::npos) {
      if (pos < text.length()) {
        api.push(arr, Value::makeNull()); // TODO: string pool
      }
      break;
    }
    // Include the punctuation
    size_t end = found + 1;
    while (end < text.length() && std::isspace(text[end])) {
      end++;
    }
    api.push(arr, Value::makeNull()); // TODO: string pool
    pos = end;
  }
  
  return arr;
}

// textChunker.paragraphs(text) -> array (split by blank lines)
static Value textChunkerParagraphs(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 1) {
    return api.makeArray();
  }
  
  std::string text = toString(args[0]);
  auto arr = api.makeArray();
  
  std::istringstream stream(text);
  std::string paragraph;
  std::string line;
  
  while (std::getline(stream, line)) {
    if (line.empty()) {
      if (!paragraph.empty()) {
        api.push(arr, Value::makeNull()); // TODO: string pool
        paragraph.clear();
      }
    } else {
      if (!paragraph.empty()) {
        paragraph += "\n";
      }
      paragraph += line;
    }
  }
  
  if (!paragraph.empty()) {
    api.push(arr, Value::makeNull()); // TODO: string pool
  }
  
  return arr;
}

// ============================================================================
// Token/Window Chunking for LLMs
// ============================================================================

// textChunker.tokens(text, maxTokens, overlap) -> array
static Value textChunkerTokens(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 2) {
    return api.makeArray();
  }
  
  std::string text = toString(args[0]);
  int maxTokens = toInt(args[1]);
  int overlap = args.size() > 2 ? toInt(args[2]) : 0;
  
  if (maxTokens <= 0) maxTokens = 512;
  if (overlap < 0) overlap = 0;
  
  auto arr = api.makeArray();
  
  // Simple approximation: ~4 chars per token
  int maxChars = maxTokens * 4;
  int overlapChars = overlap * 4;
  
  size_t pos = 0;
  while (pos < text.length()) {
    size_t chunkSize = std::min(static_cast<size_t>(maxChars), text.length() - pos);
    std::string chunk = text.substr(pos, chunkSize);
    api.push(arr, Value::makeNull()); // TODO: string pool
    
    if (pos + chunkSize >= text.length()) {
      break;
    }
    pos += chunkSize - overlapChars;
  }
  
  return arr;
}

// textChunker.window(text, size, step) -> array of sliding windows
static Value textChunkerWindow(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 2) {
    return api.makeArray();
  }
  
  std::string text = toString(args[0]);
  int windowSize = toInt(args[1]);
  int step = args.size() > 2 ? toInt(args[2]) : 1;
  
  if (windowSize <= 0) windowSize = 100;
  if (step <= 0) step = 1;
  
  auto arr = api.makeArray();
  
  for (size_t i = 0; i + windowSize <= text.length(); i += step) {
    std::string window = text.substr(i, windowSize);
    api.push(arr, Value::makeNull()); // TODO: string pool
  }
  
  return arr;
}

// ============================================================================
// Register TextChunker Module
// ============================================================================

void registerTextChunkerModule(compiler::VMApi &api) {
  // Basic splitting
  api.registerFunction("textChunker.split",
                       [&api](const std::vector<Value> &args) {
                         return textChunkerSplit(api, args);
                       });
  
  api.registerFunction("textChunker.lines",
                       [&api](const std::vector<Value> &args) {
                         return textChunkerLines(api, args);
                       });
  
  api.registerFunction("textChunker.words",
                       [&api](const std::vector<Value> &args) {
                         return textChunkerWords(api, args);
                       });
  
  // Size-based
  api.registerFunction("textChunker.chars",
                       [&api](const std::vector<Value> &args) {
                         return textChunkerChars(api, args);
                       });
  
  api.registerFunction("textChunker.fixed",
                       [&api](const std::vector<Value> &args) {
                         return textChunkerFixed(api, args);
                       });
  
  // Smart chunking
  api.registerFunction("textChunker.sentences",
                       [&api](const std::vector<Value> &args) {
                         return textChunkerSentences(api, args);
                       });
  
  api.registerFunction("textChunker.paragraphs",
                       [&api](const std::vector<Value> &args) {
                         return textChunkerParagraphs(api, args);
                       });
  
  // LLM token chunking
  api.registerFunction("textChunker.tokens",
                       [&api](const std::vector<Value> &args) {
                         return textChunkerTokens(api, args);
                       });
  
  api.registerFunction("textChunker.window",
                       [&api](const std::vector<Value> &args) {
                         return textChunkerWindow(api, args);
                       });
  
  // Register global 'textChunker' object
  auto chunkerObj = api.makeObject();
  
  api.setField(chunkerObj, "split", api.makeFunctionRef("textChunker.split"));
  api.setField(chunkerObj, "lines", api.makeFunctionRef("textChunker.lines"));
  api.setField(chunkerObj, "words", api.makeFunctionRef("textChunker.words"));
  api.setField(chunkerObj, "chars", api.makeFunctionRef("textChunker.chars"));
  api.setField(chunkerObj, "fixed", api.makeFunctionRef("textChunker.fixed"));
  api.setField(chunkerObj, "sentences", api.makeFunctionRef("textChunker.sentences"));
  api.setField(chunkerObj, "paragraphs", api.makeFunctionRef("textChunker.paragraphs"));
  api.setField(chunkerObj, "tokens", api.makeFunctionRef("textChunker.tokens"));
  api.setField(chunkerObj, "window", api.makeFunctionRef("textChunker.window"));
  
  api.setGlobal("textChunker", chunkerObj);
}

} // namespace havel::modules
