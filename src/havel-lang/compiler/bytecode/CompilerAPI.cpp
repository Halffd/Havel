#include "CompilerAPI.hpp"
#include "BytecodeDisassembler.hpp"
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

namespace havel::compiler {

// ============================================================================
// CompilerDriver Implementation
// ============================================================================

CompilerDriver::CompilerDriver() {
  initialize();
}

CompilerDriver::CompilerDriver(const CompileOptions& options) : options_(options) {
  initialize();
}

CompilerDriver::~CompilerDriver() = default;

void CompilerDriver::initialize() {
  // Initialize components
  CompilationPipeline::Options pipelineOpts;
  pipelineOpts.enableOptimizations = options_.optimize;
  pipelineOpts.enableDebugInfo = options_.generateDebugInfo;
  pipelineOpts.strictMode = options_.strictMode;
  pipeline_ = std::make_unique<CompilationPipeline>(pipelineOpts);

  moduleResolver_ = std::make_unique<ModuleResolver>(*static_cast<ModuleLoader*>(nullptr));
  for (const auto& path : options_.importPaths) {
    moduleResolver_->addModulePath(path);
  }

  if (options_.useCache) {
    moduleCache_ = std::make_unique<ModuleCache>(options_.cacheDir);
  }

  nativeBridge_ = std::make_unique<NativeFunctionBridge>();
  config_ = std::make_unique<ConfigManager>();
}

void CompilerDriver::setOptions(const CompileOptions& options) {
  options_ = options;
  // Reinitialize with new options
  initialize();
}

void CompilerDriver::registerNativeFunction(const std::string& name,
                                             NativeFunctionBridge::NativeFunction func) {
  nativeBridge_->registerFunction(name, func);
}

CompileResult CompilerDriver::compileString(const std::string& source,
                                              const std::string& filename) {
  return executeCompilation(source, filename);
}

CompileResult CompilerDriver::compileFile(const std::filesystem::path& path) {
  // Check cache first
  if (options_.useCache && moduleCache_) {
    auto cached = moduleCache_->get(path);
    if (cached) {
      stats_.cacheHits++;
      CompileResult result;
      result.success = true;
      result.chunk = std::make_unique<BytecodeChunk>(**cached);
      return result;
    }
    stats_.cacheMisses++;
  }

  // Read file
  std::ifstream file(path);
  if (!file.is_open()) {
    CompileResult result;
    result.errors.push_back("Cannot open file: " + path.string());
    return result;
  }

  std::string source((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());

  auto result = executeCompilation(source, path.string());

  // Cache result
  if (result.success && options_.useCache && moduleCache_ && result.chunk) {
    moduleCache_->put(path, std::make_unique<BytecodeChunk>(*result.chunk));
  }

  return result;
}

CompileResult CompilerDriver::compileFiles(const std::vector<std::filesystem::path>& paths) {
  // For now, compile sequentially
  CompileResult combinedResult;
  combinedResult.success = true;

  for (const auto& path : paths) {
    auto result = compileFile(path);
    if (!result.success) {
      combinedResult.success = false;
    }
    combinedResult.errors.insert(combinedResult.errors.end(),
                                  result.errors.begin(), result.errors.end());
    combinedResult.warnings.insert(combinedResult.warnings.end(),
                                    result.warnings.begin(), result.warnings.end());

    if (result.chunk) {
      // Merge chunks
      if (!combinedResult.chunk) {
        combinedResult.chunk = std::make_unique<BytecodeChunk>();
      }
      for (const auto& func : result.chunk->getAllFunctions()) {
        combinedResult.chunk->addFunction(func);
      }
    }
  }

  return combinedResult;
}

CompileResult CompilerDriver::executeCompilation(const std::string& source,
                                                  const std::string& filename) {
  auto start = std::chrono::steady_clock::now();
  stats_.compilations++;

  CompileResult result;

  // Run pipeline
  auto pipelineResult = pipeline_->compile(source, filename);

  result.success = pipelineResult.success;
  result.compileTimeMs = pipelineResult.compilationTimeMs;
  result.chunk = std::move(pipelineResult.chunk);
  result.errors = pipelineResult.errors;
  result.warnings = pipelineResult.warnings;

  // Count source lines
  result.sourceLines = std::count(source.begin(), source.end(), '\n') + 1;

  // Calculate bytecode size
  if (result.chunk) {
    for (const auto& func : result.chunk->getAllFunctions()) {
      result.bytecodeSize += func.instructions.size() * sizeof(Instruction);
    }
  }

  // Verify if requested
  if (result.success && options_.verifyBytecode && result.chunk) {
    BytecodeVerifier verifier;
    auto verification = verifier.verify(*result.chunk);
    if (!verification.valid) {
      result.errors.insert(result.errors.end(),
                          verification.errors.begin(),
                          verification.errors.end());
    }
  }

  // Write output if requested
  if (result.success && !options_.outputPath.empty() && result.chunk) {
    if (writeOutput(*result.chunk, options_.outputPath)) {
      result.outputFile = options_.outputPath;
    }
  }

  // Execute if requested
  if (result.success && options_.executeImmediately && result.chunk) {
    std::vector<BytecodeValue> args;
    for (const auto& arg : options_.entryPointArgs) {
      args.push_back(arg); // Simplified
    }
    result.executionResult = execute(*result.chunk, "main", args);
  }

  auto end = std::chrono::steady_clock::now();
  result.compileTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
  stats_.totalCompileTimeMs += result.compileTimeMs;

  return result;
}

BytecodeValue CompilerDriver::execute(const BytecodeChunk& chunk,
                                       const std::string& functionName,
                                       const std::vector<BytecodeValue>& args) {
  stats_.executions++;
  auto start = std::chrono::steady_clock::now();

  // Would use executionContext_ to execute
  (void)chunk;
  (void)functionName;
  (void)args;

  auto end = std::chrono::steady_clock::now();
  stats_.totalExecTimeMs += std::chrono::duration<double, std::milli>(end - start).count();

  return nullptr; // Simplified
}

void CompilerDriver::startREPL() {
  // REPL functionality not yet implemented
}

bool CompilerDriver::runTests(const std::filesystem::path& testDir) {
  (void)testDir;
  // Would discover and run tests
  return true;
}

std::string CompilerDriver::disassemble(const BytecodeChunk& chunk) {
  // Disassembly not yet implemented
  (void)chunk;
  return ""; // Simplified
}

bool CompilerDriver::verify(const BytecodeChunk& chunk) {
  BytecodeVerifier verifier;
  auto result = verifier.verify(chunk);
  return result.valid;
}

bool CompilerDriver::writeOutput(const BytecodeChunk& chunk,
                                  const std::filesystem::path& path) {
  switch (options_.outputFormat) {
    case CompileOptions::OutputFormat::Bytecode: {
      ValueSerializer serializer;
      auto data = serializer.serializeChunk(chunk);
      std::ofstream file(path, std::ios::binary);
      file.write(reinterpret_cast<const char*>(data.data()), data.size());
      return file.good();
    }
    case CompileOptions::OutputFormat::Assembly: {
      std::ofstream file(path);
      file << disassemble(chunk);
      return file.good();
    }
    default:
      return false;
  }
}

CompilerDriver& CompilerDriver::instance() {
  static CompilerDriver instance;
  return instance;
}

template<typename ReturnType, typename... ArgTypes>
void CompilerDriver::registerNativeFunction(const std::string& name,
                                             ReturnType (*func)(ArgTypes...)) {
  nativeBridge_->registerFunction<ReturnType, ArgTypes...>(name, func);
}

// ============================================================================
// BytecodeVerifier Implementation
// ============================================================================

BytecodeVerifier::VerificationResult BytecodeVerifier::verify(const BytecodeChunk& chunk) {
  VerificationResult result;

  for (const auto& func : chunk.getAllFunctions()) {
    auto funcResult = verifyFunction(func, chunk);
    if (!funcResult.valid) {
      result.valid = false;
      result.errors.insert(result.errors.end(),
                          funcResult.errors.begin(),
                          funcResult.errors.end());
    }
  }

  return result;
}

BytecodeVerifier::VerificationResult BytecodeVerifier::verifyFunction(
    const BytecodeFunction& function, const BytecodeChunk& chunk) {
  VerificationResult result;

  if (!checkInstructionIntegrity(function)) {
    addError(result, "Instruction integrity check failed for " + function.name);
  }

  if (!checkStackBalance(function)) {
    addWarning(result, "Stack balance issue in " + function.name);
  }

  if (!checkJumpTargets(function)) {
    addError(result, "Invalid jump target in " + function.name);
  }

  if (!checkConstantIndices(function, chunk)) {
    addError(result, "Invalid constant index in " + function.name);
  }

  return result;
}

bool BytecodeVerifier::checkInstructionIntegrity(const BytecodeFunction& function) {
  for (const auto& instr : function.instructions) {
    // Check opcode validity
    if (static_cast<int>(instr.opcode) < 0 ||
        static_cast<int>(instr.opcode) > static_cast<int>(OpCode::CALL_HOST)) {
      return false;
    }

    // Check operand count (simplified)
    switch (instr.opcode) {
      case OpCode::LOAD_CONST:
      case OpCode::LOAD_VAR:
      case OpCode::STORE_VAR:
      case OpCode::JUMP:
      case OpCode::JUMP_IF_TRUE:
      case OpCode::JUMP_IF_FALSE:
      case OpCode::CALL:
        if (instr.operands.empty()) return false;
        break;
      default:
        break;
    }
  }
  return true;
}

bool BytecodeVerifier::checkStackBalance(const BytecodeFunction& function) {
  // Simplified check - would need full simulation
  (void)function;
  return true;
}

bool BytecodeVerifier::checkJumpTargets(const BytecodeFunction& function) {
  for (const auto& instr : function.instructions) {
    if (instr.opcode == OpCode::JUMP ||
        instr.opcode == OpCode::JUMP_IF_TRUE ||
        instr.opcode == OpCode::JUMP_IF_FALSE) {
      if (!instr.operands.empty()) {
        uint32_t target = std::get<uint32_t>(instr.operands[0]);
        if (target >= function.instructions.size()) {
          return false;
        }
      }
    }
  }
  return true;
}

bool BytecodeVerifier::checkLocalSlots(const BytecodeFunction& function) {
  (void)function;
  return true;
}

bool BytecodeVerifier::checkConstantIndices(const BytecodeFunction& function,
                                               const BytecodeChunk& chunk) {
  (void)function;
  (void)chunk;
  // Constants are now function-level, check against function's constants
  return true;
}

void BytecodeVerifier::addError(VerificationResult& result, const std::string& message) {
  result.valid = false;
  result.errors.push_back(message);
}

void BytecodeVerifier::addWarning(VerificationResult& result, const std::string& message) {
  result.warnings.push_back(message);
}

// ============================================================================
// SourceLocationMapper Implementation
// ============================================================================

void SourceLocationMapper::addMapping(const Mapping& mapping) {
  mappings_.push_back(mapping);
  std::string key = mapping.source.filename + ":" +
                   std::to_string(mapping.source.line) + ":" +
                   std::to_string(mapping.source.column);
  sourceIndex_[key].push_back(mappings_.size() - 1);
}

std::optional<SourceLocation> SourceLocationMapper::findSourceLocation(
    uint32_t functionIndex, uint32_t instructionIndex) const {
  for (const auto& mapping : mappings_) {
    if (mapping.functionIndex == functionIndex &&
        mapping.instructionIndex == instructionIndex) {
      return mapping.source;
    }
  }
  return std::nullopt;
}

std::optional<std::pair<uint32_t, uint32_t>> SourceLocationMapper::findCompiledLocation(
    const SourceLocation& source) const {
  for (const auto& mapping : mappings_) {
    if (mapping.source.filename == source.filename &&
        mapping.source.line == source.line &&
        mapping.source.column == source.column) {
      return std::make_pair(mapping.functionIndex, mapping.instructionIndex);
    }
  }
  return std::nullopt;
}

std::vector<SourceLocationMapper::Mapping> SourceLocationMapper::findMappingsInRange(
    const SourceLocation& start, const SourceLocation& end) const {
  std::vector<Mapping> result;
  for (const auto& mapping : mappings_) {
    if (mapping.source.filename == start.filename &&
        mapping.source.line >= start.line &&
        mapping.source.line <= end.line) {
      result.push_back(mapping);
    }
  }
  return result;
}

void SourceLocationMapper::buildFromDebugInfo(const void* debugInfo) {
  (void)debugInfo;
  // Would extract mappings from debug info
}

std::string SourceLocationMapper::serialize() const {
  std::ostringstream ss;
  for (const auto& mapping : mappings_) {
    ss << mapping.source.filename << ":" << mapping.source.line << ":"
       << mapping.source.column << " -> " << mapping.functionIndex << ":"
       << mapping.instructionIndex << "\n";
  }
  return ss.str();
}

bool SourceLocationMapper::deserialize(const std::string& data) {
  (void)data;
  // Would parse serialized format
  return false;
}

// ============================================================================
// ThreadPool Implementation
// ============================================================================

ThreadPool::ThreadPool(size_t numThreads) {
  for (size_t i = 0; i < numThreads; ++i) {
    workers_.emplace_back(&ThreadPool::workerLoop, this);
  }
}

ThreadPool::~ThreadPool() {
  stop();
}

void ThreadPool::pause() {
  paused_ = true;
}

void ThreadPool::resume() {
  paused_ = false;
  condition_.notify_all();
}

void ThreadPool::stop() {
  stop_ = true;
  condition_.notify_all();
  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

void ThreadPool::waitForAll() {
  std::unique_lock<std::mutex> lock(queueMutex_);
  condition_.wait(lock, [this] {
    return pending_ == 0 && active_ == 0;
  });
}

size_t ThreadPool::getPendingCount() const {
  return pending_.load();
}

size_t ThreadPool::getActiveCount() const {
  return active_.load();
}

size_t ThreadPool::getCompletedCount() const {
  return completed_.load();
}

void ThreadPool::workerLoop() {
  while (!stop_) {
    std::function<void()> task;

    {
      std::unique_lock<std::mutex> lock(queueMutex_);
      condition_.wait(lock, [this] {
        return stop_ || (!paused_ && !tasks_.empty());
      });

      if (stop_ || (paused_ && tasks_.empty())) continue;

      if (!tasks_.empty()) {
        task = std::move(tasks_.front());
        tasks_.pop();
        pending_--;
        active_++;
      }
    }

    if (task) {
      task();
      active_--;
      completed_++;
      condition_.notify_one();
    }
  }
}

// ============================================================================
// ParallelCompiler Implementation
// ============================================================================

ParallelCompiler::ParallelCompiler(ThreadPool& threadPool) : threadPool_(threadPool) {}

std::vector<CompileResult> ParallelCompiler::compileBatch(
    const std::vector<std::pair<std::string, std::string>>& sources) {
  std::vector<CompileResult> results(sources.size());
  std::vector<std::future<void>> futures;

  CompilerDriver driver;

  for (size_t i = 0; i < sources.size(); ++i) {
    futures.push_back(
      threadPool_.submit([&, i]() {
        results[i] = driver.compileString(sources[i].second, sources[i].first);
      })
    );
  }

  for (auto& future : futures) {
    future.wait();
  }

  return results;
}

std::vector<CompileResult> ParallelCompiler::compileDirectory(
    const std::filesystem::path& dir) {
  std::vector<std::filesystem::path> files;

  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".havel") {
      files.push_back(entry.path());
    }
  }

  CompilerDriver driver;
  std::vector<CompileResult> results;

  for (const auto& file : files) {
    results.push_back(driver.compileFile(file));
  }

  return results;
}

// ============================================================================
// CompilerMetrics Implementation
// ============================================================================

void CompilerMetrics::recordCompile(const CompileMetrics& metrics) {
  std::lock_guard<std::mutex> lock(mutex_);
  compiles_.push_back(metrics);
}

void CompilerMetrics::recordPhase(const std::string& filename,
                                   const PhaseMetrics& phase) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& compile : compiles_) {
    if (compile.filename == filename) {
      compile.phases.push_back(phase);
      return;
    }
  }
}

std::string CompilerMetrics::generateReport() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ostringstream ss;

  ss << "=== Compiler Metrics Report ===\n\n";
  ss << "Total compilations: " << compiles_.size() << "\n";

  double totalTime = 0.0;
  for (const auto& compile : compiles_) {
    totalTime += compile.totalTimeMs;
  }
  ss << "Average compile time: " << (compiles_.empty() ? 0 : totalTime / compiles_.size())
     << " ms\n";

  return ss.str();
}

void CompilerMetrics::printReport(std::ostream& output) const {
  output << generateReport();
}

double CompilerMetrics::getAverageCompileTime() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (compiles_.empty()) return 0.0;

  double total = 0.0;
  for (const auto& compile : compiles_) {
    total += compile.totalTimeMs;
  }
  return total / compiles_.size();
}

std::string CompilerMetrics::getSlowestPhase() const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::unordered_map<std::string, double> phaseTimes;
  for (const auto& compile : compiles_) {
    for (const auto& phase : compile.phases) {
      phaseTimes[phase.phaseName] += phase.timeMs;
    }
  }

  std::string slowest;
  double maxTime = 0.0;
  for (const auto& [name, time] : phaseTimes) {
    if (time > maxTime) {
      maxTime = time;
      slowest = name;
    }
  }

  return slowest;
}

bool CompilerMetrics::exportJSON(const std::filesystem::path& path) const {
  (void)path;
  return false;
}

bool CompilerMetrics::exportCSV(const std::filesystem::path& path) const {
  (void)path;
  return false;
}

// ============================================================================
// MemoryPool Implementation
// ============================================================================

template<typename T>
MemoryPool<T>::MemoryPool(size_t blockSize) : blockSize_(blockSize) {
  grow();
}

template<typename T>
MemoryPool<T>::~MemoryPool() = default;

template<typename T>
T* MemoryPool<T>::allocate() {
  if (!freeList_.empty()) {
    T* ptr = freeList_.back();
    freeList_.pop_back();
    allocated_++;
    available_--;
    return ptr;
  }

  T* ptr = allocateFromBlock();
  if (ptr) {
    allocated_++;
    return ptr;
  }

  grow();
  return allocate();
}

template<typename T>
void MemoryPool<T>::deallocate(T* ptr) {
  if (ptr) {
    freeList_.push_back(ptr);
    allocated_--;
    available_++;
  }
}

template<typename T>
void MemoryPool<T>::reserve(size_t count) {
  while (available_ < count) {
    grow();
  }
}

template<typename T>
void MemoryPool<T>::clear() {
  freeList_.clear();
  blocks_.clear();
  allocated_ = 0;
  available_ = 0;
  grow();
}

template<typename T>
T* MemoryPool<T>::allocateFromBlock() {
  for (auto& block : blocks_) {
    if (block.used < blockSize_) {
      T* ptr = reinterpret_cast<T*>(block.data.get() + block.used * sizeof(T));
      block.used++;
      return ptr;
    }
  }
  return nullptr;
}

template<typename T>
void MemoryPool<T>::grow() {
  Block block;
  block.data = std::make_unique<std::byte[]>(blockSize_ * sizeof(T));
  block.used = 0;
  blocks_.push_back(std::move(block));
  available_ += blockSize_;
}

// Explicit instantiations
template class MemoryPool<ast::ASTNode>;
template class MemoryPool<BytecodeValue>;
template class MemoryPool<Instruction>;

// Explicit template instantiations for CompilerDriver
template void CompilerDriver::registerNativeFunction<int>(const std::string&, int (*)());
template void CompilerDriver::registerNativeFunction<void>(const std::string&, void (*)());

} // namespace havel::compiler
