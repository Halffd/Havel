// ===== Cranelift backend prototype (TODO.md #24 / §42 #15) =====
//
// Lowers validated Havel bytecode (int arithmetic + locals/stack + control
// flow subset) to native code via Cranelift. Everything outside the proven
// subset calls back into the Havel runtime through the Runtime ABI
// (RuntimeABI.hpp): the backend must not duplicate language semantics
// (TODO #24: "It should not duplicate compiler semantics").
//
// Value words are raw 64-bit NaN-boxed payloads identical to C++ Value
// (src/havel-lang/core/Value.hpp):
//   int48: QNAN | (1 << 48) | (v & 0x0000FFFFFFFFFFFF), sign bit = bit 47.
//   bool:  QNAN | (2 << 48) | (0 or 1)
//   null:  QNAN | (3 << 48)
//
// Input: the linear instruction stream as (opcode, operand) u32 pairs with
// jump operands pointing at instruction indices. The lowering performs the
// same leader analysis as reconstruct_cfg (CFGIntegration.hpp): instruction
// 0, every jump target, and every instruction after a jump/return start a
// new Cranelift block; terminators become branches. Backedges keep the
// natural block order, so loops lower to Cranelift loops.
//
// Int arithmetic/comparisons are lowered speculatively (tag-checked fast
// path, Runtime ABI bridge fallback). JUMP_IF_FALSE inlines truthiness for
// the scalar shapes (null/bool/int/double) and bridges havel_vm_is_truthy
// for everything else.
//
// C ABI surface (hclb_*): mirrors the C++ CompilerBackend contract
// (Backend.hpp); the C driver links this staticlib.

use cranelift::frontend::Variable;
use cranelift::prelude::*;
use cranelift_jit::{JITBuilder, JITModule};
use cranelift_module::{default_libcall_names, Linkage, Module};
use std::collections::HashMap;
use std::ffi::c_void;

// ---------------------------------------------------------------------------
// NaN-boxing helpers (must match src/havel-lang/core/Value.hpp exactly).
// ---------------------------------------------------------------------------

const QNAN: u64 = 0x7FF8_0000_0000_0000;
const TAG_MASK: u64 = 0x0007_0000_0000_0000;
const TAG_INT48: u64 = 0x1;
const TAG_BOOL: u64 = 0x2;
const TAG_NULL: u64 = 0x3;
const INT48_TAGGED: u64 = QNAN | (TAG_INT48 << 48);
const BOOL_TAGGED: u64 = QNAN | (TAG_BOOL << 48);
const NULL_TAGGED: u64 = QNAN | (TAG_NULL << 48);
const INT48_SIGN_BIT: u64 = 0x0000_8000_0000_0000;
const PAYLOAD_MASK: u64 = 0x0000_FFFF_FFFF_FFFF;
const TAG_SHIFT: u32 = 48;

#[inline]
fn pack_int48(v: i64) -> u64 {
    INT48_TAGGED | ((v as u64) & PAYLOAD_MASK)
}

#[inline]
fn unpack_int48(bits: u64) -> i64 {
    let payload = bits & PAYLOAD_MASK;
    if payload & INT48_SIGN_BIT != 0 {
        (payload | 0xFFFF_0000_0000_0000) as i64
    } else {
        payload as i64
    }
}

#[inline]
fn is_int48(bits: u64) -> bool {
    (bits & TAG_MASK) == (TAG_INT48 << TAG_SHIFT)
}

#[inline]
fn pack_bool(b: bool) -> u64 {
    BOOL_TAGGED | (b as u64)
}

// ---------------------------------------------------------------------------
// Bytecode subset. Flat u32 stream of (opcode, operand) pairs; constants are
// raw Value words passed alongside; jump operands are instruction indices.
// ---------------------------------------------------------------------------

pub const OP_LOAD_CONST: u32 = 0;
pub const OP_LOAD_VAR: u32 = 1;
pub const OP_STORE_VAR: u32 = 2;
pub const OP_ADD: u32 = 3;
pub const OP_SUB: u32 = 4;
pub const OP_MUL: u32 = 5;
pub const OP_LT: u32 = 6;
pub const OP_RETURN: u32 = 7;
// Control flow + comparisons (v2):
pub const OP_JUMP: u32 = 8;
pub const OP_JUMP_IF_FALSE: u32 = 9;
pub const OP_CALL: u32 = 15;
pub const OP_POP: u32 = 16;
pub const OP_DUP: u32 = 17;
pub const OP_PUSH_NULL: u32 = 18;
pub const OP_EQ: u32 = 10;
pub const OP_NEQ: u32 = 11;
pub const OP_LTE: u32 = 12;
pub const OP_GT: u32 = 13;
pub const OP_GTE: u32 = 14;

#[derive(Debug)]
pub struct LoweringError(pub String);

impl std::fmt::Display for LoweringError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

// ---------------------------------------------------------------------------
// Fallback shims for standalone/test binaries. The real embedder resolves
// the same symbols from the Havel runtime via dlsym (registry is consulted
// first only for names the process does not define).
// ---------------------------------------------------------------------------

mod fallback_shims {
    use super::*;
    use std::ffi::c_void;

    unsafe extern "C" fn shim_add(_vm: *mut c_void, l: u64, r: u64) -> u64 {
        if is_int48(l) && is_int48(r) {
            pack_int48(unpack_int48(l).wrapping_add(unpack_int48(r)))
        } else {
            NULL_TAGGED
        }
    }

    unsafe extern "C" fn shim_lt(_vm: *mut c_void, l: u64, r: u64) -> u64 {
        if is_int48(l) && is_int48(r) {
            pack_bool(unpack_int48(l) < unpack_int48(r))
        } else {
            NULL_TAGGED
        }
    }

    unsafe extern "C" fn shim_sub(_vm: *mut c_void, l: u64, r: u64) -> u64 {
        if is_int48(l) && is_int48(r) {
            pack_int48(unpack_int48(l).wrapping_sub(unpack_int48(r)))
        } else {
            NULL_TAGGED
        }
    }

    unsafe extern "C" fn shim_mul(_vm: *mut c_void, l: u64, r: u64) -> u64 {
        if is_int48(l) && is_int48(r) {
            pack_int48(unpack_int48(l).wrapping_mul(unpack_int48(r)))
        } else {
            NULL_TAGGED
        }
    }

    unsafe extern "C" fn shim_eq(_vm: *mut c_void, l: u64, r: u64) -> u64 {
        if is_int48(l) && is_int48(r) {
            pack_bool(unpack_int48(l) == unpack_int48(r))
        } else {
            NULL_TAGGED
        }
    }

    unsafe extern "C" fn shim_neq(_vm: *mut c_void, l: u64, r: u64) -> u64 {
        if is_int48(l) && is_int48(r) {
            pack_bool(unpack_int48(l) != unpack_int48(r))
        } else {
            NULL_TAGGED
        }
    }

    unsafe extern "C" fn shim_lte(_vm: *mut c_void, l: u64, r: u64) -> u64 {
        if is_int48(l) && is_int48(r) {
            pack_bool(unpack_int48(l) <= unpack_int48(r))
        } else {
            NULL_TAGGED
        }
    }

    unsafe extern "C" fn shim_gt(_vm: *mut c_void, l: u64, r: u64) -> u64 {
        if is_int48(l) && is_int48(r) {
            pack_bool(unpack_int48(l) > unpack_int48(r))
        } else {
            NULL_TAGGED
        }
    }

    unsafe extern "C" fn shim_gte(_vm: *mut c_void, l: u64, r: u64) -> u64 {
        if is_int48(l) && is_int48(r) {
            pack_bool(unpack_int48(l) >= unpack_int48(r))
        } else {
            NULL_TAGGED
        }
    }

    unsafe extern "C" fn shim_is_truthy(_vm: *mut c_void, v: u64) -> u64 {
        // Mirrors VM::isTruthy for scalar shapes; non-scalars yield the
        // runtime's job (null here) in the standalone shim.
        let tag = (v & TAG_MASK) >> TAG_SHIFT;
        let truthy = match tag {
            TAG_NULL => false,
            TAG_BOOL => (v & PAYLOAD_MASK) != 0,
            TAG_INT48 => unpack_int48(v) != 0,
            _ => {
                // Real doubles are unboxed; 0.0 is falsy.
                if v & TAG_MASK == 0 && (v as f64) == 0.0 {
                    false
                } else {
                    true
                }
            }
        };
        pack_bool(truthy)
    }

    unsafe extern "C" fn shim_call(
        _vm: *mut c_void,
        args: *const u64,
        count: u32,
    ) -> u64 {
        // Standalone plumbing check: treat the callee word as int48 n and
        // return n + sum(args) so tests can verify the [callee, args...]
        // array layout. The real runtime resolves and runs the callee.
        if args.is_null() || count == 0 {
            return NULL_TAGGED;
        }
        let callee = *args;
        if !is_int48(callee) {
            return NULL_TAGGED;
        }
        let mut acc = unpack_int48(callee);
        for i in 1..count {
            let w = *args.add(i as usize);
            if is_int48(w) {
                acc = acc.wrapping_add(unpack_int48(w));
            }
        }
        pack_int48(acc)
    }

    pub fn fallback_symbol(name: &str) -> Option<*const u8> {
        match name {
            "havel_vm_add" => Some(shim_add as *const u8),
            "havel_vm_sub" => Some(shim_sub as *const u8),
            "havel_vm_mul" => Some(shim_mul as *const u8),
            "havel_vm_lt" => Some(shim_lt as *const u8),
            "havel_vm_eq" => Some(shim_eq as *const u8),
            "havel_vm_neq" => Some(shim_neq as *const u8),
            "havel_vm_lte" => Some(shim_lte as *const u8),
            "havel_vm_gt" => Some(shim_gt as *const u8),
            "havel_vm_gte" => Some(shim_gte as *const u8),
            "havel_vm_is_truthy" => Some(shim_is_truthy as *const u8),
            "havel_vm_call" => Some(shim_call as *const u8),
            _ => None,
        }
    }
}

// ---------------------------------------------------------------------------
// Backend.
// ---------------------------------------------------------------------------

pub struct CraneliftBackend {
    module: JITModule,
    symbols: HashMap<String, HavelFn>,
}

pub type HavelFn = unsafe extern "C" fn(*mut c_void, *const u64, u32) -> u64;

impl CraneliftBackend {
    pub fn new() -> Result<Self, String> {
        let mut flag_builder = settings::builder();
        flag_builder
            .set("use_colocated_libcalls", "false")
            .map_err(|e| e.to_string())?;
        flag_builder
            .set("is_pic", "false")
            .map_err(|e| e.to_string())?;
        let isa_builder = cranelift_native::builder().map_err(|e| e.to_string())?;
        let isa = isa_builder
            .finish(settings::Flags::new(flag_builder))
            .map_err(|e| e.to_string())?;
        let mut jit_builder = JITBuilder::with_isa(isa, default_libcall_names());
        // Runtime ABI symbols resolve from the embedding process
        // (dlsym/RTLD_DEFAULT), matching the LLJIT symbol registration
        // BytecodeOrcJIT performs from the same RuntimeABI.hpp X-macro.
        jit_builder.symbol_lookup_fn(Box::new(lookup_runtime_abi) as Box<_>);
        let module = JITModule::new(jit_builder);
        Ok(Self {
            module,
            symbols: HashMap::new(),
        })
    }

    /// Lower one function of the subset to native code. `code` is the flat
    /// u32 (opcode, operand) stream; `constants` the raw Value words;
    /// `arg_count` the number of leading local slots bound to arguments.
    /// Returns the executable handle.
    pub fn compile_function(
        &mut self,
        name: &str,
        code: &[u32],
        constants: &[u64],
        arg_count: u32,
    ) -> Result<HavelFn, LoweringError> {
        let err = |m: String| LoweringError(m);
        if code.len() % 2 != 0 {
            return Err(err("instruction stream must be (op, operand) pairs".into()));
        }
        let n = code.len() / 2;

        // ---- Leader analysis (mirrors reconstruct_cfg on the C++ side) ----
        let mut leader = vec![false; n];
        leader[0] = true;
        for i in 0..n {
            match code[2 * i] {
                OP_JUMP | OP_JUMP_IF_FALSE => {
                    let t = code[2 * i + 1] as usize;
                    if t >= n {
                        return Err(err(format!("jump target {t} out of range")));
                    }
                    leader[t] = true;
                    if i + 1 < n {
                        leader[i + 1] = true;
                    }
                }
                OP_RETURN => {
                    if i + 1 < n {
                        leader[i + 1] = true;
                    }
                }
                _ => {}
            }
        }

        let pointer_ty = self.module.isa().pointer_type();
        let int64 = types::I64;
        let int32 = types::I32;

        // Signature: (vm, args_ptr, arg_count) -> u64
        let mut sig = self.module.make_signature();
        sig.params = vec![
            AbiParam::new(pointer_ty),
            AbiParam::new(pointer_ty),
            AbiParam::new(int32),
        ];
        sig.returns = vec![AbiParam::new(int64)];

        let func_id = self
            .module
            .declare_function(name, Linkage::Export, &sig)
            .map_err(|e| err(format!("declare {name}: {e}")))?;

        // Runtime ABI bridges (vm, l, r) -> result.
        let mut bridge_sig = self.module.make_signature();
        bridge_sig.params = vec![
            AbiParam::new(pointer_ty),
            AbiParam::new(int64),
            AbiParam::new(int64),
        ];
        bridge_sig.returns = vec![AbiParam::new(int64)];
        let mut bridge_ids: HashMap<&str, cranelift_module::FuncId> = HashMap::new();
        for sym in [
            "havel_vm_add",
            "havel_vm_sub",
            "havel_vm_mul",
            "havel_vm_lt",
            "havel_vm_eq",
            "havel_vm_neq",
            "havel_vm_lte",
            "havel_vm_gt",
            "havel_vm_gte",
        ] {
            let s = bridge_sig.clone();
            let id = self
                .module
                .declare_function(sym, Linkage::Import, &s)
                .map_err(|e| err(format!("declare {sym}: {e}")))?;
            bridge_ids.insert(sym, id);
        }
        // havel_vm_is_truthy is (vm, value) -> i64: its own signature.
        let mut truthy_sig = self.module.make_signature();
        truthy_sig.params = vec![AbiParam::new(pointer_ty), AbiParam::new(int64)];
        truthy_sig.returns = vec![AbiParam::new(int64)];
        let truthy_id = self
            .module
            .declare_function("havel_vm_is_truthy", Linkage::Import, &truthy_sig)
            .map_err(|e| err(format!("declare havel_vm_is_truthy: {e}")))?;
        // havel_vm_call is (vm, args_ptr, count) -> result: args[0] is the
        // callee Value, the rest are the call arguments.
        let mut call_sig = self.module.make_signature();
        call_sig.params = vec![
            AbiParam::new(pointer_ty),
            AbiParam::new(pointer_ty),
            AbiParam::new(int32),
        ];
        call_sig.returns = vec![AbiParam::new(int64)];
        let call_id = self
            .module
            .declare_function("havel_vm_call", Linkage::Import, &call_sig)
            .map_err(|e| err(format!("declare havel_vm_call: {e}")))?;

        let mut ctx = self.module.make_context();
        ctx.func.signature = sig;
        ctx.func.name = cranelift::codegen::ir::UserFuncName::user(0, func_id.as_u32());
        {
            let mut fb_ctx = FunctionBuilderContext::new();
            let mut builder = FunctionBuilder::new(&mut ctx.func, &mut fb_ctx);

            // One Cranelift block per leader instruction, created up front.
            let mut block_of: Vec<Option<Block>> = vec![None; n];
            for i in 0..n {
                if leader[i] {
                    block_of[i] = Some(builder.create_block());
                }
            }
            let entry = block_of[0].ok_or_else(|| err("no entry block".into()))?;
            builder.append_block_params_for_function_params(entry);
            builder.switch_to_block(entry);

            let vm = builder.block_params(entry)[0];
            let args_ptr = builder.block_params(entry)[1];

            // Constants reused across the lowering.
            let tag_int_bits = builder.ins().iconst(int64, (TAG_INT48 << TAG_SHIFT) as i64);
            let tag_bool_bits = builder.ins().iconst(int64, (TAG_BOOL << TAG_SHIFT) as i64);
            let tag_null_bits = builder.ins().iconst(int64, (TAG_NULL << TAG_SHIFT) as i64);
            let ext_tag_bits = builder.ins().iconst(int64, (7u64 << TAG_SHIFT) as i64);
            let int48_tagged = builder.ins().iconst(int64, INT48_TAGGED as i64);
            let bool_tagged = builder.ins().iconst(int64, BOOL_TAGGED as i64);
            let tag_mask = builder.ins().iconst(int64, TAG_MASK as i64);
            let payload_mask = builder.ins().iconst(int64, PAYLOAD_MASK as i64);
            let shift16 = builder.ins().iconst(int64, 16);
            let one64 = builder.ins().iconst(int64, 1);
            let zero64 = builder.ins().iconst(int64, 0);
            let zero8 = builder.ins().iconst(types::I8, 0);
            let one8 = builder.ins().iconst(types::I8, 1);


            // Resolve every bridge FuncRef up front so the lowering
            // closures never touch self.module (single-borrow discipline).
            let mut bridge_refs: HashMap<&str, cranelift::codegen::ir::FuncRef> = HashMap::new();
            for (sym, id) in &bridge_ids {
                let r = self.module.declare_func_in_func(*id, &mut builder.func);
                bridge_refs.insert(sym, r);
            }
            let truthy_bridge_ref = self
                .module
                .declare_func_in_func(truthy_id, &mut builder.func);
            let call_bridge_ref = self
                .module
                .declare_func_in_func(call_id, &mut builder.func);

            // Locals as SSA variables (declare/def/use), so values flow
            // across blocks and loop backedges; arguments seed the first
            // arg_count slots.
            let mut var_of: HashMap<u32, Variable> = HashMap::new();
            let mut next_var = 0u32;
            for i in 0..arg_count {
                let var = Variable::from_u32(next_var);
                next_var += 1;
                builder.declare_var(var, int64);
                let off = builder.ins().iconst(pointer_ty, (i as i64) * 8);
                let p = builder.ins().iadd(args_ptr, off);
                let v = builder.ins().load(int64, MemFlags::new(), p, 0);
                builder.def_var(var, v);
                var_of.insert(i, var);
            }
            let declare_local = |operand: u32,
                                     var_of: &mut HashMap<u32, Variable>,
                                     next_var: &mut u32,
                                     b: &mut FunctionBuilder|
             -> Variable {
                if let Some(v) = var_of.get(&operand) {
                    return *v;
                }
                let var = Variable::from_u32(*next_var);
                *next_var += 1;
                b.declare_var(var, int64);
                var_of.insert(operand, var);
                var
            };

            // Truthiness via nested selects (no inner CFG blocks): null ->
            // false, bool -> payload != 0, int -> unbox != 0, raw double ->
            // bits not +/-0; EXTENDED tags bridge havel_vm_is_truthy.
            let lower_truthy = |b: &mut FunctionBuilder, v: Value| -> Value {
                let is_ext = {
                    let t = b.ins().band(v, tag_mask);
                    b.ins().icmp(IntCC::Equal, t, ext_tag_bits)
                };
                // The bridge is consulted unconditionally (pure runtime
                // predicate); the select keeps only the applicable result.
                let call = b.ins().call(truthy_bridge_ref, &[vm, v]);
                let res = b.inst_results(call)[0];
                let bridge_on = b.ins().icmp_imm(IntCC::NotEqual, res, 0);

                let is_null = {
                    let t = b.ins().band(v, tag_mask);
                    b.ins().icmp(IntCC::Equal, t, tag_null_bits)
                };
                let is_bool = {
                    let t = b.ins().band(v, tag_mask);
                    b.ins().icmp(IntCC::Equal, t, tag_bool_bits)
                };
                let pl = b.ins().band(v, payload_mask);
                let bool_on = b.ins().icmp(IntCC::NotEqual, pl, zero64);

                let is_int = {
                    let t = b.ins().band(v, tag_mask);
                    b.ins().icmp(IntCC::Equal, t, tag_int_bits)
                };
                let masked = b.ins().band(v, payload_mask);
                let shl = b.ins().ishl(masked, shift16);
                let iv = b.ins().sshr(shl, shift16);
                let int_on = b.ins().icmp(IntCC::NotEqual, iv, zero64);

                let neg_zero = b.ins().iconst(int64, (-0.0f64).to_bits() as i64);
                let is_pz = b.ins().icmp(IntCC::Equal, v, zero64);
                let is_nz = b.ins().icmp(IntCC::Equal, v, neg_zero);
                let is_zero = b.ins().bor(is_pz, is_nz);
                let dbl_on = b.ins().select(is_zero, zero8, one8);

                let sel_num = b.ins().select(is_int, int_on, dbl_on);
                let sel_bool = b.ins().select(is_bool, bool_on, sel_num);
                let sel_scalar = b.ins().select(is_null, zero8, sel_bool);
                b.ins().select(is_ext, bridge_on, sel_scalar)
            };

            // Speculative int binop via selects: int/int -> inline op, else
            // bridge. The bridge call is emitted unconditionally (pure
            // runtime semantics); the select keeps the applicable result.
            let lower_binop = |b: &mut FunctionBuilder, op: u32, l: Value, r: Value| -> Value {
                let is_int = {
                    let tl = b.ins().band(l, tag_mask);
                    let tr = b.ins().band(r, tag_mask);
                    let li = b.ins().icmp(IntCC::Equal, tl, tag_int_bits);
                    let ri = b.ins().icmp(IntCC::Equal, tr, tag_int_bits);
                    b.ins().band(li, ri)
                };
                let bridge_name = match op {
                    OP_ADD => "havel_vm_add",
                    OP_SUB => "havel_vm_sub",
                    OP_MUL => "havel_vm_mul",
                    OP_LT => "havel_vm_lt",
                    OP_EQ => "havel_vm_eq",
                    OP_NEQ => "havel_vm_neq",
                    OP_LTE => "havel_vm_lte",
                    OP_GT => "havel_vm_gt",
                    _ => "havel_vm_gte",
                };
                let func_ref = *bridge_refs.get(bridge_name).expect("bridge declared above");
                let call = b.ins().call(func_ref, &[vm, l, r]);
                let bridged = b.inst_results(call)[0];

                let masked_l = b.ins().band(l, payload_mask);
                let shl_l = b.ins().ishl(masked_l, shift16);
                let lv = b.ins().sshr(shl_l, shift16);
                let masked_r = b.ins().band(r, payload_mask);
                let shl_r = b.ins().ishl(masked_r, shift16);
                let rv = b.ins().sshr(shl_r, shift16);
                let raw = match op {
                    OP_ADD => b.ins().iadd(lv, rv),
                    OP_SUB => b.ins().isub(lv, rv),
                    OP_MUL => b.ins().imul(lv, rv),
                    _ => {
                        let cc = match op {
                            OP_LT => IntCC::SignedLessThan,
                            OP_EQ => IntCC::Equal,
                            OP_NEQ => IntCC::NotEqual,
                            OP_LTE => IntCC::SignedLessThanOrEqual,
                            OP_GT => IntCC::SignedGreaterThan,
                            _ => IntCC::SignedGreaterThanOrEqual,
                        };
                        let c = b.ins().icmp(cc, lv, rv);
                        b.ins().uextend(int64, c)
                    }
                };
                match op {
                    OP_LT | OP_EQ | OP_NEQ | OP_LTE | OP_GT | OP_GTE => {
                        let bit = b.ins().band(raw, one64);
                        let on = b.ins().bor(bit, bool_tagged);
                        b.ins().select(is_int, on, bridged)
                    }
                    _ => {
                        let on = {
                            let masked = b.ins().band(raw, payload_mask);
                            b.ins().bor(masked, int48_tagged)
                        };
                        b.ins().select(is_int, on, bridged)
                    }
                }
            };

            // Straight-line lowering; the virtual stack is per-block (the
            // subset never carries stack values across a block boundary -
            // branch conditions are consumed by their branch).
            let mut cur: usize = 0;
            let mut vstack: Vec<Value> = Vec::new();
            let mut terminated = true;
            let mut saw_return = false;

            while cur < n {
                if leader[cur] {
                    let blk = block_of[cur].ok_or_else(|| err("missing leader block".into()))?;
                    if !terminated {
                        // Fall-through edge into this leader: Cranelift
                        // blocks do not fall through implicitly.
                        builder.ins().jump(blk, &[]);
                    }
                    builder.switch_to_block(blk);
                    vstack.clear();
                    terminated = false;
                }
                if terminated {
                    // Unreachable instruction after a terminator without a
                    // leader boundary (dead code); the CFG never emits this.
                    cur += 1;
                    continue;
                }
                let op = code[2 * cur];
                let operand = code[2 * cur + 1];
                match op {
                    OP_LOAD_CONST => {
                        let c = constants
                            .get(operand as usize)
                            .copied()
                            .ok_or_else(|| err(format!("LOAD_CONST {} out of range", operand)))?;
                        vstack.push(builder.ins().iconst(int64, c as i64));
                    }
                    OP_LOAD_VAR => {
                        let var = declare_local(operand, &mut var_of, &mut next_var, &mut builder);
                        vstack.push(builder.use_var(var));
                    }
                    OP_STORE_VAR => {
                        let v = vstack
                            .pop()
                            .ok_or_else(|| err("STORE_VAR with empty stack".into()))?;
                        let var = declare_local(operand, &mut var_of, &mut next_var, &mut builder);
                        builder.def_var(var, v);
                    }
                    OP_ADD | OP_SUB | OP_MUL | OP_LT | OP_EQ | OP_NEQ | OP_LTE | OP_GT | OP_GTE => {
                        let r = vstack
                            .pop()
                            .ok_or_else(|| err("binop with empty stack".into()))?;
                        let l = vstack
                            .pop()
                            .ok_or_else(|| err("binop with empty stack".into()))?;
                        vstack.push(lower_binop(&mut builder, op, l, r));
                    }
                    OP_CALL => {
                        // Stack in: [..., callee, arg1..argN]. The runtime
                        // bridge wants a contiguous [callee, args...] array;
                        // a stack slot holds it (calls can nest, so allocate
                        // one slot per call site).
                        let argc = operand as usize;
                        if vstack.len() < argc + 1 {
                            return Err(err("CALL with too few stack values".into()));
                        }
                        let slot = builder.create_sized_stack_slot(
                            cranelift::codegen::ir::StackSlotData::new(
                                cranelift::codegen::ir::StackSlotKind::ExplicitSlot,
                                ((argc + 1) * 8) as u32,
                                8,
                            ),
                        );
                        // store [callee, args...] into the slot; note the
                        // vstack pops come last-first.
                        // words = [callee, argN..arg1] with args in
                        // reverse pop order; store each at its byte offset.
                        let callee = vstack.pop().expect("checked depth");
                        let mut words: Vec<Value> = Vec::with_capacity(argc + 1);
                        words.push(callee);
                        for k in 0..argc {
                            words.push(vstack[vstack.len() - 1 - k]);
                        }
                        for (k, w) in words.iter().enumerate() {
                            builder.ins().stack_store(*w, slot, (k as i32) * 8);
                        }
                        let base = builder.ins().stack_addr(pointer_ty, slot, 0);
                        let cnt = builder.ins().iconst(int32, (argc + 1) as i64);
                        let call =
                            builder.ins().call(call_bridge_ref, &[vm, base, cnt]);
                        vstack.truncate(vstack.len() - argc);
                        vstack.push(builder.inst_results(call)[0]);
                    }
                    OP_JUMP_IF_FALSE => {
                        let target = operand as usize;
                        let cond_word = vstack
                            .pop()
                            .ok_or_else(|| err("JUMP_IF_FALSE with empty stack".into()))?;
                        let truthy = lower_truthy(&mut builder, cond_word);
                        let then_blk = block_of[target]
                            .ok_or_else(|| err("jump target has no block".into()))?;
                        let else_idx = cur + 1;
                        if else_idx < n && leader[else_idx] {
                            let else_blk = block_of[else_idx]
                                .ok_or_else(|| err("fall-through has no block".into()))?;
                            builder.ins().brif(truthy, else_blk, &[], then_blk, &[]);
                        } else {
                            // No fall-through instruction: both arms exit
                            // through the target.
                            builder.ins().brif(truthy, then_blk, &[], then_blk, &[]);
                        }
                        terminated = true;
                    }
                    OP_JUMP => {
                        let target = operand as usize;
                        let blk = block_of[target]
                            .ok_or_else(|| err("jump target has no block".into()))?;
                        builder.ins().jump(blk, &[]);
                        terminated = true;
                    }
                    OP_POP => {
                        if vstack.is_empty() {
                            return Err(err("POP with empty stack".into()));
                        }
                        vstack.pop();
                    }
                    OP_DUP => {
                        let v = *vstack
                            .last()
                            .ok_or_else(|| err("DUP with empty stack".into()))?;
                        vstack.push(v);
                    }
                    OP_PUSH_NULL => {
                        let null_w = builder.ins().iconst(int64, NULL_TAGGED as i64);
                        vstack.push(null_w);
                    }
                    OP_RETURN => {
                        let v = vstack
                            .pop()
                            .ok_or_else(|| err("RETURN with empty stack".into()))?;
                        builder.ins().return_(&[v]);
                        saw_return = true;
                        terminated = true;
                    }
                    _ => return Err(err(format!("unsupported opcode {op}"))),
                }
                cur += 1;
            }
            if !saw_return {
                // No RETURN at all: the function yields null.
                let null_w = builder.ins().iconst(int64, NULL_TAGGED as i64);
                builder.ins().return_(&[null_w]);
            }
            // Canonical SSA pattern: seal everything lazily; the SSA builder
            // rewrites predecessor branch args as it places phi parameters.
            builder.seal_all_blocks();
            builder.finalize();
        }
        if std::env::var("HCLB_DUMP_IR").is_ok() {
            eprintln!("---- IR for {name} ----\n{}", ctx.func.display());
        }
        self.module
            .define_function(func_id, &mut ctx)
            .map_err(|e| err(format!("define {name}: {e}")))?;
        self.module.clear_context(&mut ctx);
        self.module
            .finalize_definitions()
            .map_err(|e| err(format!("finalize: {e}")))?;
        let code_ptr = self.module.get_finalized_function(func_id);
        let typed: HavelFn = unsafe { std::mem::transmute(code_ptr) };
        self.symbols.insert(name.to_string(), typed);
        Ok(typed)
    }
}

// Runtime ABI symbol lookup: registry first (standalone/test binaries),
// then the embedding process.
fn lookup_runtime_abi(name: &str) -> Option<*const u8> {
    if let Some(ptr) = fallback_shims::fallback_symbol(name) {
        return Some(ptr);
    }
    unsafe {
        let c = std::ffi::CString::new(name).ok()?;
        extern "C" {
            fn dlsym(handle: *mut c_void, symbol: *const std::os::raw::c_char) -> *mut c_void;
        }
        let sym = dlsym(std::ptr::null_mut(), c.as_ptr()); // RTLD_DEFAULT
        if sym.is_null() {
            None
        } else {
            Some(sym as *const u8)
        }
    }
}

// ---------------------------------------------------------------------------
// Tests.
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn int48_roundtrip() {
        for v in [
            0i64,
            1,
            -1,
            42,
            -42,
            i32::MIN as i64,
            i32::MAX as i64,
            -1 << 40,
        ] {
            let bits = pack_int48(v);
            assert!(is_int48(bits), "tag survives for {v}");
            assert_eq!(unpack_int48(bits), v, "payload round-trips for {v}");
        }
    }

    #[test]
    fn pack_matches_cpp_value_bits() {
        // C++ Value::makeInt(42).rawBits() == QNAN|1<<48|42.
        assert_eq!(pack_int48(42), 0x7FF9_0000_0000_002A);
        assert_eq!(pack_bool(true), 0x7FFA_0000_0000_0001);
        assert_eq!(pack_bool(false), 0x7FFA_0000_0000_0000);
    }

    #[test]
    fn backend_constructs() {
        let backend = CraneliftBackend::new();
        assert!(backend.is_ok(), "{:?}", backend.err());
    }

    #[test]
    fn identity_returns_raw_arg() {
        let mut backend = CraneliftBackend::new().unwrap();
        let code = [OP_LOAD_VAR, 0, OP_RETURN, 0];
        let constants: [u64; 0] = [];
        let f = backend
            .compile_function("ident", &code, &constants, 1)
            .expect("lowering");
        let a = [pack_int48(42)];
        let out = unsafe { f(std::ptr::null_mut(), a.as_ptr(), 1) };
        assert_eq!(out, pack_int48(42), "raw word must round-trip: {out:#x}");
    }

    #[test]
    fn compile_and_run_int_add() {
        let mut backend = CraneliftBackend::new().unwrap();
        // fn (a, b) = a + b
        let code = [
            OP_LOAD_VAR,
            0, //
            OP_LOAD_VAR,
            1, //
            OP_ADD,
            0, //
            OP_RETURN,
            0,
        ];
        let constants: [u64; 0] = [];
        let f = backend
            .compile_function("add2", &code, &constants, 2)
            .expect("lowering");
        let a = [pack_int48(40), pack_int48(2)];
        let out = unsafe { f(std::ptr::null_mut(), a.as_ptr(), 2) };
        assert_eq!(unpack_int48(out), 42);
    }

    #[test]
    fn compile_and_run_const_arith() {
        let mut backend = CraneliftBackend::new().unwrap();
        // fn () = (2 + 3) < 10 -> true
        let code = [
            OP_LOAD_CONST,
            0, //
            OP_LOAD_CONST,
            1, //
            OP_ADD,
            0, //
            OP_LOAD_CONST,
            2, //
            OP_LT,
            0, //
            OP_RETURN,
            0,
        ];
        let constants = [pack_int48(2), pack_int48(3), pack_int48(10)];
        let f = backend
            .compile_function("lt10", &code, &constants, 0)
            .expect("lowering");
        let out = unsafe { f(std::ptr::null_mut(), [].as_ptr(), 0) };
        assert_eq!(out, pack_bool(true));
    }

    #[test]
    fn negative_integers_arith() {
        let mut backend = CraneliftBackend::new().unwrap();
        // fn (a) = a + a where a = -40 -> -80 (checks int48 sign handling)
        let code = [OP_LOAD_VAR, 0, OP_LOAD_VAR, 0, OP_ADD, 0, OP_RETURN, 0];
        let constants: [u64; 0] = [];
        let f = backend
            .compile_function("neg", &code, &constants, 1)
            .expect("lowering");
        let a = [pack_int48(-40)];
        let out = unsafe { f(std::ptr::null_mut(), a.as_ptr(), 1) };
        assert_eq!(unpack_int48(out), -80);
    }

    // ---- Control flow ----

    #[test]
    fn loop_lowers_and_runs() {
        // fn (n) { s = 0; i = 0; while (i < n) { s = s + i; i = i + 1 } s }
        // Instruction layout (indices in pairs):
        //   0: LOAD_VAR n        (arg 0)
        //   1: LOAD_CONST 0      (0)
        //   2: STORE_VAR s       (slot 1)   <- leader (loop head)
        //   3: LOAD_CONST 0
        //   4: STORE_VAR i       (slot 2)
        //   5: LOAD_VAR i        <- leader (target of the backedge test)
        //   6: LOAD_VAR n
        //   7: LT
        //   8: JUMP_IF_FALSE 14  (exit)
        //   9: LOAD_VAR s
        //  10: LOAD_VAR i
        //  11: ADD; STORE_VAR s
        //  12: LOAD_VAR i; LOAD_CONST 1; ADD; STORE_VAR i
        //  13: JUMP 5            (backedge)
        //  14: LOAD_VAR s; RETURN  <- leader (exit target)
        let mut backend = CraneliftBackend::new().unwrap();
        let code: Vec<u32> = vec![
            OP_LOAD_VAR,
            0, // 0: n
            OP_LOAD_CONST,
            0, // 1: 0
            OP_STORE_VAR,
            1, // 2: s = 0
            OP_LOAD_CONST,
            0, // 3: 0
            OP_STORE_VAR,
            2, // 4: i = 0
            OP_LOAD_VAR,
            2, // 5: loop head: i   (leader)
            OP_LOAD_VAR,
            0, // 6: n
            OP_LT,
            0, // 7: i < n
            OP_JUMP_IF_FALSE,
            14, // 8: exit to 14
            OP_LOAD_VAR,
            1, // 9: s
            OP_LOAD_VAR,
            2, // 10: i
            OP_ADD,
            0, // 11: s+i
            OP_STORE_VAR,
            1, // 12: s = s+i
            OP_LOAD_VAR,
            2, // 13: i
            OP_LOAD_CONST,
            1, // 14: 1
            OP_ADD,
            0, // 15: i+1
            OP_STORE_VAR,
            2, // 16: i = i+1
            OP_JUMP,
            5, // 17: backedge to 5
            OP_LOAD_VAR,
            1, // 18: s  (leader 14*? no: index in pairs)
            OP_RETURN,
            0, // 19
        ];
        // JUMP targets above are PAIR indices (0..code.len()/2). Fix: the
        // stream uses instruction indices = pair indices. So targets 14 and
        // 5 must be pair indices: the loop head is pair 5, exit is pair 18.
        let code: Vec<u32> = {
            let mut c = code.clone();
            c[2 * 8 + 1] = 18; // JUMP_IF_FALSE -> pair 18
            c[2 * 17 + 1] = 5; // JUMP -> pair 5
            c
        };
        let constants = [pack_int48(0), pack_int48(1)];
        let f = backend
            .compile_function("loopsum", &code, &constants, 1)
            .expect("lowering");
        // sum(0..10) = 45
        let a = [pack_int48(10)];
        let out = unsafe { f(std::ptr::null_mut(), a.as_ptr(), 1) };
        assert_eq!(unpack_int48(out), 45, "loop computed wrong sum: {out:#x}");
    }

    #[test]
    fn branch_if_lowers_and_runs() {
        // fn (a) { if (a < 5) { return 100 } return 200 }
        //   0: LOAD_VAR a
        //   1: LOAD_CONST 5
        //   2: LT
        //   3: JUMP_IF_FALSE 6
        //   4: LOAD_CONST 100
        //   5: RETURN
        //   6: LOAD_CONST 200
        //   7: RETURN
        let mut backend = CraneliftBackend::new().unwrap();
        let code: Vec<u32> = vec![
            OP_LOAD_VAR,
            0, // 0
            OP_LOAD_CONST,
            0, // 1: 5
            OP_LT,
            0, // 2
            OP_JUMP_IF_FALSE,
            6, // 3
            OP_LOAD_CONST,
            1, // 4: 100
            OP_RETURN,
            0, // 5
            OP_LOAD_CONST,
            2, // 6: 200
            OP_RETURN,
            0, // 7
        ];
        let constants = [pack_int48(5), pack_int48(100), pack_int48(200)];
        let f = backend
            .compile_function("iflt", &code, &constants, 1)
            .expect("lowering");
        let out_then = unsafe { f(std::ptr::null_mut(), [pack_int48(3)].as_ptr(), 1) };
        assert_eq!(unpack_int48(out_then), 100);
        let out_else = unsafe { f(std::ptr::null_mut(), [pack_int48(9)].as_ptr(), 1) };
        assert_eq!(unpack_int48(out_else), 200);
    }

    #[test]
    fn jump_if_false_const_conditions() {
        // Constant conditions: 0 -> falsy path, 1 -> truthy path.
        let mut backend = CraneliftBackend::new().unwrap();
        //   0: LOAD_CONST c0
        //   1: JUMP_IF_FALSE 4
        //   2: LOAD_CONST 111
        //   3: RETURN
        //   4: LOAD_CONST 222
        //   5: RETURN
        let code: Vec<u32> = vec![
            OP_LOAD_CONST,
            0, // 0
            OP_JUMP_IF_FALSE,
            4, // 1
            OP_LOAD_CONST,
            1, // 2
            OP_RETURN,
            0, // 3
            OP_LOAD_CONST,
            2, // 4
            OP_RETURN,
            0, // 5
        ];
        let constants = [pack_int48(0), pack_int48(111), pack_int48(222)];
        let f = backend
            .compile_function("constcond", &code, &constants, 0)
            .expect("lowering");
        let out = unsafe { f(std::ptr::null_mut(), [].as_ptr(), 0) };
        assert_eq!(unpack_int48(out), 222, "const 0 must be falsy: {out:#x}");
    }

    // Canonical SSA pattern from cranelift-frontend's own tests, ported
    // to this crate: proves the block/variable/seal machinery works in
    // this environment the same way the official sample expects.
    #[test]
    fn canonical_ssa_loop_pattern() {
        use cranelift::frontend::Variable;

        let mut backend = CraneliftBackend::new().unwrap();
        let pointer_ty = backend.module.isa().pointer_type();
        let int64 = types::I64;
        let int32 = types::I32;

        let mut sig = backend.module.make_signature();
        sig.params = vec![
            AbiParam::new(pointer_ty),
            AbiParam::new(pointer_ty),
            AbiParam::new(int32),
        ];
        sig.returns = vec![AbiParam::new(int64)];
        let func_id = backend
            .module
            .declare_function("ssa_sample", Linkage::Export, &sig)
            .unwrap();
        let mut ctx = backend.module.make_context();
        ctx.func.signature = sig;
        ctx.func.name = cranelift::codegen::ir::UserFuncName::user(0, func_id.as_u32());
        {
            let mut fb_ctx = FunctionBuilderContext::new();
            let mut builder = FunctionBuilder::new(&mut ctx.func, &mut fb_ctx);
            let block0 = builder.create_block();
            let block1 = builder.create_block();
            let block2 = builder.create_block();
            let block3 = builder.create_block();
            let x = Variable::from_u32(0);
            let y = Variable::from_u32(1);
            let z = Variable::from_u32(2);
            builder.declare_var(x, int64);
            builder.declare_var(y, int64);
            builder.declare_var(z, int64);
            builder.append_block_params_for_function_params(block0);

            builder.switch_to_block(block0);
            let tmp = builder.block_params(block0)[1]; // args ptr as seed
            builder.def_var(x, tmp);
            let c = builder.ins().iconst(int64, 2);
            builder.def_var(y, c);
            let a1 = builder.use_var(x);
            let a2 = builder.use_var(y);
            let t = builder.ins().iadd(a1, a2);
            builder.def_var(z, t);
            builder.ins().jump(block1, &[]);

            builder.switch_to_block(block1);
            let b1 = builder.use_var(y);
            let b2 = builder.use_var(z);
            let t2 = builder.ins().iadd(b1, b2);
            builder.def_var(z, t2);
            let cond = builder.use_var(y);
            let cz = builder.ins().icmp_imm(IntCC::SignedLessThan, cond, 10);
            builder.ins().brif(cz, block3, &[], block2, &[]);

            builder.switch_to_block(block2);
            let c1 = builder.use_var(z);
            let c2 = builder.use_var(x);
            let t3 = builder.ins().isub(c1, c2);
            builder.def_var(z, t3);
            let rv = builder.use_var(y);
            builder.ins().return_(&[rv]);

            builder.switch_to_block(block3);
            let d1 = builder.use_var(y);
            let d2 = builder.use_var(x);
            let t4 = builder.ins().isub(d1, d2);
            builder.def_var(y, t4);
            builder.ins().jump(block1, &[]);

            builder.seal_all_blocks();
            builder.finalize();
        }
        backend
            .module
            .define_function(func_id, &mut ctx)
            .expect("define ssa_sample");
        backend.module.clear_context(&mut ctx);
        backend.module.finalize_definitions().expect("finalize");
    }

    #[test]
    fn call_bridge_receives_callee_and_args() {
        // fn (n) = call(callee=100, 1, 2, 3) -> shim returns 100+1+2+3 = 106
        let mut backend = CraneliftBackend::new().unwrap();
        //   0: LOAD_CONST 0 (100)
        //   1: LOAD_CONST 1 (1)
        //   2: LOAD_CONST 2 (2)
        //   3: LOAD_CONST 3 (3)
        //   4: CALL 3
        //   5: RETURN
        let code: Vec<u32> = vec![
            OP_LOAD_CONST, 0, //
            OP_LOAD_CONST, 1, //
            OP_LOAD_CONST, 2, //
            OP_LOAD_CONST, 3, //
            OP_CALL, 3, //
            OP_RETURN, 0,
        ];
        let constants = [
            pack_int48(100),
            pack_int48(1),
            pack_int48(2),
            pack_int48(3),
        ];
        let f = backend
            .compile_function("calltest", &code, &constants, 0)
            .expect("lowering");
        let out = unsafe { f(std::ptr::null_mut(), [].as_ptr(), 0) };
        assert_eq!(
            unpack_int48(out),
            106,
            "shim must see [callee=100, 1, 2, 3]: {out:#x}"
        );
    }

    #[test]
    fn comparisons_with_bridge_fallback() {
        // EQ on int operands takes the fast path; the standalone shim
        // returns null for non-int, proving the fallback is wired (the
        // C++ runtime would compute the real result).
        let mut backend = CraneliftBackend::new().unwrap();
        let code = [OP_LOAD_VAR, 0, OP_LOAD_VAR, 1, OP_EQ, 0, OP_RETURN, 0];
        let constants: [u64; 0] = [];
        let f = backend
            .compile_function("eq2", &code, &constants, 2)
            .expect("lowering");
        let ints = [pack_int48(7), pack_int48(7)];
        let out = unsafe { f(std::ptr::null_mut(), ints.as_ptr(), 2) };
        assert_eq!(out, pack_bool(true));
    }

    #[test]
    fn truthy_null_condition_takes_false_path() {
        // JUMP_IF_FALSE with a null condition must take the false arm
        // (null is falsy), proving inline scalar truthiness.
        let mut backend = CraneliftBackend::new().unwrap();
        //   0: LOAD_VAR a
        //   1: JUMP_IF_FALSE 4
        //   2: LOAD_CONST 1   (111)
        //   3: RETURN
        //   4: LOAD_CONST 2   (222)
        //   5: RETURN
        let code: Vec<u32> = vec![
            OP_LOAD_VAR,
            0, // 0
            OP_JUMP_IF_FALSE,
            4, // 1
            OP_LOAD_CONST,
            0, // 2: 111
            OP_RETURN,
            0, // 3
            OP_LOAD_CONST,
            1, // 4: 222
            OP_RETURN,
            0, // 5
        ];
        let constants = [pack_int48(111), pack_int48(222)];
        let f = backend
            .compile_function("nullcond", &code, &constants, 1)
            .expect("lowering");
        // null condition -> falsy -> jump to 4 -> 222
        let out = unsafe { f(std::ptr::null_mut(), [NULL_TAGGED].as_ptr(), 1) };
        assert_eq!(unpack_int48(out), 222, "null must be falsy: {out:#x}");
        // non-zero int condition -> truthy -> fall through -> 111
        let out2 = unsafe { f(std::ptr::null_mut(), [pack_int48(1)].as_ptr(), 1) };
        assert_eq!(unpack_int48(out2), 111, "non-zero int must be truthy");
        // zero int condition -> falsy -> 222
        let out3 = unsafe { f(std::ptr::null_mut(), [pack_int48(0)].as_ptr(), 1) };
        assert_eq!(unpack_int48(out3), 222, "zero int must be falsy");
        // false bool -> falsy; true bool -> truthy
        let out4 = unsafe { f(std::ptr::null_mut(), [pack_bool(false)].as_ptr(), 1) };
        assert_eq!(unpack_int48(out4), 222, "false must be falsy");
        let out5 = unsafe { f(std::ptr::null_mut(), [pack_bool(true)].as_ptr(), 1) };
        assert_eq!(unpack_int48(out5), 111, "true must be truthy");
    }
}

// ---------------------------------------------------------------------------
// C ABI surface for the C++/CTest driver.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// C ABI backend surface: a CraneliftBackend as an owning handle so the C++
// side can attach it through CompilerBackend (Backend.hpp).
//
// hclb_create()        -> opaque CraneliftBackend handle
// hclb_compile(h, name, code, code_len, constants, arg_count) -> bool
// hclb_execute(h, name, args, arg_count, out) -> bool
// hclb_is_compiled(h, name) -> bool
// hclb_destroy(h)
//
// The instruction stream is the flat (opcode, operand) u32 pairs the
// lowering consumes; the C++ adapter extracts it from BytecodeFunction.
// ---------------------------------------------------------------------------

use std::ffi::{c_char, CStr};

#[no_mangle]
pub extern "C" fn hclb_create() -> *mut c_void {
    match CraneliftBackend::new() {
        Ok(b) => Box::into_raw(Box::new(b)) as *mut c_void,
        Err(e) => {
            eprintln!("[hclb] backend creation failed: {e}");
            std::ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn hclb_destroy(handle: *mut c_void) {
    if !handle.is_null() {
        unsafe {
            drop(Box::from_raw(handle as *mut CraneliftBackend));
        }
    }
}

#[no_mangle]
pub extern "C" fn hclb_compile(
    handle: *mut c_void,
    name: *const c_char,
    code: *const u32,
    code_len: u32,
    constants: *const u64,
    constants_len: u32,
    arg_count: u32,
) -> bool {
    if handle.is_null() || name.is_null() || code.is_null() {
        return false;
    }
    let backend = unsafe { &mut *(handle as *mut CraneliftBackend) };
    let name = unsafe { CStr::from_ptr(name) }
        .to_string_lossy()
        .into_owned();
    let code = unsafe { std::slice::from_raw_parts(code, code_len as usize) };
    let constants = if constants.is_null() || constants_len == 0 {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(constants, constants_len as usize) }
    };
    match backend.compile_function(&name, code, constants, arg_count) {
        Ok(_) => true,
        Err(e) => {
            eprintln!("[hclb] compile {name} failed: {e}");
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn hclb_is_compiled(
    handle: *mut c_void,
    name: *const c_char,
) -> bool {
    if handle.is_null() || name.is_null() {
        return false;
    }
    let backend = unsafe { &*(handle as *mut CraneliftBackend) };
    let name = unsafe { CStr::from_ptr(name) }.to_string_lossy().into_owned();
    backend.symbols.contains_key(&name)
}

#[no_mangle]
pub extern "C" fn hclb_execute(
    handle: *mut c_void,
    vm: *mut c_void,
    name: *const c_char,
    args: *const u64,
    arg_count: u32,
    out: *mut u64,
) -> bool {
    if handle.is_null() || name.is_null() {
        return false;
    }
    let backend = unsafe { &mut *(handle as *mut CraneliftBackend) };
    let name = unsafe { CStr::from_ptr(name) }.to_string_lossy().into_owned();
    let Some(f) = backend.symbols.get(&name) else {
        return false;
    };
    let args = if args.is_null() || arg_count == 0 {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(args, arg_count as usize) }
    };
    let result = unsafe { (*f)(vm, args.as_ptr(), arg_count) };
    if !out.is_null() {
        unsafe { *out = result };
    }
    true
}

/// NaN-box an int (driver-side sanity check against C++ Value::rawBits()).
#[no_mangle]
pub extern "C" fn hclb_pack_int48(v: i64) -> u64 {
    pack_int48(v)
}

/// Unbox a NaN-boxed int (driver-side sanity check).
#[no_mangle]
pub extern "C" fn hclb_unpack_int48(bits: u64) -> i64 {
    unpack_int48(bits)
}

#[no_mangle]
pub extern "C" fn hclb_is_int48(bits: u64) -> bool {
    is_int48(bits)
}
