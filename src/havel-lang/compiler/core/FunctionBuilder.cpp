#include "BytecodeIR.hpp"

#include <algorithm>
#include <utility>

namespace havel::compiler {

// ===== FunctionBuilder::Impl =====
//
// Stateful construction of a CFG-backed BytecodeFunction. Blocks are appended
// to `blocks` in creation order; the "current" block id is where emissions go.
// The entry block is always block 0 (created at construction) unless the caller
// re-targets via set_current_block before emitting.
struct FunctionBuilder::Impl {
  std::string name;
  uint32_t param_count;
  uint32_t current = 0;
  std::vector<BasicBlock> blocks;
  std::vector<LocalInfo> locals;
  std::vector<std::string> global_names;
  // Upvalues captured by the closure, by upvalue descriptor index.
  std::vector<uint32_t> upvalue_indices;

  // Intern a global name, returning the index to use as the LOAD_GLOBAL /
  // STORE_GLOBAL operand (a chunk-local StringValId at lowering time).
  uint32_t intern_global(const std::string& name) {
    for (uint32_t i = 0; i < global_names.size(); ++i) {
      if (global_names[i] == name) return i;
    }
    global_names.push_back(name);
    return static_cast<uint32_t>(global_names.size() - 1);
  }

  Impl(std::string n, uint32_t params, uint32_t locals_count)
      : name(std::move(n)), param_count(params) {
    locals.reserve(locals_count);
    // Parameter slots first, then the requested number of extra temps.
    for (uint32_t i = 0; i < params; ++i) {
      locals.push_back(LocalInfo{/*type_hint=*/0, /*is_mutable=*/true,
                                 /*is_param=*/true, /*name=*/""});
    }
    for (uint32_t i = 0; i < locals_count; ++i) {
      locals.push_back(LocalInfo{/*type_hint=*/0, /*is_mutable=*/true,
                                 /*is_param=*/false, /*name=*/""});
    }
    // Entry block is block 0.
    blocks.emplace_back(0);
  }

  uint32_t add_block(std::optional<SourceLocation> loc, bool landing_pad) {
    BasicBlock b(blocks.size());
    b.location = std::move(loc);
    b.is_landing_pad = landing_pad;
    blocks.push_back(std::move(b));
    return static_cast<uint32_t>(blocks.size() - 1);
  }

  // Reserve an empty block WITHOUT switching current to it. Used by the
  // structured helpers to pre-create targets (e.g. loop exit) whose ids must
  // be stable before the current block finishes.
  uint32_t reserve_block() {
    BasicBlock b(blocks.size());
    blocks.push_back(std::move(b));
    return static_cast<uint32_t>(blocks.size() - 1);
  }

  BasicBlock& cur() { return blocks[current]; }
  const BasicBlock& cur() const { return blocks[current]; }

  // Set the terminator on the current block. Does not switch current: the
  // caller creates the next block explicitly via create_block()/set_current_block().
  // Returns the id of the block that received the terminator.
  uint32_t terminate(Terminator t) {
    cur().terminator = std::move(t);
    return current;
  }
};

FunctionBuilder::FunctionBuilder(std::string name, uint32_t param_count,
                                 uint32_t local_count)
    : impl(std::make_unique<Impl>(std::move(name), param_count, local_count)) {}

FunctionBuilder::~FunctionBuilder() = default;

uint32_t FunctionBuilder::current_block() const { return impl->current; }

void FunctionBuilder::set_current_block(uint32_t block_id) {
  if (block_id < impl->blocks.size()) {
    impl->current = block_id;
  }
}

uint32_t FunctionBuilder::create_block(std::optional<SourceLocation> loc) {
  uint32_t id = impl->add_block(std::move(loc), false);
  impl->current = id;
  return id;
}

uint32_t FunctionBuilder::create_landing_pad(std::optional<SourceLocation> loc) {
  uint32_t id = impl->add_block(std::move(loc), true);
  impl->current = id;
  return id;
}

uint32_t FunctionBuilder::jump(uint32_t target, std::optional<SourceLocation> loc) {
  return impl->terminate(Terminator::jump(target, loc));
}

uint32_t FunctionBuilder::jump_if_false(uint32_t target, std::optional<SourceLocation> loc) {
  return impl->terminate(Terminator::jumpIfFalse(target, loc));
}

uint32_t FunctionBuilder::jump_if_true(uint32_t target, std::optional<SourceLocation> loc) {
  return impl->terminate(Terminator::jumpIfTrue(target, loc));
}

uint32_t FunctionBuilder::jump_if_null(uint32_t target, std::optional<SourceLocation> loc) {
  return impl->terminate(Terminator::jumpIfNull(target, loc));
}

uint32_t FunctionBuilder::ret(std::optional<SourceLocation> loc) {
  return impl->terminate(Terminator::ret(loc));
}

uint32_t FunctionBuilder::throw_(std::optional<SourceLocation> loc) {
  return impl->terminate(Terminator::throw_(loc));
}

uint32_t FunctionBuilder::unreachable(std::optional<SourceLocation> loc) {
  return impl->terminate(Terminator::unreachable(loc));
}

std::pair<uint32_t, uint32_t> FunctionBuilder::branch(
    uint32_t cond_block, uint32_t true_target, uint32_t false_target) {
  // `cond_block` must be the block holding the boolean condition, which in the
  // common idiom is also the current emission block. Wire it so that a true
  // value falls through to `true_target` and a false value jumps to
  // `false_target`. Returns the (then, else) destination pair per the contract.
  if (cond_block < impl->blocks.size() &&
      impl->blocks[cond_block].terminator.kind == TerminatorKind::None) {
    impl->blocks[cond_block].terminator = Terminator::jumpIfFalse(false_target);
  }
  return {true_target, false_target};
}

std::tuple<uint32_t, uint32_t, uint32_t> FunctionBuilder::loop(
    std::function<void(uint32_t header)> init,
    std::function<void(uint32_t body)> body_fn) {
  // Layout:
  //   header   <- condition computed by init(); loop appends
  //              JumpIfFalse(exit)
  //   body     <- body_fn(); loop appends Jump(header) back-edge
  //   exit     <- empty continuation, returned to the caller
  // The exit id must be stable while init() runs, so reserve it first.
  uint32_t exit = impl->reserve_block();

  uint32_t header = create_block();
  init(header);
  // The block the client left as current holds the condition; close it with a
  // conditional branch to the reserved exit block, falling through to body.
  if (impl->cur().terminator.kind == TerminatorKind::None) {
    impl->cur().terminator = Terminator::jumpIfFalse(exit);
  }

  uint32_t body = create_block();
  body_fn(body);
  // Close the body with a back-edge to the header.
  if (impl->cur().terminator.kind == TerminatorKind::None) {
    impl->cur().terminator = Terminator::jump(header);
  }

  // Leave the caller emitting into the loop exit so post-loop code lands there.
  impl->current = exit;
  return {header, body, exit};
}

std::tuple<uint32_t, std::vector<uint32_t>, uint32_t> FunctionBuilder::switch_(
    uint32_t value_block, const std::vector<uint64_t>& cases,
    uint32_t default_target) {
  // Layout (selector value must be on the eval stack at `value_block`):
  //   value_block: STORE_VAR sel
  //   dispatch_0: LOAD_VAR sel; LOAD_CONST cases[0]; EQ;
  //               terminator JumpIfTrue(case_target_0)   // false -> dispatch_1
  //   dispatch_1: ... cases[1] ...
  //   final:      Jump(default_target)
  // Each case target block is created empty and returned so the caller can emit
  // the per-case bodies; false falls through along contiguous dispatch blocks.
  uint32_t sel_local = add_local(LocalInfo{/*type_hint=*/0, /*is_mutable=*/true,
                                           /*is_param=*/false, /*name=*/"@switch_sel"});

  if (value_block < impl->blocks.size() &&
      impl->blocks[value_block].terminator.kind == TerminatorKind::None) {
    impl->blocks[value_block].instructions.push_back(
        Instruction(OpCode::STORE_VAR, {Value::makeInt(sel_local)}));
    impl->current = value_block;
  }

  std::vector<uint32_t> case_targets;
  case_targets.reserve(cases.size());

  uint32_t first_dispatch = UINT32_MAX;
  for (size_t i = 0; i < cases.size(); ++i) {
    uint32_t case_target = impl->reserve_block();
    case_targets.push_back(case_target);

    uint32_t dispatch = create_block();
    if (first_dispatch == UINT32_MAX) first_dispatch = dispatch;
    impl->cur().instructions.emplace_back(
        OpCode::LOAD_VAR, std::vector<Value>{Value::makeInt(sel_local)});
    impl->cur().instructions.emplace_back(
        OpCode::LOAD_CONST, std::vector<Value>{Value::makeInt(static_cast<int64_t>(cases[i]))});
    impl->cur().instructions.emplace_back(OpCode::EQ);
    impl->cur().terminator = Terminator::jumpIfTrue(case_target);
    impl->current = impl->add_block({}, false);  // false falls into next dispatch
  }

  // Final dispatch block unconditionally jumps to the default target.
  impl->cur().terminator = Terminator::jump(default_target);
  impl->current = impl->add_block({}, false);

  return {first_dispatch, case_targets, default_target};
}

void FunctionBuilder::push(Instruction inst) {
  impl->cur().push(std::move(inst));
}

void FunctionBuilder::push(OpCode op, std::vector<Value> ops,
                           std::optional<SourceLocation> loc) {
  Instruction inst(op, std::move(ops));
  inst.location = std::move(loc);
  push(std::move(inst));
}

void FunctionBuilder::load_const(Value val, std::optional<SourceLocation> loc) {
  push(OpCode::LOAD_CONST, {std::move(val)}, std::move(loc));
}

void FunctionBuilder::load_var(uint32_t local_idx, std::optional<SourceLocation> loc) {
  push(OpCode::LOAD_VAR, {Value::makeInt(local_idx)}, std::move(loc));
}

void FunctionBuilder::store_var(uint32_t local_idx, std::optional<SourceLocation> loc) {
  push(OpCode::STORE_VAR, {Value::makeInt(local_idx)}, std::move(loc));
}

void FunctionBuilder::load_global(std::string name, std::optional<SourceLocation> loc) {
  uint32_t idx = impl->intern_global(name);
  push(OpCode::LOAD_GLOBAL, {Value::makeStringValId(idx)}, std::move(loc));
}

void FunctionBuilder::store_global(std::string name, std::optional<SourceLocation> loc) {
  uint32_t idx = impl->intern_global(name);
  push(OpCode::STORE_GLOBAL, {Value::makeStringValId(idx)}, std::move(loc));
}

void FunctionBuilder::load_upvalue(uint32_t idx, std::optional<SourceLocation> loc) {
  push(OpCode::LOAD_UPVALUE, {Value::makeInt(idx)}, std::move(loc));
}

void FunctionBuilder::store_upvalue(uint32_t idx, std::optional<SourceLocation> loc) {
  push(OpCode::STORE_UPVALUE, {Value::makeInt(idx)}, std::move(loc));
}

void FunctionBuilder::add(uint64_t type_hint, std::optional<SourceLocation> loc) {
  push(OpCode::ADD, {}, std::move(loc));
  (void)type_hint;
}

void FunctionBuilder::sub(uint64_t type_hint, std::optional<SourceLocation> loc) {
  push(OpCode::SUB, {}, std::move(loc));
  (void)type_hint;
}

void FunctionBuilder::mul(uint64_t type_hint, std::optional<SourceLocation> loc) {
  push(OpCode::MUL, {}, std::move(loc));
  (void)type_hint;
}

void FunctionBuilder::div(uint64_t type_hint, std::optional<SourceLocation> loc) {
  push(OpCode::DIV, {}, std::move(loc));
  (void)type_hint;
}

void FunctionBuilder::mod(uint64_t type_hint, std::optional<SourceLocation> loc) {
  push(OpCode::MOD, {}, std::move(loc));
  (void)type_hint;
}

void FunctionBuilder::eq(std::optional<SourceLocation> loc) { push(OpCode::EQ, {}, std::move(loc)); }
void FunctionBuilder::neq(std::optional<SourceLocation> loc) { push(OpCode::NEQ, {}, std::move(loc)); }
void FunctionBuilder::lt(std::optional<SourceLocation> loc) { push(OpCode::LT, {}, std::move(loc)); }
void FunctionBuilder::lte(std::optional<SourceLocation> loc) { push(OpCode::LTE, {}, std::move(loc)); }
void FunctionBuilder::gt(std::optional<SourceLocation> loc) { push(OpCode::GT, {}, std::move(loc)); }
void FunctionBuilder::gte(std::optional<SourceLocation> loc) { push(OpCode::GTE, {}, std::move(loc)); }

void FunctionBuilder::and_(std::optional<SourceLocation> loc) { push(OpCode::AND, {}, std::move(loc)); }
void FunctionBuilder::or_(std::optional<SourceLocation> loc) { push(OpCode::OR, {}, std::move(loc)); }
void FunctionBuilder::not_(std::optional<SourceLocation> loc) { push(OpCode::NOT, {}, std::move(loc)); }

void FunctionBuilder::add_int(std::optional<SourceLocation> loc) { push(OpCode::ADD_INT, {}, std::move(loc)); }
void FunctionBuilder::sub_int(std::optional<SourceLocation> loc) { push(OpCode::SUB_INT, {}, std::move(loc)); }
void FunctionBuilder::mul_int(std::optional<SourceLocation> loc) { push(OpCode::MUL_INT, {}, std::move(loc)); }
void FunctionBuilder::div_int(std::optional<SourceLocation> loc) { push(OpCode::DIV_INT, {}, std::move(loc)); }
void FunctionBuilder::mod_int(std::optional<SourceLocation> loc) { push(OpCode::MOD_INT, {}, std::move(loc)); }

void FunctionBuilder::string_cursor_new(std::optional<SourceLocation> loc) { push(OpCode::STRING_CURSOR_NEW, {}, std::move(loc)); }
void FunctionBuilder::string_cursor_current(std::optional<SourceLocation> loc) { push(OpCode::STRING_CURSOR_CURRENT, {}, std::move(loc)); }
void FunctionBuilder::string_cursor_advance(std::optional<SourceLocation> loc) { push(OpCode::STRING_CURSOR_ADVANCE, {}, std::move(loc)); }
void FunctionBuilder::string_cursor_peek(std::optional<SourceLocation> loc) { push(OpCode::STRING_CURSOR_PEEK, {}, std::move(loc)); }
void FunctionBuilder::string_cursor_reset(std::optional<SourceLocation> loc) { push(OpCode::STRING_CURSOR_RESET, {}, std::move(loc)); }
void FunctionBuilder::string_cursor_get_pos(std::optional<SourceLocation> loc) { push(OpCode::STRING_CURSOR_GET_POS, {}, std::move(loc)); }
void FunctionBuilder::string_cursor_set_pos(std::optional<SourceLocation> loc) { push(OpCode::STRING_CURSOR_SET_POS, {}, std::move(loc)); }

void FunctionBuilder::call(uint32_t arg_count, std::optional<SourceLocation> loc) {
  push(OpCode::CALL, {Value::makeInt(arg_count)}, std::move(loc));
}

void FunctionBuilder::tail_call(uint32_t arg_count, std::optional<SourceLocation> loc) {
  push(OpCode::TAIL_CALL, {Value::makeInt(arg_count)}, std::move(loc));
}

BytecodeFunction FunctionBuilder::build() {
  BytecodeFunction func(impl->name, impl->param_count,
                        static_cast<uint32_t>(impl->locals.size()));
  func.blocks = std::move(impl->blocks);
  func.entry_block = 0;
  func.locals = std::move(impl->locals);
  func.global_names = std::move(impl->global_names);
  // Upvalue descriptors: one per captured upvalue index.
  func.upvalues.reserve(impl->upvalue_indices.size());
  for (uint32_t idx : impl->upvalue_indices) {
    func.upvalues.push_back(UpvalueDescriptor{idx, true});
  }
  return func;
}

std::vector<BasicBlock>& FunctionBuilder::blocks() { return impl->blocks; }
const std::vector<BasicBlock>& FunctionBuilder::blocks() const { return impl->blocks; }

uint32_t FunctionBuilder::add_local(LocalInfo info) {
  uint32_t idx = static_cast<uint32_t>(impl->locals.size());
  impl->locals.push_back(std::move(info));
  return idx;
}

uint32_t FunctionBuilder::add_upvalue(uint32_t index) {
  impl->upvalue_indices.push_back(index);
  return static_cast<uint32_t>(impl->upvalue_indices.size() - 1);
}

void FunctionBuilder::set_local_type(uint32_t local_idx, uint64_t type_hint) {
  if (local_idx < impl->locals.size()) {
    impl->locals[local_idx].type_hint = type_hint;
  }
}

void FunctionBuilder::set_location(SourceLocation loc) {
  if (!impl->blocks.empty()) {
    impl->blocks[impl->current].location = std::move(loc);
  }
}

}  // namespace havel::compiler
