#pragma once

#include "BytecodeIR.hpp"
#include "GC.hpp"

#include <array>
#include <memory>
#include <optional>
#include <stack>
#include <unordered_map>
#include <vector>

namespace havel::compiler {

class VM : public BytecodeInterpreter {
public:
  class GCRoot {
  public:
    GCRoot() = default;
    GCRoot(VM &vm, const BytecodeValue &value) : vm_(&vm) {
      id_ = vm.pinExternalRoot(value);
    }
    GCRoot(const GCRoot &) = delete;
    GCRoot &operator=(const GCRoot &) = delete;
    GCRoot(GCRoot &&other) noexcept : vm_(other.vm_), id_(other.id_) {
      other.vm_ = nullptr;
      other.id_ = 0;
    }
    GCRoot &operator=(GCRoot &&other) noexcept {
      if (this == &other) {
        return *this;
      }
      reset();
      vm_ = other.vm_;
      id_ = other.id_;
      other.vm_ = nullptr;
      other.id_ = 0;
      return *this;
    }
    ~GCRoot() { reset(); }

    std::optional<BytecodeValue> get() const {
      if (!vm_ || id_ == 0) {
        return std::nullopt;
      }
      return vm_->externalRootValue(id_);
    }

    void reset() {
      if (vm_ && id_ != 0) {
        vm_->unpinExternalRoot(id_);
      }
      vm_ = nullptr;
      id_ = 0;
    }

  private:
    VM *vm_ = nullptr;
    uint64_t id_ = 0;
  };

private:
  struct RuntimeClosure {
    uint32_t function_index = 0;
    std::vector<std::shared_ptr<GCHeap::UpvalueCell>> upvalues;
  };

  struct CallFrame {
    const BytecodeFunction *function = nullptr;
    size_t ip = 0;
    size_t locals_base = 0;
    uint32_t closure_id = 0;
  };

  std::stack<BytecodeValue> stack;
  std::vector<BytecodeValue> locals;
  std::vector<CallFrame> frames;
  GCHeap heap_;
  std::unordered_map<uint32_t, std::shared_ptr<GCHeap::UpvalueCell>>
      open_upvalues;
  std::unordered_map<std::string, BytecodeValue> globals;
  std::unordered_map<std::string, BytecodeHostFunction> host_functions;
  const BytecodeChunk *current_chunk = nullptr;
  bool debug_mode = false;
  size_t max_call_depth_ = 1024;
  bool profiling_enabled_ = false;
  std::array<uint64_t, 256> opcode_counts_{};
  uint64_t executed_instructions_ = 0;

  template <typename T> T getValue(const BytecodeValue &value);
  const CallFrame &currentFrame() const;
  CallFrame &currentFrame();
  BytecodeValue getConstant(uint32_t index);
  void executeInstruction(const Instruction &instruction);
  void doCall(BytecodeValue callee_value, std::vector<BytecodeValue> args);
  void runDispatchLoop(size_t stop_frame_depth);
  void closeFrameUpvalues(uint32_t locals_base, uint32_t locals_end);
  std::vector<BytecodeValue> stackValuesForRoots() const;
  std::vector<uint32_t> activeClosureIdsForRoots() const;
  void maybeCollectGarbage();
  void collectGarbage();
  void registerDefaultHostFunctions();
  void registerDefaultHostGlobals();
  BytecodeValue invokeHostFunction(const std::string &name,
                                   uint32_t arg_count);

public:
  VM();
  ~VM() override;
  BytecodeValue execute(const BytecodeChunk &chunk,
                        const std::string &function_name,
                        const std::vector<BytecodeValue> &args = {}) override;
  BytecodeValue call(const BytecodeValue &callee_value,
                     const std::vector<BytecodeValue> &args = {});
  void setDebugMode(bool enabled) override;
  void registerHostFunction(const std::string &name,
                            BytecodeHostFunction function) override;
  void registerHostFunction(const std::string &name, size_t arity,
                            BytecodeHostFunction function);
  bool hasHostFunction(const std::string &name) const override;

  void setMaxCallDepth(size_t value) { max_call_depth_ = value; }
  void setProfilingEnabled(bool enabled) { profiling_enabled_ = enabled; }
  uint64_t executedInstructionCount() const { return executed_instructions_; }
  uint64_t opcodeCount(OpCode opcode) const {
    return opcode_counts_[static_cast<uint8_t>(opcode)];
  }

  void setGcAllocationBudget(size_t value) { heap_.setAllocationBudget(value); }
  void runGarbageCollection() { collectGarbage(); }
  GCHeap::Stats gcStats() const { return heap_.stats(); }
  void setGlobal(std::string name, BytecodeValue value) {
    globals[std::move(name)] = std::move(value);
  }
  [[nodiscard]] GCRoot makeRoot(const BytecodeValue &value) {
    return GCRoot(*this, value);
  }
  ObjectRef createHostObject();
  ArrayRef createHostArray();
  void setHostObjectField(ObjectRef object_ref, const std::string &key,
                          BytecodeValue value);
  void pushHostArrayValue(ArrayRef array_ref, BytecodeValue value);
  uint64_t pinExternalRoot(const BytecodeValue &value);
  bool unpinExternalRoot(uint64_t root_id);
  std::optional<BytecodeValue> externalRootValue(uint64_t root_id) const;
  size_t externalRootCount() const { return heap_.externalRootCount(); }
};

} // namespace havel::compiler
