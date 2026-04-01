#include "havel-lang/ast/AST.h"
#include "havel-lang/compiler/bytecode/AdvancedUtils.hpp"
#include "havel-lang/compiler/bytecode/BytecodeDisassembler.hpp"
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace havel::compiler {

// ============================================================================
// ASTTransformer Implementation
// ============================================================================

std::unique_ptr<ast::ASTNode> ASTTransformer::transform(
    std::unique_ptr<ast::ASTNode> node) {
  if (!node) return nullptr;
  return visit(*node);
}

std::unique_ptr<ast::ASTNode> ASTTransformer::visitProgram(ast::Program& node) {
  auto newProgram = std::make_unique<ast::Program>();
  newProgram->line = node.line;
  newProgram->column = node.column;

  for (auto& stmt : node.body) {
    auto newStmt = transform(std::move(stmt));
    if (newStmt) {
      newProgram->body.push_back(std::unique_ptr<ast::Statement>(
        static_cast<ast::Statement*>(newStmt.release())));
    }
  }

  return newProgram;
}

std::unique_ptr<ast::ASTNode> ASTTransformer::visitBinaryExpression(
    ast::BinaryExpression& node) {
  std::unique_ptr<ast::Expression> newLeft;
  std::unique_ptr<ast::Expression> newRight;

  if (node.left) {
    newLeft = std::unique_ptr<ast::Expression>(
      static_cast<ast::Expression*>(transform(std::move(node.left)).release()));
  }
  if (node.right) {
    newRight = std::unique_ptr<ast::Expression>(
      static_cast<ast::Expression*>(transform(std::move(node.right)).release()));
  }

  auto newExpr = std::make_unique<ast::BinaryExpression>(
    std::move(newLeft), node.operator_, std::move(newRight));
  newExpr->line = node.line;
  newExpr->column = node.column;

  return newExpr;
}

std::unique_ptr<ast::ASTNode> ASTTransformer::visitUnaryExpression(
    ast::UnaryExpression& node) {
  std::unique_ptr<ast::Expression> newOperand;

  if (node.operand) {
    newOperand = std::unique_ptr<ast::Expression>(
      static_cast<ast::Expression*>(transform(std::move(node.operand)).release()));
  }

  auto newExpr = std::make_unique<ast::UnaryExpression>(node.operator_, std::move(newOperand));
  newExpr->line = node.line;
  newExpr->column = node.column;

  return newExpr;
}

std::unique_ptr<ast::ASTNode> ASTTransformer::visitCallExpression(
    ast::CallExpression& node) {
  std::vector<std::unique_ptr<ast::Expression>> newArgs;
  std::vector<ast::KeywordArg> newKwargs;

  if (node.callee) {
    auto newCallee = transform(std::move(node.callee));
    for (auto& arg : node.args) {
      if (arg) {
        auto newArg = transform(std::move(arg));
        newArgs.push_back(std::unique_ptr<ast::Expression>(static_cast<ast::Expression*>(newArg.release())));
      }
    }
    auto newExpr = std::make_unique<ast::CallExpression>(
      std::unique_ptr<ast::Expression>(static_cast<ast::Expression*>(newCallee.release())),
      std::move(newArgs),
      std::move(newKwargs)
    );
    newExpr->line = node.line;
    newExpr->column = node.column;
    return newExpr;
  }

  return nullptr;
}

// ============================================================================
// ASTCollector Implementation
// ============================================================================

void ASTCollector::visitDefault(const ast::ASTNode& node) {
  if (predicate_(node)) {
    matches_.push_back(&node);
  }

  // Continue visiting children
  // This would need to be implemented for each node type
  // For now, just check the node itself
}

// ============================================================================
// ASTPrinter Implementation
// ============================================================================

void ASTPrinter::print(const ast::ASTNode& node) {
  visit(node);
}

void ASTPrinter::visitDefault(const ast::ASTNode& node) {
  printIndent();
  output_ << node.toString() << "\n";
}

void ASTPrinter::visitProgram(const ast::Program& node) {
  printIndent();
  output_ << "Program\n";
  indent_++;
  for (const auto& stmt : node.body) {
    if (stmt) visit(*stmt);
  }
  indent_--;
}

void ASTPrinter::visitIdentifier(const ast::Identifier& node) {
  printIndent();
  output_ << "Identifier: " << node.symbol << "\n";
}

void ASTPrinter::visitBinaryExpression(const ast::BinaryExpression& node) {
  printIndent();
  output_ << "BinaryExpression: " << static_cast<int>(node.operator_) << "\n";
  indent_++;
  if (node.left) visit(*node.left);
  if (node.right) visit(*node.right);
  indent_--;
}

void ASTPrinter::indent() {
  indent_++;
}

void ASTPrinter::printIndent() {
  for (int i = 0; i < indent_; ++i) {
    output_ << "  ";
  }
}

// ============================================================================
// ConfigManager Implementation
// ============================================================================

ConfigManager& ConfigManager::instance() {
  static ConfigManager instance;
  return instance;
}

bool ConfigManager::loadFromFile(const std::string& filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    return false;
  }

  std::string line;
  std::string currentSection;

  while (std::getline(file, line)) {
    // Skip comments and empty lines
    if (line.empty() || line[0] == '#') continue;

    // Section header
    if (line[0] == '[' && line.back() == ']') {
      currentSection = line.substr(1, line.size() - 2);
      continue;
    }

    // Key-value pair
    size_t eqPos = line.find('=');
    if (eqPos != std::string::npos) {
      std::string key = line.substr(0, eqPos);
      std::string valueStr = line.substr(eqPos + 1);

      // Trim whitespace
      key.erase(0, key.find_first_not_of(" \t"));
      key.erase(key.find_last_not_of(" \t") + 1);
      valueStr.erase(0, valueStr.find_first_not_of(" \t"));
      valueStr.erase(valueStr.find_last_not_of(" \t") + 1);

      if (!currentSection.empty()) {
        key = currentSection + "." + key;
      }

      values_[key] = parseValue(valueStr);
    }
  }

  return true;
}

bool ConfigManager::saveToFile(const std::string& filename) const {
  std::ofstream file(filename);
  if (!file.is_open()) {
    return false;
  }

  for (const auto& [key, value] : values_) {
    file << key << " = " << valueToString(value) << "\n";
  }

  return true;
}

template<typename T>
T ConfigManager::get(const std::string& key, const T& defaultValue) const {
  auto it = values_.find(key);
  if (it == values_.end()) {
    return defaultValue;
  }

  if constexpr (std::is_same_v<T, bool>) {
    if (std::holds_alternative<bool>(it->second)) {
      return std::get<bool>(it->second);
    }
  } else if constexpr (std::is_same_v<T, int64_t>) {
    if (std::holds_alternative<int64_t>(it->second)) {
      return std::get<int64_t>(it->second);
    }
    if (std::holds_alternative<double>(it->second)) {
      return static_cast<int64_t>(std::get<double>(it->second));
    }
  } else if constexpr (std::is_same_v<T, double>) {
    if (std::holds_alternative<double>(it->second)) {
      return std::get<double>(it->second);
    }
    if (std::holds_alternative<int64_t>(it->second)) {
      return static_cast<double>(std::get<int64_t>(it->second));
    }
  } else if constexpr (std::is_same_v<T, std::string>) {
    return valueToString(it->second);
  }

  return defaultValue;
}

template<typename T>
void ConfigManager::set(const std::string& key, const T& value) {
  if constexpr (std::is_same_v<T, bool>) {
    values_[key] = value;
  } else if constexpr (std::is_same_v<T, int64_t>) {
    values_[key] = value;
  } else if constexpr (std::is_same_v<T, int>) {
    values_[key] = static_cast<int64_t>(value);
  } else if constexpr (std::is_same_v<T, double>) {
    values_[key] = value;
  } else if constexpr (std::is_same_v<T, float>) {
    values_[key] = static_cast<double>(value);
  } else if constexpr (std::is_same_v<T, std::string>) {
    values_[key] = value;
  } else if constexpr (std::is_same_v<T, const char*>) {
    values_[key] = std::string(value);
  }
}

bool ConfigManager::has(const std::string& key) const {
  return values_.count(key) > 0;
}

std::vector<std::string> ConfigManager::getKeys() const {
  std::vector<std::string> keys;
  for (const auto& [key, _] : values_) {
    (void)_;
    keys.push_back(key);
  }
  return keys;
}

std::unique_ptr<ConfigManager> ConfigManager::getSection(const std::string& name) const {
  auto result = std::make_unique<ConfigManager>();
  std::string prefix = name + ".";

  for (const auto& [key, value] : values_) {
    if (key.find(prefix) == 0) {
      std::string newKey = key.substr(prefix.length());
      result->values_[newKey] = value;
    }
  }

  return result;
}

void ConfigManager::remove(const std::string& key) {
  values_.erase(key);
}

void ConfigManager::setSection(const std::string& name, const ConfigManager& section) {
  std::string prefix = name + ".";
  for (const auto& [key, value] : section.values_) {
    values_[prefix + key] = value;
  }
}

CompilationPipeline::Options ConfigManager::getPipelineOptions() const {
  CompilationPipeline::Options opts;
  opts.enableOptimizations = get<bool>("pipeline.optimizations", true);
  opts.enableDebugInfo = get<bool>("pipeline.debugInfo", true);
  opts.strictMode = get<bool>("pipeline.strict", false);
  opts.maxOptimizationPasses = get<int64_t>("pipeline.maxPasses", 3);
  return opts;
}

void ConfigManager::setPipelineOptions(const CompilationPipeline::Options& options) {
  set<bool>("pipeline.optimizations", options.enableOptimizations);
  set<bool>("pipeline.debugInfo", options.enableDebugInfo);
  set<bool>("pipeline.strict", options.strictMode);
  set<int64_t>("pipeline.maxPasses", options.maxOptimizationPasses);
}

void ConfigManager::clear() {
  values_.clear();
  sections_.clear();
}

ConfigManager::Value ConfigManager::parseValue(const std::string& str) const {
  // Try boolean
  if (str == "true" || str == "yes" || str == "on" || str == "1") {
    return true;
  }
  if (str == "false" || str == "no" || str == "off" || str == "0") {
    return false;
  }

  // Try integer
  try {
    size_t pos;
    int64_t val = std::stoll(str, &pos);
    if (pos == str.size()) {
      return val;
    }
  } catch (...) {}

  // Try float
  try {
    size_t pos;
    double val = std::stod(str, &pos);
    if (pos == str.size()) {
      return val;
    }
  } catch (...) {}

  // String
  return str;
}

std::string ConfigManager::valueToString(const Value& value) const {
  return std::visit([](const auto& v) -> std::string {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, bool>) {
      return v ? "true" : "false";
    } else if constexpr (std::is_same_v<T, int64_t>) {
      return std::to_string(v);
    } else if constexpr (std::is_same_v<T, double>) {
      return std::to_string(v);
    } else {
      return v;
    }
  }, value);
}

// ============================================================================
// ModuleCache Implementation
// ============================================================================

ModuleCache::ModuleCache(const std::filesystem::path& cacheDir) : cacheDir_(cacheDir) {
  std::filesystem::create_directories(cacheDir);
}

std::optional<const BytecodeChunk*> ModuleCache::get(
    const std::filesystem::path& sourcePath) {
  auto hash = computeHash(sourcePath);
  auto it = entries_.find(hash);

  if (it == entries_.end()) {
    return std::nullopt;
  }

  // Check if entry is still valid
  if (!isEntryValid(it->second)) {
    entries_.erase(it);
    return std::nullopt;
  }

  return it->second.compiledChunk.get();
}

void ModuleCache::put(const std::filesystem::path& sourcePath,
                       std::unique_ptr<BytecodeChunk> chunk) {
  auto hash = computeHash(sourcePath);

  CacheEntry entry;
  entry.sourcePath = sourcePath;
  entry.sourceMtime = std::filesystem::last_write_time(sourcePath);
  entry.compiledChunk = std::move(chunk);
  entry.compilerVersion = "1.0.0"; // Would be actual version
  entry.cachedAt = std::chrono::system_clock::now();

  entries_[hash] = std::move(entry);
}

void ModuleCache::invalidate(const std::filesystem::path& sourcePath) {
  auto hash = computeHash(sourcePath);
  entries_.erase(hash);
}

void ModuleCache::invalidateAll() {
  entries_.clear();
}

size_t ModuleCache::size() const {
  return entries_.size();
}

void ModuleCache::cleanExpired(std::chrono::hours maxAge) {
  auto now = std::chrono::system_clock::now();

  for (auto it = entries_.begin(); it != entries_.end();) {
    auto age = now - it->second.cachedAt;
    if (age > maxAge) {
      it = entries_.erase(it);
    } else {
      ++it;
    }
  }
}

bool ModuleCache::loadFromDisk() {
  // Would load cached entries from disk
  return true;
}

bool ModuleCache::saveToDisk() {
  // Would save cached entries to disk
  return true;
}

std::string ModuleCache::computeHash(const std::filesystem::path& path) const {
  // Simple hash - would use proper hash in production
  std::string pathStr = path.string();
  size_t hash = std::hash<std::string>{}(pathStr);
  return std::to_string(hash);
}

bool ModuleCache::isEntryValid(const CacheEntry& entry) const {
  // Check if source file has been modified
  if (!std::filesystem::exists(entry.sourcePath)) {
    return false;
  }

  auto currentMtime = std::filesystem::last_write_time(entry.sourcePath);
  if (currentMtime != entry.sourceMtime) {
    return false;
  }

  // Check compiler version
  // if (entry.compilerVersion != currentVersion) return false;

  return true;
}

// ============================================================================
// Explicit template instantiations for ConfigManager
// ============================================================================
template bool ConfigManager::get<bool>(const std::string&, const bool&) const;
template int64_t ConfigManager::get<int64_t>(const std::string&, const int64_t&) const;
template double ConfigManager::get<double>(const std::string&, const double&) const;
template std::string ConfigManager::get<std::string>(const std::string&, const std::string&) const;

template void ConfigManager::set<bool>(const std::string&, const bool&);
template void ConfigManager::set<int64_t>(const std::string&, const int64_t&);
template void ConfigManager::set<int>(const std::string&, const int&);
template void ConfigManager::set<double>(const std::string&, const double&);
template void ConfigManager::set<float>(const std::string&, const float&);
template void ConfigManager::set<std::string>(const std::string&, const std::string&);
template void ConfigManager::set<const char*>(const std::string&, const char* const&);

} // namespace havel::compiler
