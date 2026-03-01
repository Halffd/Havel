#pragma once

#include "havel-lang/lexer/Lexer.hpp"
#include "havel-lang/parser/Parser.h"
#include "utils/Logger.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <mutex>

namespace havel::lsp {

using json = nlohmann::json;

// LSP Types
struct Position {
  int line = 0;         // 0-based
  int character = 0;    // 0-based
};

struct Range {
  Position start;
  Position end;
};

struct Diagnostic {
  Range range;
  int severity = 1;  // 1=Error, 2=Warning, 3=Info, 4=Hint
  std::string message;
  std::string source = "havel";
};

struct TextDocumentItem {
  std::string uri;
  std::string languageId;
  int version = 0;
  std::string text;
};

// Document store
class DocumentStore {
public:
  void open(const TextDocumentItem& doc);
  void update(const std::string& uri, const std::string& text, int version);
  void close(const std::string& uri);
  std::string getText(const std::string& uri) const;
  bool hasDocument(const std::string& uri) const;

private:
  std::unordered_map<std::string, TextDocumentItem> documents;
  mutable std::mutex mutex;
};

// Main LSP Server
class LanguageServer {
public:
  LanguageServer();
  ~LanguageServer();

  // Run the server (reads from stdin, writes to stdout)
  void run();

private:
  // Message handling
  void handleMessage(const json& message);
  
  // LSP method handlers
  json handleInitialize(const json& params);
  json handleInitialized(const json& params);
  json handleShutdown(const json& params);
  void handleExit(const json& params);
  
  // Text document handlers
  void handleDidOpen(const json& params);
  void handleDidChange(const json& params);
  void handleDidClose(const json& params);
  
  // Analysis
  void analyzeDocument(const std::string& uri, const std::string& text);
  std::vector<Diagnostic> getDiagnostics(const std::string& text);
  
  // JSON-RPC helpers
  void sendMessage(const json& message);
  json makeResponse(int id, const json& result);
  json makeError(int id, int code, const std::string& message);
  json makeNotification(const std::string& method, const json& params);
  
  // Position conversion
  Position toLSPPosition(size_t line, size_t column);
  
  // State
  bool initialized = false;
  bool shutdownRequested = false;
  DocumentStore documentStore;
  
  // Capabilities
  json getServerCapabilities();
};

} // namespace havel::lsp
