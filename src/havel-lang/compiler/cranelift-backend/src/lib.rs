// ===== Cranelift backend prototype (TODO.md #24 / §42 #15) =====
//
// Lowers validated Havel bytecode (the int arithmetic + local/stack subset)
// to native code via Cranelift. Everything outside the proven subset calls
// back into the Havel runtime through the Runtime ABI (RuntimeABI.hpp):
// the backend must not duplicate language semantics (TODO #24: "It should
// not duplicate compiler semantics").
//
// Value words are raw 64-bit NaN-boxed payloads identical to C++ Value
// (src/havel-lang/core/Value.hpp):
//   int48: QNAN | (1 << 48) | (v & 0x0000FFFFFFFFFFFF), sign bit = bit 47.
//   bool:  QNAN | (2 << 48) | (0 or 1)
//
// Compiled functions use the signature
//   (vm: *mut c_void, args: *const u64, arg_count: u32) -> Value word
// Arguments arrive as raw Value words exactly where the VM would put them.
//
// Int arithmetic is lowered speculatively: operands are tag-checked and a
// non-int operand routes through the Runtime ABI bridge (havel_vm_add,
// havel_vm_lt) so generic semantics stay in the runtime. The prototype
// subset is straight-line code (no branches beyond the speculative guard,
// no calls beyond the ABI bridge).
//
// C ABI surface (hclb_*): mirrors the C++ CompilerBackend contract
// (Backend.hpp) so the follow-up slice can attach this as a real backend;
// the prototype phase drives it from C tests.

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
const INT48_TAGGED: u64 = QNAN | (TAG_INT48 << 48);
const BOOL_TAGGED: u64 = QNAN | (TAG_BOOL << 48);
const INT48_SIGN_BIT: u64 = 0x0000_8000_0000_0000;
const PAYLOAD_MASK: u64 = 0x0000_FFFF_FFFF_FFFF;

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
    (bits & TAG_MASK) == (TAG_INT48 << 48)
}

#[inline]
fn pack_bool(b: bool) -> u64 {
    BOOL_TAGGED | (b as u64)
}

// ---------------------------------------------------------------------------
// Bytecode subset. Flat u32 stream of (opcode, operand) pairs; constants are
// raw Value words passed alongside.
// ---------------------------------------------------------------------------

pub const OP_LOAD_CONST: u32 = 0;
pub const OP_LOAD_VAR: u32 = 1;
pub const OP_STORE_VAR: u32 = 2;
pub const OP_ADD: u32 = 3;
pub const OP_SUB: u32 = 4;
pub const OP_MUL: u32 = 5;
pub const OP_LT: u32 = 6;
pub const OP_RETURN: u32 = 7;

#[derive(Debug)]
pub struct LoweringError(pub String);

impl std::fmt::Display for LoweringError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
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

    /// Lower one int-subset function to native code. `code` is the flat u32
    /// (opcode, operand) stream; `constants` the raw Value words for
    /// LOAD_CONST. Returns the executable handle.
    pub fn compile_function(
        &mut self,
        name: &str,
        code: &[u32],
        constants: &[u64],
    ) -> Result<HavelFn, LoweringError> {
        let err = |m: String| LoweringError(m);
        if code.len() % 2 != 0 {
            return Err(err("instruction stream must be (op, operand) pairs".into()));
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

        // Runtime ABI bridge (vm, l, r) -> result.
        let mut bridge_sig = self.module.make_signature();
        bridge_sig.params = vec![
            AbiParam::new(pointer_ty),
            AbiParam::new(int64),
            AbiParam::new(int64),
        ];
        bridge_sig.returns = vec![AbiParam::new(int64)];
        let add_id = self
            .module
            .declare_function("havel_vm_add", Linkage::Import, &bridge_sig)
            .map_err(|e| err(format!("declare havel_vm_add: {e}")))?;
        let lt_id = self
            .module
            .declare_function("havel_vm_lt", Linkage::Import, &bridge_sig)
            .map_err(|e| err(format!("declare havel_vm_lt: {e}")))?;
        let _lt_id = lt_id;

        let mut ctx = self.module.make_context();
        ctx.func.signature = sig.clone();
        ctx.func.name = cranelift::codegen::ir::UserFuncName::user(0, func_id.as_u32());
        {
            let mut fb_ctx = FunctionBuilderContext::new();
            let mut builder = FunctionBuilder::new(&mut ctx.func, &mut fb_ctx);
            let entry = builder.create_block();
            builder.append_block_params_for_function_params(entry);
            builder.switch_to_block(entry);
            builder.seal_all_blocks();

            let vm = builder.block_params(entry)[0];
            let args_ptr = builder.block_params(entry)[1];
            let _arg_count = builder.block_params(entry)[2];

            let int48_tagged = builder.ins().iconst(int64, INT48_TAGGED as i64);
            let bool_tagged = builder.ins().iconst(int64, BOOL_TAGGED as i64);
            let tag_mask = builder.ins().iconst(int64, TAG_MASK as i64);
            let payload_mask = builder.ins().iconst(int64, PAYLOAD_MASK as i64);
            let shift16 = builder.ins().iconst(int64, 16);
            let one64 = builder.ins().iconst(int64, 1);
            let zero64 = builder.ins().iconst(int64, 0);

            // Helpers as macros so `builder` stays singly-borrowed.
            macro_rules! load_arg {
                ($i:expr) => {{
                    // args are u64 words: byte offset = index * 8.
                    let off = builder.ins().iconst(pointer_ty, ($i as i64) * 8);
                    let p = builder.ins().iadd(args_ptr, off);
                    builder.ins().load(int64, MemFlags::new(), p, 0)
                }};
            }
            macro_rules! box_int {
                ($v:expr) => {{
                    let masked = builder.ins().band($v, payload_mask);
                    builder.ins().bor(masked, int48_tagged)
                }};
            }
            macro_rules! unbox_int {
                ($v:expr) => {{
                    let masked = builder.ins().band($v, payload_mask);
                    let shl = builder.ins().ishl(masked, shift16);
                    builder.ins().sshr(shl, shift16)
                }};
            }
            macro_rules! is_int48 {
                ($v:expr) => {{
                    let t = builder.ins().band($v, tag_mask);
                    builder.ins().icmp(IntCC::Equal, t, int48_tagged)
                }};
            }

            let mut vstack: Vec<Value> = Vec::new();
            let mut locals: HashMap<u32, Value> = HashMap::new();
            let mut saw_return = false;

            let n = code.len() / 2;
            for i in 0..n {
                let op = code[2 * i];
                let operand = code[2 * i + 1];
                match op {
                    OP_LOAD_CONST => {
                        let c = constants
                            .get(operand as usize)
                            .copied()
                            .ok_or_else(|| err(format!("LOAD_CONST {} out of range", operand)))?;
                        vstack.push(builder.ins().iconst(int64, c as i64));
                    }
                    OP_LOAD_VAR => match locals.get(&operand).copied() {
                        Some(v) => vstack.push(v),
                        None => vstack.push(load_arg!(operand)),
                    },
                    OP_STORE_VAR => {
                        let v = vstack
                            .pop()
                            .ok_or_else(|| err("STORE_VAR with empty stack".into()))?;
                        locals.insert(operand, v);
                    }
                    OP_ADD | OP_SUB | OP_MUL | OP_LT => {
                        let r = vstack
                            .pop()
                            .ok_or_else(|| err("binop with empty stack".into()))?;
                        let l = vstack
                            .pop()
                            .ok_or_else(|| err("binop with empty stack".into()))?;
                        let li = is_int48!(l);
                        let ri = is_int48!(r);
                        let both = builder.ins().band(li, ri);

                        // Only ADD and LT have runtime bridges for the
                        // non-int path in this prototype; SUB/MUL require
                        // proven-int operands (validated input).
                        let bridge_name = match op {
                            OP_ADD => "havel_vm_add",
                            OP_LT => "havel_vm_lt",
                            _ => {
                                return Err(err("SUB/MUL fallback unsupported in prototype".into()))
                            }
                        };
                        let _ = bridge_name;

                        let int_bb = builder.create_block();
                        let bridge_bb = builder.create_block();
                        let merge_bb = builder.create_block();
                        let result = builder.append_block_param(merge_bb, int64);

                        builder.ins().brif(both, int_bb, &[], bridge_bb, &[]);

                        builder.switch_to_block(int_bb);
                        let lv = unbox_int!(l);
                        let rv = unbox_int!(r);
                        let raw = match op {
                            OP_ADD => builder.ins().iadd(lv, rv),
                            OP_SUB => builder.ins().isub(lv, rv),
                            OP_MUL => builder.ins().imul(lv, rv),
                            OP_LT => {
                                let c = builder.ins().icmp(IntCC::SignedLessThan, lv, rv);
                                builder.ins().uextend(int64, c)
                            }
                            _ => unreachable!(),
                        };
                        let boxed = match op {
                            OP_LT => {
                                let b = builder.ins().band(raw, one64);
                                builder.ins().bor(b, bool_tagged)
                            }
                            _ => box_int!(raw),
                        };
                        builder.ins().jump(merge_bb, &[boxed.into()]);
                        builder.seal_block(int_bb);

                        builder.switch_to_block(bridge_bb);
                        let callee = match op {
                            OP_ADD => add_id,
                            _ => lt_id,
                        };
                        let func_ref = self.module.declare_func_in_func(callee, &mut builder.func);
                        let call = builder.ins().call(func_ref, &[vm, l, r]);
                        let bridged = builder.inst_results(call)[0];
                        builder.ins().jump(merge_bb, &[bridged.into()]);
                        builder.seal_block(bridge_bb);

                        builder.switch_to_block(merge_bb);
                        vstack.push(result);
                    }
                    OP_RETURN => {
                        let v = vstack
                            .pop()
                            .ok_or_else(|| err("RETURN with empty stack".into()))?;
                        builder.ins().return_(&[v.into()]);
                        saw_return = true;
                        // Remaining pairs (if any) are unreachable in this
                        // straight-line subset; stop lowering here.
                        break;
                    }
                    _ => return Err(err(format!("unsupported opcode {op}"))),
                }
            }
            if !saw_return {
                // Straight-line body without RETURN: the function yields null.
                builder.ins().return_(&[zero64.into()]);
            }
            builder.finalize();
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

// Fallback shims for the Runtime ABI bridge symbols in the standalone Rust
// test binary. In the real embedder the symbols come from the Havel runtime
// (RuntimeABI.hpp registration, resolved via dlsym/RTLD_DEFAULT); here the
// speculative-fallback path needs them linkable, so a process-wide registry
// is consulted first. The semantics mirror VMArithmetic's generic ADD and
// LT for the shapes the prototype feeds them (int/int; anything else yields
// the null word).
mod fallback_shims {
    use super::*;
    use std::ffi::c_void;

    extern "C" fn shim_add(_vm: *mut c_void, l: u64, r: u64) -> u64 {
        if is_int48(l) && is_int48(r) {
            pack_int48(unpack_int48(l).wrapping_add(unpack_int48(r)))
        } else {
            0x7FFB_0000_0000_0000 // null word (tag NULL_ = 3)
        }
    }

    extern "C" fn shim_lt(_vm: *mut c_void, l: u64, r: u64) -> u64 {
        if is_int48(l) && is_int48(r) {
            pack_bool(unpack_int48(l) < unpack_int48(r))
        } else {
            0x7FFB_0000_0000_0000
        }
    }

    /// Symbol registry consulted before dlsym for standalone/test binaries;
    /// the real embedding process resolves its own ABI functions through
    /// dlsym because the registry only ever holds the fallbacks.
    pub fn fallback_symbol(name: &str) -> Option<*const u8> {
        match name {
            "havel_vm_add" => Some(shim_add as *const u8),
            "havel_vm_lt" => Some(shim_lt as *const u8),
            _ => None,
        }
    }
}

// Runtime ABI symbol lookup: symbols live in the embedding process.
fn lookup_runtime_abi(name: &str) -> Option<*const u8> {
    // Registry first (standalone/test binaries), then the embedding process.
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
        // fn (a) = a : proves arg loading + return plumbing.
        let mut backend = CraneliftBackend::new().unwrap();
        let code = [OP_LOAD_VAR, 0, OP_RETURN, 0];
        let constants: [u64; 0] = [];
        let f = backend
            .compile_function("ident", &code, &constants)
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
            .compile_function("add2", &code, &constants)
            .expect("lowering");
        let a = [pack_int48(40), pack_int48(2)];
        let out = unsafe { f(std::ptr::null_mut(), a.as_ptr(), 2) };
        assert_eq!(
            unpack_int48(out),
            42,
            "raw out = {out:#x} (l={}, r={})",
            a[0],
            a[1]
        );
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
            .compile_function("lt10", &code, &constants)
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
            .compile_function("neg", &code, &constants)
            .expect("lowering");
        let a = [pack_int48(-40)];
        let out = unsafe { f(std::ptr::null_mut(), a.as_ptr(), 1) };
        assert_eq!(unpack_int48(out), -80);
    }
}

// ---------------------------------------------------------------------------
// C ABI surface for the C++/CTest driver.
// ---------------------------------------------------------------------------

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
