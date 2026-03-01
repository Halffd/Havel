#include "LanguageServer.hpp"
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <algorithm>

namespace havel::lsp {

// ============ DocumentStore ============

void DocumentStore::open(const TextDocumentItem& doc) {
  std::lock_guard<std::mutex> lock(mutex);
  documents[doc.uri] = doc;
}

void DocumentStore::update(const std::string& uri, const std::string& text, int version) {
  std::lock_guard<std::mutex> lock(mutex);
  auto it = documents.find(uri);
  if (it != documents.end()) {
    it->second.text = text;
    it->second.version = version;
  }
}

void DocumentStore::close(const std::string& uri) {
  std::lock_guard<std::mutex> lock(mutex);
  documents.erase(uri);
}

std::string DocumentStore::getText(const std::string& uri) const {
  std::lock_guard<std::mutex> lock(mutex);
  auto it = documents.find(uri);
  if (it != documents.end()) {
    return it->second.text;
  }
  return "";
}

bool DocumentStore::hasDocument(const std::string& uri) const {
  std::lock_guard<std::mutex> lock(mutex);
  return documents.find(uri) != documents.end();
}

// ============ LanguageServer ============

LanguageServer::LanguageServer() = default;
LanguageServer::~LanguageServer() = default;

void LanguageServer::run() {
  std::string line;
  std::string content;
  int contentLength = -1;
  
  while (std::getline(std::cin, line)) {
    // Remove \r if present (Windows line endings)
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    
    // Empty line marks end of headers
    if (line.empty()) {
      if (contentLength > 0 && static_cast<int>(content.size()) >= contentLength) {
        try {
          auto json = json::parse(content.substr(0, contentLength));
          handleMessage(json);
        } catch (const std::exception& e) {
          havel::error("Failed to parse JSON: {}", e.what());
        }
        content.clear();
        contentLength = -1;
      }
    } else if (line.find("Content-Length:") == 0) {
      contentLength = std::stoi(line.substr(15));
    } else {
      content += line + "\n";
    }
  }
}

void LanguageServer::handleMessage(const json& message) {
  try {
    std::string method = message.value("method", "");
    json params = message.value("params", json::object());
    json id = message.value("id", json(nullptr));
    
    havel::debug("LSP: Received method: {}", method);
    
    if (method == "initialize") {
      sendMessage(makeResponse(id, handleInitialize(params)));
    } else if (method == "initialized") {
      handleInitialized(params);
    } else if (method == "shutdown") {
      sendMessage(makeResponse(id, handleShutdown(params)));
    } else if (method == "exit") {
      handleExit(params);
      std::exit(shutdownRequested ? 0 : 1);
    } else if (method == "textDocument/didOpen") {
      handleDidOpen(params);
    } else if (method == "textDocument/didChange") {
      handleDidChange(params);
    } else if (method == "textDocument/didClose") {
      handleDidClose(params);
    } else {
      havel::debug("LSP: Unhandled method: {}", method);
    }
  } catch (const std::exception& e) {
    havel::error("LSP error: {}", e.what());
  }
}

json LanguageServer::handleInitialize(const json& params) {
  (void)params;
  initialized = true;
  
  json result = {
    {"capabilities", getServerCapabilities()}
  };
  
  havel::info("LSP: Initialized");
  return result;
}

json LanguageServer::handleInitialized(const json& params) {
  (void)params;
  havel::info("LSP: Server fully initialized");
  return json();
}

json LanguageServer::handleShutdown(const json& params) {
  (void)params;
  shutdownRequested = true;
  havel::info("LSP: Shutdown requested");
  return json();
}

void LanguageServer::handleExit(const json& params) {
  (void)params;
  havel::info("LSP: Exit requested");
}

void LanguageServer::handleDidOpen(const json& params) {
  auto doc = params["textDocument"];
  TextDocumentItem item;
  item.uri = doc["uri"];
  item.languageId = doc["languageId"];
  item.version = doc["version"];
  item.text = doc["text"];
  
  documentStore.open(item);
  analyzeDocument(item.uri, item.text);
}

void LanguageServer::handleDidChange(const json& params) {
  std::string uri = params["textDocument"]["uri"];
  auto changes = params["contentChanges"];
  
  std::string text;
  int version = params["textDocument"]["version"];
  
  if (changes.is_array() && !changes.empty()) {
    // For simplicity, use the last change's text
    // Full implementation would handle incremental changes
    text = changes.back()["text"];
  }
  
  documentStore.update(uri, text, version);
  analyzeDocument(uri, text);
}

void LanguageServer::handleDidClose(const json& params) {
  std::string uri = params["textDocument"]["uri"];
  documentStore.close(uri);
}

void LanguageServer::analyzeDocument(const std::string& uri, const std::string& text) {
  auto diagnostics = getDiagnostics(text);
  
  json diagArray = json::array();
  for (const auto& diag : diagnostics) {
    diagArray.push_back({
      {"range", {
        {"start", {{"line", diag.range.start.line}, {"character", diag.range.start.character}}},
        {"end", {{"line", diag.range.end.line}, {"character", diag.range.end.character}}}
      }},
      {"severity", diag.severity},
      {"message", diag.message},
      {"source", diag.source}
    });
  }
  
  sendMessage(makeNotification("textDocument/publishDiagnostics", {
    {"uri", uri},
    {"diagnostics", diagArray}
  }));
}

std::vector<Diagnostic> LanguageServer::getDiagnostics(const std::string& text) {
  std::vector<Diagnostic> diagnostics;
  
  try {
    // Try to tokenize
    Lexer lexer(text, false);
    auto tokens = lexer.tokenize();
    
    // Check for lexer errors
    for (const auto& err : lexer.getErrors()) {
      Diagnostic diag;
      diag.range.start = toLSPPosition(err.line - 1, err.column - 1);  // Convert to 0-based
      diag.range.end = toLSPPosition(err.line - 1, err.column);  // End one char after
      diag.severity = err.severity == ErrorSeverity::Warning ? 2 : 1;
      diag.message = err.message;
      diagnostics.push_back(diag);
    }
    
    // Try to parse
    parser::DebugOptions debug;
    parser::Parser parser(debug);
    auto ast = parser.produceAST(text);
    
    // Check for parser errors
    for (const auto& err : parser.getErrors()) {
      Diagnostic diag;
      diag.range.start = toLSPPosition(err.line - 1, err.column - 1);  // Convert to 0-based
      diag.range.end = toLSPPosition(err.line - 1, err.column + 10);  // Approximate end
      diag.severity = err.severity == ErrorSeverity::Warning ? 2 : 1;
      diag.message = err.message;
      diagnostics.push_back(diag);
    }
    
  } catch (const parser::ParseError& e) {
    Diagnostic diag;
    diag.range.start = toLSPPosition(e.line - 1, e.column - 1);
    diag.range.end = toLSPPosition(e.line - 1, e.column + 10);
    diag.severity = 1;
    diag.message = e.what();
    diagnostics.push_back(diag);
  } catch (const LexError& e) {
    Diagnostic diag;
    diag.range.start = toLSPPosition(e.line - 1, e.column - 1);
    diag.range.end = toLSPPosition(e.line - 1, e.column + 10);
    diag.severity = 1;
    diag.message = e.what();
    diagnostics.push_back(diag);
  } catch (const std::exception& e) {
    Diagnostic diag;
    diag.range.start = toLSPPosition(0, 0);
    diag.range.end = toLSPPosition(0, 10);
    diag.severity = 1;
    diag.message = e.what();
    diagnostics.push_back(diag);
  }
  
  return diagnostics;
}

Position LanguageServer::toLSPPosition(size_t line, size_t column) {
  Position pos;
  pos.line = static_cast<int>(line);
  pos.character = static_cast<int>(column);
  return pos;
}

json LanguageServer::getServerCapabilities() {
  return {
    {"textDocumentSync", {
      {"openClose", true},
      {"change", 1},  // Full
      {"willSave", false},
      {"willSaveWaitUntil", false},
      {"save", false}
    }},
    {"diagnosticProvider", {
      {"identifier", "havel"},
      {"documentSelector", {{"language", "havel"}}},
      {"interFileDependencies", false},
      {"workspaceDiagnostics", false}
    }}
  };
}

void LanguageServer::sendMessage(const json& message) {
  std::string content = message.dump();
  std::cout << "Content-Length: " << content.size() << "\r\n";
  std::cout << "\r\n";
  std::cout << content;
  std::cout.flush();
}

json LanguageServer::makeResponse(int id, const json& result) {
  return {
    {"jsonrpc", "2.0"},
    {"id", id},
    {"result", result}
  };
}

json LanguageServer::makeError(int id, int code, const std::string& message) {
  return {
    {"jsonrpc", "2.0"},
    {"id", id},
    {"error", {
      {"code", code},
      {"message", message}
    }}
  };
}

json LanguageServer::makeNotification(const std::string& method, const json& params) {
  return {
    {"jsonrpc", "2.0"},
    {"method", method},
    {"params", params}
  };
}

} // namespace havel::lsp
