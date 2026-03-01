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

void DocumentStore::setSymbols(const std::string& uri, const std::vector<SymbolInfo>& symbols) {
  std::lock_guard<std::mutex> lock(mutex);
  symbolTables[uri] = symbols;
}

std::vector<SymbolInfo> DocumentStore::getSymbols(const std::string& uri) const {
  std::lock_guard<std::mutex> lock(mutex);
  auto it = symbolTables.find(uri);
  if (it != symbolTables.end()) {
    return it->second;
  }
  return {};
}

SymbolInfo* DocumentStore::findSymbol(const std::string& uri, const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex);
  auto it = symbolTables.find(uri);
  if (it != symbolTables.end()) {
    for (auto& sym : it->second) {
      if (sym.name == name) {
        return &sym;
      }
    }
  }
  return nullptr;
}

// ============ LanguageServer ============

LanguageServer::LanguageServer() = default;
LanguageServer::~LanguageServer() = default;

void LanguageServer::run() {
  std::string buffer;
  int contentLength = -1;
  bool readingHeaders = true;
  
  char c;
  while (std::cin.get(c)) {
    buffer += c;
    
    // Check for end of headers (\r\n\r\n)
    if (readingHeaders && buffer.size() >= 4) {
      size_t headerEnd = buffer.find("\r\n\r\n");
      if (headerEnd != std::string::npos) {
        // Parse headers
        std::string headers = buffer.substr(0, headerEnd);
        size_t lenPos = headers.find("Content-Length:");
        if (lenPos != std::string::npos) {
          size_t numStart = headers.find_first_of("0123456789", lenPos);
          if (numStart != std::string::npos) {
            size_t numEnd = headers.find_first_not_of("0123456789", numStart);
            std::string numStr;
            if (numEnd == std::string::npos) {
              numStr = headers.substr(numStart);
            } else {
              numStr = headers.substr(numStart, numEnd - numStart);
            }
            contentLength = std::stoi(numStr);
          }
        }
        readingHeaders = false;
        buffer = buffer.substr(headerEnd + 4);
      }
    }
    
    // Check if we have complete message
    if (!readingHeaders && contentLength > 0 && static_cast<int>(buffer.size()) >= contentLength) {
      try {
        std::string jsonStr = buffer.substr(0, contentLength);
        auto json = json::parse(jsonStr);
        handleMessage(json);
        
        buffer = buffer.substr(contentLength);
        contentLength = -1;
        readingHeaders = true;
      } catch (const std::exception& e) {
        buffer.clear();
        contentLength = -1;
        readingHeaders = true;
      }
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
    } else if (method == "textDocument/hover") {
      sendMessage(makeResponse(id, handleHover(params)));
    } else if (method == "textDocument/definition") {
      sendMessage(makeResponse(id, handleDefinition(params)));
    } else if (method == "textDocument/documentSymbol") {
      sendMessage(makeResponse(id, handleDocumentSymbol(params)));
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
  
  // Extract symbols for hover/definition/symbol support
  auto symbols = extractSymbols(text);
  documentStore.setSymbols(uri, symbols);
  
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

std::vector<SymbolInfo> LanguageServer::extractSymbols(const std::string& text) {
  std::vector<SymbolInfo> symbols;
  
  try {
    // Tokenize and look for function/variable definitions
    Lexer lexer(text, false);
    auto tokens = lexer.tokenize();
    
    std::string currentKeyword;
    for (size_t i = 0; i < tokens.size(); i++) {
      const auto& token = tokens[i];
      
      // Track keywords
      if (token.type == TokenType::Fn) {
        currentKeyword = "fn";
      } else if (token.type == TokenType::Let) {
        currentKeyword = "let";
      } else if (token.type == TokenType::Struct) {
        currentKeyword = "struct";
      } else if (token.type == TokenType::Enum) {
        currentKeyword = "enum";
      }
      
      // Look for identifier after keyword
      if (!currentKeyword.empty() && token.type == TokenType::Identifier) {
        SymbolInfo sym;
        sym.name = token.value;
        sym.range.start = toLSPPosition(token.line - 1, token.column - 1);
        sym.range.end = toLSPPosition(token.line - 1, token.column - 1 + token.value.length());
        
        if (currentKeyword == "fn") {
          sym.kind = "function";
          sym.detail = "Function";
        } else if (currentKeyword == "let") {
          sym.kind = "variable";
          sym.detail = "Variable";
        } else if (currentKeyword == "struct") {
          sym.kind = "struct";
          sym.detail = "Struct";
        } else if (currentKeyword == "enum") {
          sym.kind = "enum";
          sym.detail = "Enum";
        }
        
        symbols.push_back(sym);
        currentKeyword.clear();
      }
      
      // Reset keyword on semicolon or newline
      if (token.type == TokenType::Semicolon || token.type == TokenType::NewLine) {
        currentKeyword.clear();
      }
    }
  } catch (...) {
    // Ignore extraction errors
  }
  
  return symbols;
}

SymbolInfo* LanguageServer::findSymbolAt(const std::string& uri, Position position) {
  auto symbols = documentStore.getSymbols(uri);
  for (auto& sym : symbols) {
    if (position.line >= sym.range.start.line && 
        position.line <= sym.range.end.line &&
        position.character >= sym.range.start.character &&
        position.character <= sym.range.end.character) {
      return &sym;
    }
  }
  return nullptr;
}

json LanguageServer::handleHover(const json& params) {
  std::string uri = params["textDocument"]["uri"];
  auto position = params["position"];
  
  Position pos;
  pos.line = position["line"];
  pos.character = position["character"];
  
  auto* sym = findSymbolAt(uri, pos);
  if (sym) {
    return {
      {"contents", {
        {"kind", "markdown"},
        {"value", "```havel\n" + sym->detail + " " + sym->name + "\n```"}
      }},
      {"range", {
        {"start", {{"line", sym->range.start.line}, {"character", sym->range.start.character}}},
        {"end", {{"line", sym->range.end.line}, {"character", sym->range.end.character}}}
      }}
    };
  }
  
  return json();  // No hover info
}

json LanguageServer::handleDefinition(const json& params) {
  std::string uri = params["textDocument"]["uri"];
  auto position = params["position"];
  
  Position pos;
  pos.line = position["line"];
  pos.character = position["character"];
  
  auto* sym = findSymbolAt(uri, pos);
  if (sym) {
    return {
      {"uri", uri},
      {"range", {
        {"start", {{"line", sym->range.start.line}, {"character", sym->range.start.character}}},
        {"end", {{"line", sym->range.end.line}, {"character", sym->range.end.character}}}
      }}
    };
  }
  
  return json();  // No definition found
}

json LanguageServer::handleDocumentSymbol(const json& params) {
  std::string uri = params["textDocument"]["uri"];
  auto symbols = documentStore.getSymbols(uri);
  
  json result = json::array();
  for (const auto& sym : symbols) {
    result.push_back({
      {"name", sym.name},
      {"kind", sym.kind == "function" ? 12 : 
                sym.kind == "variable" ? 13 :
                sym.kind == "struct" ? 23 :
                sym.kind == "enum" ? 10 : 1},
      {"range", {
        {"start", {{"line", sym.range.start.line}, {"character", sym.range.start.character}}},
        {"end", {{"line", sym.range.end.line}, {"character", sym.range.end.character}}}
      }},
      {"selectionRange", {
        {"start", {{"line", sym.range.start.line}, {"character", sym.range.start.character}}},
        {"end", {{"line", sym.range.end.line}, {"character", sym.range.end.character}}}
      }},
      {"detail", sym.detail}
    });
  }
  
  return result;
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
    }},
    {"hoverProvider", {
      {"workDoneProgress", false}
    }},
    {"definitionProvider", {
      {"workDoneProgress", false}
    }},
    {"documentSymbolProvider", {
      {"workDoneProgress", false},
      {"label", "Havel Symbols"}
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
