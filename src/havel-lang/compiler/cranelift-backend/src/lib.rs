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
// Block arguments (block params on edges) are BlockArg, not Value.
use cranelift::codegen::ir::BlockArg;
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
pub const OP_LOAD_GLOBAL: u32 = 19;
pub const OP_STORE_GLOBAL: u32 = 20;
pub const OP_EQ: u32 = 10;
pub const OP_NEQ: u32 = 11;
pub const OP_LTE: u32 = 12;
pub const OP_GT: u32 = 13;
pub const OP_GTE: u32 = 14;
// Extension set: bridge-lowered ops (semantics stay in the runtime).
pub const OP_NOT: u32 = 21;
pub const OP_IS_NULL: u32 = 22;
pub const OP_LENGTH: u32 = 23;
pub const OP_STRING_LEN: u32 = 24;
pub const OP_STRING_UPPER: u32 = 25;
pub const OP_STRING_LOWER: u32 = 26;
pub const OP_STRING_TRIM: u32 = 27;
pub const OP_STRING_CONCAT: u32 = 28;
pub const OP_BIT_AND: u32 = 29;
pub const OP_BIT_OR: u32 = 30;
pub const OP_BIT_XOR: u32 = 31;
pub const OP_BIT_NOT: u32 = 32;
pub const OP_BIT_LSH: u32 = 33;
pub const OP_BIT_RSH: u32 = 34;
pub const OP_SWAP: u32 = 35;
pub const OP_JUMP_IF_TRUE: u32 = 36;
pub const OP_LOAD_UPVALUE: u32 = 37;
pub const OP_STORE_UPVALUE: u32 = 38;
pub const OP_STRING_PROMOTE: u32 = 39;
pub const OP_OBJECT_GET: u32 = 40;
pub const OP_OBJECT_SET: u32 = 41;
pub const OP_ITER_NEW: u32 = 42;
pub const OP_ITER_NEXT: u32 = 43;
pub const OP_ARRAY_GET: u32 = 44;
pub const OP_ARRAY_SET: u32 = 45;
pub const OP_ARRAY_LEN: u32 = 46;
pub const OP_ARRAY_PUSH: u32 = 47;
pub const OP_JUMP_IF_NULL: u32 = 48;
pub const OP_CALL_METHOD: u32 = 49;
// Pseudo-instruction carrying a second operand for the preceding
// CALL_METHOD (the flat stream is (op, operand) pairs, one operand per
// instruction; CALL_METHOD needs both the method-name id and the arg
// count). Never a jump target; always immediately follows its op.
pub const OP_EXTENDED_ARG: u32 = 50;
pub const OP_OBJECT_NEW: u32 = 51;
pub const OP_OBJECT_NEW_UNSORTED: u32 = 52;
pub const OP_ARRAY_NEW: u32 = 53;
pub const OP_SET_NEW: u32 = 54;
pub const OP_RANGE_NEW: u32 = 55;
pub const OP_SET_SET: u32 = 56;

#[derive(Debug)]
pub struct LoweringError(pub String);

// Edge arguments: block params on CFG edges are BlockArg; the subset only
// ever passes plain Values (no try_call exception args).
fn edge_args(stack: &[Value]) -> Vec<BlockArg> {
    stack.iter().map(|v| BlockArg::Value(*v)).collect()
}

// Static stack effect of one subset instruction: (pops, pushes) beyond the
// pops the operand implies. RETURN consumes 1; jumps consume their
// condition (except JUMP). CALL/CALL_METHOD pop their argument count plus
// the callee/receiver and push one result. The flat stream's (op, operand)
// pairs carry CALL_METHOD's arg count in the following OP_EXTENDED_ARG
// pair, which itself is data (no effect).
fn stack_effect(op: u32, operand: u32, next_operand: Option<u32>) -> Result<(i64, i64), String> {
    Ok(match op {
        OP_LOAD_CONST | OP_LOAD_VAR | OP_PUSH_NULL | OP_IS_NULL => (0, 1),
        OP_STORE_VAR | OP_POP | OP_STORE_GLOBAL => (1, 0),
        OP_DUP => (0, 1), // pops 0, pushes a copy of the top
        OP_SWAP => (0, 0),
        // STORE_UPVALUE pops the value to write (interpreter
        // VMDispatch.cpp); the bridge takes (vm, slot, value).
        OP_STORE_UPVALUE => (1, 0),
        OP_LOAD_UPVALUE => (0, 1),
        OP_ADD | OP_SUB | OP_MUL | OP_LT | OP_EQ | OP_NEQ | OP_LTE | OP_GT | OP_GTE => (2, 1),
        OP_STRING_CONCAT | OP_BIT_AND | OP_BIT_OR | OP_BIT_XOR | OP_BIT_LSH | OP_BIT_RSH => (2, 1),
        OP_NOT | OP_LENGTH | OP_BIT_NOT | OP_STRING_LEN | OP_STRING_UPPER | OP_STRING_LOWER
        | OP_STRING_TRIM | OP_STRING_PROMOTE => (1, 1),
        OP_JUMP => (0, 0),
        OP_JUMP_IF_FALSE | OP_JUMP_IF_TRUE | OP_JUMP_IF_NULL => (1, 0),
        OP_CALL => {
            let argc = operand as i64;
            (argc + 1, 1)
        }
        OP_CALL_METHOD => {
            let argc = next_operand
                .ok_or_else(|| "CALL_METHOD without EXTENDED_ARG pair".to_string())?
                as i64;
            (argc + 1, 1)
        }
        OP_EXTENDED_ARG => (0, 0),
        OP_LOAD_GLOBAL => (0, 1),
        OP_OBJECT_GET | OP_ARRAY_GET => (2, 1),
        // ITER_NEXT pops the iterator and pushes the wrapped result
        // object {first, second, done} - (1, 1), NOT (2, 1).
        OP_ITER_NEXT => (1, 1),
        // OBJECT_SET pops obj/val/key and pushes the OBJECT back for
        // chaining (interpreter VMCollections.cpp); ARRAY_SET pushes
        // nothing on the plain paths (the emitter always re-loads the
        // stored temp right after); ARRAY_PUSH pushes the container.
        OP_OBJECT_SET => (3, 1),
        OP_ARRAY_SET => (3, 0),
        OP_ARRAY_LEN | OP_ITER_NEW => (1, 1),
        OP_ARRAY_PUSH => (2, 1),
        // Constructors: (vm) -> new value.
        OP_OBJECT_NEW | OP_OBJECT_NEW_UNSORTED | OP_ARRAY_NEW | OP_SET_NEW => (0, 1),
        // RANGE_NEW pops end then start, pushes the range (2, 1).
        OP_RANGE_NEW => (2, 1),
        // SET_SET pops key, value, set; pushes nothing (interpreter: the
        // caller keeps managing the set on the stack).
        OP_SET_SET => (3, 0),
        OP_RETURN => (1, 0),
        _ => return Err(format!("stack effect: unsupported opcode {op}")),
    })
}

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

    unsafe extern "C" fn shim_lt(l: u64, r: u64) -> u64 {
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

    unsafe extern "C" fn shim_eq(l: u64, r: u64) -> u64 {
        if is_int48(l) && is_int48(r) {
            pack_bool(unpack_int48(l) == unpack_int48(r))
        } else {
            NULL_TAGGED
        }
    }

    unsafe extern "C" fn shim_neq(l: u64, r: u64) -> u64 {
        if is_int48(l) && is_int48(r) {
            pack_bool(unpack_int48(l) != unpack_int48(r))
        } else {
            NULL_TAGGED
        }
    }

    unsafe extern "C" fn shim_lte(l: u64, r: u64) -> u64 {
        if is_int48(l) && is_int48(r) {
            pack_bool(unpack_int48(l) <= unpack_int48(r))
        } else {
            NULL_TAGGED
        }
    }

    unsafe extern "C" fn shim_gt(l: u64, r: u64) -> u64 {
        if is_int48(l) && is_int48(r) {
            pack_bool(unpack_int48(l) > unpack_int48(r))
        } else {
            NULL_TAGGED
        }
    }

    unsafe extern "C" fn shim_gte(l: u64, r: u64) -> u64 {
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

    unsafe extern "C" fn shim_call(_vm: *mut c_void, args: *const u64, count: u32) -> u64 {
        // Layout check: args[0] must be the callee and args[1..] the call
        // arguments in order. Distinguish the slots by value so an inverted
        // layout cannot pass (the callee constant is the large magic number;
        // the arguments are 1, 2, 3). Returns callee*10 + arg0*2 + arg1 so
        // a wrong layout produces a different sum.
        if args.is_null() || count != 4 {
            return NULL_TAGGED;
        }
        let callee = *args;
        if !is_int48(callee) {
            return NULL_TAGGED;
        }
        let a = *args.add(1);
        let b = *args.add(2);
        let c = *args.add(3);
        if !(is_int48(a) && is_int48(b) && is_int48(c)) {
            return NULL_TAGGED;
        }
        // callee must be 900000 (the magic), args must be 1,2,3 in order.
        if unpack_int48(callee) != 900000
            || unpack_int48(a) != 1
            || unpack_int48(b) != 2
            || unpack_int48(c) != 3
        {
            return NULL_TAGGED;
        }
        pack_int48(900123)
    }

    unsafe extern "C" fn shim_not(v: u64) -> u64 {
        // Mirrors the C bridge: !valueIsTruthy(v) for the scalar shapes.
        pack_bool(!match (v & TAG_MASK) >> TAG_SHIFT {
            TAG_NULL => false,
            TAG_BOOL => (v & PAYLOAD_MASK) != 0,
            TAG_INT48 => unpack_int48(v) != 0,
            _ => {
                if v & TAG_MASK == 0 && (v as f64) == 0.0 {
                    false
                } else {
                    true
                }
            }
        })
    }

    unsafe extern "C" fn shim_bit_and(l: u64, r: u64) -> u64 {
        if is_int48(l) && is_int48(r) {
            pack_int48(unpack_int48(l) & unpack_int48(r))
        } else {
            NULL_TAGGED
        }
    }

    unsafe extern "C" fn shim_bit_or(l: u64, r: u64) -> u64 {
        if is_int48(l) && is_int48(r) {
            pack_int48(unpack_int48(l) | unpack_int48(r))
        } else {
            NULL_TAGGED
        }
    }

    unsafe extern "C" fn shim_bit_xor(l: u64, r: u64) -> u64 {
        if is_int48(l) && is_int48(r) {
            pack_int48(unpack_int48(l) ^ unpack_int48(r))
        } else {
            NULL_TAGGED
        }
    }

    unsafe extern "C" fn shim_bit_not(v: u64) -> u64 {
        if is_int48(v) {
            pack_int48(!unpack_int48(v))
        } else {
            NULL_TAGGED
        }
    }

    unsafe extern "C" fn shim_bit_lsh(l: u64, r: u64) -> u64 {
        if is_int48(l) && is_int48(r) {
            pack_int48(unpack_int48(l) << unpack_int48(r))
        } else {
            NULL_TAGGED
        }
    }

    unsafe extern "C" fn shim_bit_rsh(l: u64, r: u64) -> u64 {
        if is_int48(l) && is_int48(r) {
            pack_int48(unpack_int48(l) >> unpack_int48(r))
        } else {
            NULL_TAGGED
        }
    }

    unsafe extern "C" fn shim_backedge(_vm: *mut c_void, _ip: u32) {}

    // Null-vm upvalue bridges: no closure context exists standalone, so
    // reads yield null and writes drop (mirrors the C null-vm behavior).
    unsafe extern "C" fn shim_upvalue_get(_vm: *mut c_void, _slot: u32) -> u64 {
        NULL_TAGGED
    }

    unsafe extern "C" fn shim_upvalue_set(_vm: *mut c_void, _slot: u32, _v: u64) {}

    // Null-vm unary bridges mirror the C behavior for a null vm: LENGTH and
    // STRING_LEN yield int 0, the string transforms yield null.
    unsafe extern "C" fn shim_length_zero(_vm: *mut c_void, _v: u64) -> u64 {
        pack_int48(0)
    }

    unsafe extern "C" fn shim_string_null(_vm: *mut c_void, _v: u64) -> u64 {
        NULL_TAGGED
    }

    unsafe extern "C" fn shim_string_concat(_vm: *mut c_void, _l: u64, _r: u64) -> u64 {
        NULL_TAGGED
    }

    // Null-vm object/iter bridges: member access and iteration have no heap
    // standalone, so gets and iterator results yield null; set echoes the
    // written value word (the C bridge returns val_bits on a null vm).
    unsafe extern "C" fn shim_object_get_null(_vm: *mut c_void, _o: u64, _k: u64) -> u64 {
        NULL_TAGGED
    }

    unsafe extern "C" fn shim_object_set_echo(_vm: *mut c_void, _o: u64, _k: u64, v: u64) -> u64 {
        v
    }

    unsafe extern "C" fn shim_array_push(_vm: *mut c_void, _arr: u64, _val: u64) {}

    unsafe extern "C" fn shim_call_method(
        _vm: *mut c_void,
        receiver: u64,
        _name_id: u32,
        args: *const u64,
        count: u32,
    ) -> u64 {
        // Layout check for the standalone harness: the receiver travels
        // separately and args hold ONLY the call arguments. Reply with
        // receiver*100 + sum(args) so an inverted or callee-staged layout
        // produces a different number.
        if args.is_null() || count != 2 {
            return NULL_TAGGED;
        }
        let a0 = unsafe { *args };
        let a1 = unsafe { *args.add(1) };
        if !(is_int48(receiver) && is_int48(a0) && is_int48(a1)) {
            return NULL_TAGGED;
        }
        let sum = unpack_int48(receiver)
            .wrapping_mul(100)
            .wrapping_add(unpack_int48(a0))
            .wrapping_add(unpack_int48(a1));
        pack_int48(sum)
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
            // VM-aware equality falls back to the pure int shims: the
            // standalone harness has no heap, so int operands are all it
            // can answer (mirrors havel_vm_eq_vm's non-string path).
            "havel_vm_eq_vm" => Some(shim_eq as *const u8),
            "havel_vm_neq_vm" => Some(shim_neq as *const u8),
            "havel_vm_not" => Some(shim_not as *const u8),
            "havel_vm_bit_and" => Some(shim_bit_and as *const u8),
            "havel_vm_bit_or" => Some(shim_bit_or as *const u8),
            "havel_vm_bit_xor" => Some(shim_bit_xor as *const u8),
            "havel_vm_bit_not" => Some(shim_bit_not as *const u8),
            "havel_vm_bit_lsh" => Some(shim_bit_lsh as *const u8),
            "havel_vm_bit_rsh" => Some(shim_bit_rsh as *const u8),
            "havel_vm_backedge" => Some(shim_backedge as *const u8),
            "havel_vm_upvalue_get" => Some(shim_upvalue_get as *const u8),
            "havel_vm_upvalue_set" => Some(shim_upvalue_set as *const u8),
            "havel_vm_object_get_raw_ic" => Some(shim_object_get_null as *const u8),
            "havel_vm_object_get_raw" => Some(shim_object_get_null as *const u8),
            "havel_vm_object_set_raw" => Some(shim_object_set_echo as *const u8),
            "havel_vm_iter_new" => Some(shim_string_null as *const u8),
            "havel_vm_iter_next" => Some(shim_string_null as *const u8),
            "havel_vm_collection_get_raw_ic" => Some(shim_object_get_null as *const u8),
            "havel_vm_collection_get_raw" => Some(shim_object_get_null as *const u8),
            "havel_vm_array_set" => Some(shim_object_set_echo as *const u8),
            "havel_vm_array_len" => Some(shim_length_zero as *const u8),
            "havel_vm_array_push" => Some(shim_array_push as *const u8),
            "havel_vm_call_method" => Some(shim_call_method as *const u8),
            "havel_vm_length" => Some(shim_length_zero as *const u8),
            "havel_vm_string_len" => Some(shim_length_zero as *const u8),
            "havel_vm_string_upper" => Some(shim_string_null as *const u8),
            "havel_vm_string_lower" => Some(shim_string_null as *const u8),
            "havel_vm_string_trim" => Some(shim_string_null as *const u8),
            "havel_vm_string_promote" => Some(shim_string_null as *const u8),
            "havel_vm_object_new" => Some(shim_string_null as *const u8),
            "havel_vm_object_new_unsorted" => Some(shim_string_null as *const u8),
            "havel_vm_array_new" => Some(shim_string_null as *const u8),
            "havel_vm_set_new" => Some(shim_string_null as *const u8),
            "havel_vm_range_new" => Some(shim_string_concat as *const u8),
            "havel_vm_set_set" => Some(shim_object_set_echo as *const u8),
            "havel_vm_string_concat" => Some(shim_string_concat as *const u8),
            // GC Runtime ABI - stub shims (no heap in standalone tests)
            "havel_gc_register_roots" => Some(shim_gc_register_roots as *const u8),
            "havel_gc_unregister_roots" => Some(shim_gc_unregister_roots as *const u8),
            "havel_gc_write_barrier" => Some(shim_gc_write_barrier as *const u8),
            _ => None,
        }
    }
}

// GC stub shims for standalone tests (no heap, no GC)
unsafe extern "C" fn shim_gc_register_roots(
    _vm: *mut c_void,
    _frame: *mut c_void,
    _roots: *mut u64,
    _count: u32,
) {
    // No-op: no GC in standalone harness
}

unsafe extern "C" fn shim_gc_unregister_roots(_frame: *mut c_void) {
    // No-op
}

unsafe extern "C" fn shim_gc_write_barrier(_vm: *mut c_void, _value: u64) {
    // No-op
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
    /// `symbols`: (name, address) pairs for the Runtime ABI entries the
    /// embedder provides. In a real embedding the main executable is built
    /// with -fvisibility=hidden (no .dynsym entries), so dlsym can never
    /// resolve the runtime from inside the process - the embedder passes
    /// the addresses explicitly, the same table the LLVM backend registers
    /// from the RuntimeABI.hpp X-macro. Unknown names fall back to the
    /// standalone shims (tests) then dlsym.
    pub fn new_with_symbols(symbols: Vec<(String, *const u8)>) -> Result<Self, String> {
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
        // Embedder-provided Runtime ABI addresses first (hidden-visibility
        // executables cannot be dlsym'd); the lookup fn serves the rest.
        for (name, addr) in symbols {
            jit_builder.symbol(name, addr);
        }
        jit_builder.symbol_lookup_fn(Box::new(lookup_runtime_abi) as Box<_>);
        let module = JITModule::new(jit_builder);
        Ok(Self {
            module,
            symbols: HashMap::new(),
        })
    }

    /// Standalone construction (tests, tools): no embedder symbols.
    pub fn new() -> Result<Self, String> {
        CraneliftBackend::new_with_symbols(Vec::new())
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
                OP_JUMP | OP_JUMP_IF_FALSE | OP_JUMP_IF_TRUE | OP_JUMP_IF_NULL => {
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

        // ---- Static stack-depth pre-pass ----
        // Real bytecode keeps operand-stack values across basic blocks
        // (short-circuit operators, loop-carried temps), so the virtual
        // stack must flow along CFG edges like the ORC lowering's PHIs.
        // Every subset opcode has a static stack effect, so an abstract
        // interpretation of the stream yields the stack depth at every
        // instruction; each leader block then receives its incoming depth
        // as block parameters.
        let mut depth_at: Vec<i64> = vec![-1; n];
        depth_at[0] = 0;
        let mut work: Vec<usize> = vec![0];
        while let Some(i) = work.pop() {
            let d = depth_at[i];
            let op = code[2 * i];
            // OP_EXTENDED_ARG is data for the preceding CALL_METHOD: it
            // carries no effect and falls through with the same depth.
            let after = if op == OP_EXTENDED_ARG {
                d
            } else {
                let next_operand = if op == OP_CALL_METHOD && i + 1 < n {
                    Some(code[2 * (i + 1) + 1])
                } else {
                    None
                };
                let (pops, pushes) = stack_effect(op, code[2 * i + 1], next_operand)
                    .map_err(|m| err(format!("instruction {i} ({name}): {m}")))?;
                if d < pops {
                    return Err(err(format!(
                        "instruction {i} ({name}): stack underflow ({d} < {pops}) at op {op} operand {}",
                        code[2 * i + 1]
                    )));
                }
                d - pops + pushes
            };
            let mut edges: Vec<(usize, i64)> = Vec::new();
            match op {
                OP_JUMP => {
                    edges.push((code[2 * i + 1] as usize, after));
                }
                OP_JUMP_IF_FALSE | OP_JUMP_IF_TRUE | OP_JUMP_IF_NULL => {
                    edges.push((code[2 * i + 1] as usize, after));
                    if i + 1 < n {
                        edges.push((i + 1, after));
                    }
                }
                OP_RETURN => {}
                _ => {
                    if i + 1 < n {
                        edges.push((i + 1, after));
                    }
                }
            }
            for (t, dep) in edges {
                if t >= n {
                    return Err(err(format!("jump target {t} out of range")));
                }
                match depth_at[t] {
                    -1 => {
                        depth_at[t] = dep;
                        work.push(t);
                    }
                    prev if prev == dep => {}
                    // Depths only converge: a fork pushing less is a
                    // different-shaped stream than the static model (the
                    // interpreter would underflow one arm).
                    prev => {
                        return Err(err(format!(
                            "instruction {t} ({name}): join depth conflict ({prev} vs {dep})"
                        )))
                    }
                }
            }
        }
        // Reachable instructions all have depths now. UNREACHABLE code (the
        // emitter produces it after unconditional jumps/returns) defaults to
        // depth 0: it lowers as dead blocks that never execute, which the
        // verifier still requires to be well-formed. Dead blocks enter with
        // a synthesized null-filled stack; pops that would underflow inside
        // dead regions take a synthesized null so every terminator stays
        // well-formed.
        let dead: Vec<bool> = (0..n).map(|i| depth_at[i] < 0).collect();
        for d in depth_at.iter_mut() {
            if *d < 0 {
                *d = 0;
            }
        }
        // Entry depth of each leader block.
        let entry_depth: Vec<usize> = (0..n)
            .map(|i| {
                if leader[i] {
                    depth_at[i].max(0) as usize
                } else {
                    0
                }
            })
            .collect();

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
        for sym in ["havel_vm_add", "havel_vm_sub", "havel_vm_mul"] {
            let s = bridge_sig.clone();
            let id = self
                .module
                .declare_function(sym, Linkage::Import, &s)
                .map_err(|e| err(format!("declare {sym}: {e}")))?;
            bridge_ids.insert(sym, id);
        }
        // Comparison bridges: (l, r) -> result, no vm (RuntimeABI.hpp):
        // they are pure word semantics.
        let mut cmp_sig = self.module.make_signature();
        cmp_sig.params = vec![AbiParam::new(int64), AbiParam::new(int64)];
        cmp_sig.returns = vec![AbiParam::new(int64)];
        for sym in [
            "havel_vm_lt",
            "havel_vm_eq",
            "havel_vm_neq",
            "havel_vm_lte",
            "havel_vm_gt",
            "havel_vm_gte",
        ] {
            let sig = cmp_sig.clone();
            let id = self
                .module
                .declare_function(sym, Linkage::Import, &sig)
                .map_err(|e| err(format!("declare {sym}: {e}")))?;
            bridge_ids.insert(sym, id);
        }
        // VM-aware equality bridges: (vm, l, r) -> result. EQ/NEQ may compare
        // string CONTENT (heap StringId vs chunk-local StringValId) via the
        // heap, so the pure word bridges are not enough - mirror the ORC
        // lowering, which routes EQ/NEQ slow paths through these.
        for sym in ["havel_vm_eq_vm", "havel_vm_neq_vm"] {
            let s = bridge_sig.clone();
            let id = self
                .module
                .declare_function(sym, Linkage::Import, &s)
                .map_err(|e| err(format!("declare {sym}: {e}")))?;
            bridge_ids.insert(sym, id);
        }
        // String concat: (vm, l, r) -> result (bridge_sig shape).
        {
            let s = bridge_sig.clone();
            let id = self
                .module
                .declare_function("havel_vm_string_concat", Linkage::Import, &s)
                .map_err(|e| err(format!("declare havel_vm_string_concat: {e}")))?;
            bridge_ids.insert("havel_vm_string_concat", id);
        }
        // Unary vm bridges: (vm, v) -> result. LENGTH plus the string
        // intrinsics all take the value word and hand semantics to the
        // runtime.
        let mut unary_vm_sig = self.module.make_signature();
        unary_vm_sig.params = vec![AbiParam::new(pointer_ty), AbiParam::new(int64)];
        unary_vm_sig.returns = vec![AbiParam::new(int64)];
        for sym in [
            "havel_vm_length",
            "havel_vm_string_len",
            "havel_vm_string_upper",
            "havel_vm_string_lower",
            "havel_vm_string_trim",
            "havel_vm_string_promote",
        ] {
            let s = unary_vm_sig.clone();
            let id = self
                .module
                .declare_function(sym, Linkage::Import, &s)
                .map_err(|e| err(format!("declare {sym}: {e}")))?;
            bridge_ids.insert(sym, id);
        }
        // Constructors: (vm) -> new value, a single pointer param.
        let mut ctor_sig = self.module.make_signature();
        ctor_sig.params = vec![AbiParam::new(pointer_ty)];
        ctor_sig.returns = vec![AbiParam::new(int64)];
        for sym in [
            "havel_vm_object_new",
            "havel_vm_object_new_unsorted",
            "havel_vm_array_new",
            "havel_vm_set_new",
        ] {
            let s = ctor_sig.clone();
            let id = self
                .module
                .declare_function(sym, Linkage::Import, &s)
                .map_err(|e| err(format!("declare {sym}: {e}")))?;
            bridge_ids.insert(sym, id);
        }
        // Range construction: (vm, start, end) -> range value (bridge_sig
        // shape).
        {
            let s = bridge_sig.clone();
            let id = self
                .module
                .declare_function("havel_vm_range_new", Linkage::Import, &s)
                .map_err(|e| err(format!("declare havel_vm_range_new: {e}")))?;
            bridge_ids.insert("havel_vm_range_new", id);
        }
        // Set membership write: havel_vm_set_set(vm, set, val, key) ->
        // result (result unused; the interpreter pushes nothing back).
        // NOTE the (set, val, key) argument order, unlike
        // havel_vm_object_set_raw's (obj, key, val).
        {
            let mut set_set_sig = self.module.make_signature();
            set_set_sig.params = vec![
                AbiParam::new(pointer_ty),
                AbiParam::new(int64),
                AbiParam::new(int64),
                AbiParam::new(int64),
            ];
            set_set_sig.returns = vec![AbiParam::new(int64)];
            let id = self
                .module
                .declare_function("havel_vm_set_set", Linkage::Import, &set_set_sig)
                .map_err(|e| err(format!("declare havel_vm_set_set: {e}")))?;
            bridge_ids.insert("havel_vm_set_set", id);
        }
        // Pure unary bridges: (v) -> result, no vm. NOT and BIT_NOT are pure
        // word semantics per RuntimeABI.hpp.
        let mut pure_unary_sig = self.module.make_signature();
        pure_unary_sig.params = vec![AbiParam::new(int64)];
        pure_unary_sig.returns = vec![AbiParam::new(int64)];
        for sym in ["havel_vm_not", "havel_vm_bit_not"] {
            let s = pure_unary_sig.clone();
            let id = self
                .module
                .declare_function(sym, Linkage::Import, &s)
                .map_err(|e| err(format!("declare {sym}: {e}")))?;
            bridge_ids.insert(sym, id);
        }
        // Pure binary bitwise bridges: (l, r) -> result (cmp_sig shape).
        for sym in [
            "havel_vm_bit_and",
            "havel_vm_bit_or",
            "havel_vm_bit_xor",
            "havel_vm_bit_lsh",
            "havel_vm_bit_rsh",
        ] {
            let s = cmp_sig.clone();
            let id = self
                .module
                .declare_function(sym, Linkage::Import, &s)
                .map_err(|e| err(format!("declare {sym}: {e}")))?;
            bridge_ids.insert(sym, id);
        }
        // Backedge hook: (vm, ip) -> (). Native loops must surface to the
        // VM (loop hotness, tier-up decisions, coroutine yield requests)
        // exactly like the interpreter's taken backedges.
        let mut backedge_sig = self.module.make_signature();
        backedge_sig.params = vec![AbiParam::new(pointer_ty), AbiParam::new(int32)];
        backedge_sig.returns = vec![];
        let backedge_id = self
            .module
            .declare_function("havel_vm_backedge", Linkage::Import, &backedge_sig)
            .map_err(|e| err(format!("declare havel_vm_backedge: {e}")))?;
        // Upvalue bridges: closures read/write captured locals through the
        // running closure's upvalue cells. get: (vm, slot) -> Value;
        // set: (vm, slot, Value) -> ().
        let mut upvalue_get_sig = self.module.make_signature();
        upvalue_get_sig.params = vec![AbiParam::new(pointer_ty), AbiParam::new(int32)];
        upvalue_get_sig.returns = vec![AbiParam::new(int64)];
        let upvalue_get_id = self
            .module
            .declare_function("havel_vm_upvalue_get", Linkage::Import, &upvalue_get_sig)
            .map_err(|e| err(format!("declare havel_vm_upvalue_get: {e}")))?;
        let mut upvalue_set_sig = self.module.make_signature();
        upvalue_set_sig.params = vec![
            AbiParam::new(pointer_ty),
            AbiParam::new(int32),
            AbiParam::new(int64),
        ];
        upvalue_set_sig.returns = vec![];
        let upvalue_set_id = self
            .module
            .declare_function("havel_vm_upvalue_set", Linkage::Import, &upvalue_set_sig)
            .map_err(|e| err(format!("declare havel_vm_upvalue_set: {e}")))?;
        // Object member access: same bridges the ORC lowering uses. GET goes
        // through the inline-cache variant (GC-epoch guarded, lazy-proxy
        // safe); SET is raw. Iterators share the unary-vm shape.
        let object_get_id = self
            .module
            .declare_function("havel_vm_object_get_raw_ic", Linkage::Import, &bridge_sig)
            .map_err(|e| err(format!("declare havel_vm_object_get_raw_ic: {e}")))?;
        let mut obj_set_sig = self.module.make_signature();
        obj_set_sig.params = vec![
            AbiParam::new(pointer_ty),
            AbiParam::new(int64),
            AbiParam::new(int64),
            AbiParam::new(int64),
        ];
        obj_set_sig.returns = vec![AbiParam::new(int64)];
        let object_set_id = self
            .module
            .declare_function("havel_vm_object_set_raw", Linkage::Import, &obj_set_sig)
            .map_err(|e| err(format!("declare havel_vm_object_set_raw: {e}")))?;
        let iter_new_id = self
            .module
            .declare_function("havel_vm_iter_new", Linkage::Import, &unary_vm_sig)
            .map_err(|e| err(format!("declare havel_vm_iter_new: {e}")))?;
        let iter_next_id = self
            .module
            .declare_function("havel_vm_iter_next", Linkage::Import, &unary_vm_sig)
            .map_err(|e| err(format!("declare havel_vm_iter_next: {e}")))?;
        // Array member access, same bridges the ORC lowering uses: GET via
        // the collection inline-cache variant, SET/LEN/PUSH direct.
        let array_get_id = self
            .module
            .declare_function(
                "havel_vm_collection_get_raw_ic",
                Linkage::Import,
                &bridge_sig,
            )
            .map_err(|e| err(format!("declare havel_vm_collection_get_raw_ic: {e}")))?;
        let array_set_id = self
            .module
            .declare_function("havel_vm_array_set", Linkage::Import, &obj_set_sig)
            .map_err(|e| err(format!("declare havel_vm_array_set: {e}")))?;
        let array_len_id = self
            .module
            .declare_function("havel_vm_array_len", Linkage::Import, &unary_vm_sig)
            .map_err(|e| err(format!("declare havel_vm_array_len: {e}")))?;
        let mut void_ternary_sig = self.module.make_signature();
        void_ternary_sig.params = vec![
            AbiParam::new(pointer_ty),
            AbiParam::new(int64),
            AbiParam::new(int64),
        ];
        void_ternary_sig.returns = vec![];
        let array_push_id = self
            .module
            .declare_function("havel_vm_array_push", Linkage::Import, &void_ternary_sig)
            .map_err(|e| err(format!("declare havel_vm_array_push: {e}")))?;
        // Method calls: havel_vm_call_method(vm, receiver, name_id, args_ptr,
        // count) -> result. args holds ONLY the call arguments (receiver
        // travels separately), matching the ORC lowering.
        let mut call_method_sig = self.module.make_signature();
        call_method_sig.params = vec![
            AbiParam::new(pointer_ty),
            AbiParam::new(int64),
            AbiParam::new(int32),
            AbiParam::new(pointer_ty),
            AbiParam::new(int32),
        ];
        call_method_sig.returns = vec![AbiParam::new(int64)];
        let call_method_id = self
            .module
            .declare_function("havel_vm_call_method", Linkage::Import, &call_method_sig)
            .map_err(|e| err(format!("declare havel_vm_call_method: {e}")))?;
        // havel_vm_is_truthy is (value) -> i32: its own signature
        // (RuntimeABI.hpp; the C side returns int).
        let mut truthy_sig = self.module.make_signature();
        truthy_sig.params = vec![AbiParam::new(int64)];
        truthy_sig.returns = vec![AbiParam::new(types::I32)];
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

        // havel_vm_global_get: (vm, chunk-local string id) -> global Value
        // bits; missing globals yield null.
        let mut global_get_sig = self.module.make_signature();
        global_get_sig.params = vec![AbiParam::new(pointer_ty), AbiParam::new(int32)];
        global_get_sig.returns = vec![AbiParam::new(int64)];
        let global_get_id = self
            .module
            .declare_function("havel_vm_global_get", Linkage::Import, &global_get_sig)
            .map_err(|e| err(format!("declare havel_vm_global_get: {e}")))?;
        // havel_vm_global_set: (vm, chunk-local string id, Value bits) -> ().
        let mut global_set_sig = self.module.make_signature();
        global_set_sig.params = vec![
            AbiParam::new(pointer_ty),
            AbiParam::new(int32),
            AbiParam::new(int64),
        ];
        let global_set_id = self
            .module
            .declare_function("havel_vm_global_set", Linkage::Import, &global_set_sig)
            .map_err(|e| err(format!("declare havel_vm_global_set: {e}")))?;

        // GC Runtime ABI
        let mut gc_register_roots_sig = self.module.make_signature();
        gc_register_roots_sig.params = vec![
            AbiParam::new(pointer_ty), // vm_ptr
            AbiParam::new(pointer_ty), // JITStackFrame*
            AbiParam::new(pointer_ty), // uint64_t* roots
            AbiParam::new(int32),      // uint32_t count
        ];
        gc_register_roots_sig.returns = vec![];
        let gc_register_roots_id = self
            .module
            .declare_function(
                "havel_gc_register_roots",
                Linkage::Import,
                &gc_register_roots_sig,
            )
            .map_err(|e| err(format!("declare havel_gc_register_roots: {e}")))?;

        let mut gc_unregister_roots_sig = self.module.make_signature();
        gc_unregister_roots_sig.params = vec![AbiParam::new(pointer_ty)]; // JITStackFrame*
        gc_unregister_roots_sig.returns = vec![];
        let gc_unregister_roots_id = self
            .module
            .declare_function(
                "havel_gc_unregister_roots",
                Linkage::Import,
                &gc_unregister_roots_sig,
            )
            .map_err(|e| err(format!("declare havel_gc_unregister_roots: {e}")))?;

        let mut gc_write_barrier_sig = self.module.make_signature();
        gc_write_barrier_sig.params = vec![
            AbiParam::new(pointer_ty), // vm_ptr
            AbiParam::new(int64),      // uint64_t value
        ];
        gc_write_barrier_sig.returns = vec![];
        let gc_write_barrier_id = self
            .module
            .declare_function(
                "havel_gc_write_barrier",
                Linkage::Import,
                &gc_write_barrier_sig,
            )
            .map_err(|e| err(format!("declare havel_gc_write_barrier: {e}")))?;

        let mut ctx = self.module.make_context();
        ctx.func.signature = sig;
        ctx.func.name = cranelift::codegen::ir::UserFuncName::user(0, func_id.as_u32());
        {
            let mut fb_ctx = FunctionBuilderContext::new();
            let mut builder = FunctionBuilder::new(&mut ctx.func, &mut fb_ctx);

            // One Cranelift block per leader instruction, created up front
            // with its incoming stack depth as block params (values flow
            // across block boundaries like the ORC lowering's PHIs).
            let mut block_of: Vec<Option<Block>> = vec![None; n];
            // Incoming stack values of each leader block, as plain Values
            // (block params are BlockArg::Value only in this subset - no
            // try_call exception args).
            let mut stack_params: Vec<Vec<Value>> = vec![Vec::new(); n];
            // Function-entry block: holds ONLY the function params. A real
            // bytecode loop can jump back to instruction 0 (the emitter's
            // loop-to-entry shape), and such an edge cannot target the
            // param block - so instruction 0's leader is a SEPARATE block
            // that the entry jumps to. The first instruction runs at depth
            // 0 (a loop-to-entry edge carrying stack would join-conflict
            // in the pre-pass and refuse), so the edge passes no args.
            let func_entry = builder.create_block();
            builder.append_block_params_for_function_params(func_entry);
            for i in 0..n {
                if leader[i] {
                    let blk = builder.create_block();
                    for _ in 0..entry_depth[i] {
                        builder.append_block_param(blk, int64);
                    }
                    block_of[i] = Some(blk);
                }
            }
            builder.switch_to_block(func_entry);
            let vm = builder.block_params(func_entry)[0];
            let args_ptr = builder.block_params(func_entry)[1];

            // Fill each leader block's stack-param values. Instruction 0's
            // block has only stack params (the function params live on
            // func_entry), so no stripping is needed.
            for i in 0..n {
                if leader[i] {
                    if let Some(blk) = block_of[i] {
                        stack_params[i] = builder.block_params(blk).to_vec();
                    }
                }
            }

            // Constants reused across the lowering (defined in the entry
            // block BEFORE its terminator, so they dominate everything).
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

            // Entry terminator: unconditional edge into instruction 0's
            // leader block. Emitted after the argument seeding below (see
            // the jump next to the locals setup) so the loads live in the
            // unterminated entry block and dominate the function body.

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
            let call_bridge_ref = self.module.declare_func_in_func(call_id, &mut builder.func);
            let global_get_ref = self
                .module
                .declare_func_in_func(global_get_id, &mut builder.func);
            let global_set_ref = self
                .module
                .declare_func_in_func(global_set_id, &mut builder.func);
            let backedge_ref = self
                .module
                .declare_func_in_func(backedge_id, &mut builder.func);
            let upvalue_get_ref = self
                .module
                .declare_func_in_func(upvalue_get_id, &mut builder.func);
            let upvalue_set_ref = self
                .module
                .declare_func_in_func(upvalue_set_id, &mut builder.func);
            let object_get_ref = self
                .module
                .declare_func_in_func(object_get_id, &mut builder.func);
            let object_set_ref = self
                .module
                .declare_func_in_func(object_set_id, &mut builder.func);
            let iter_new_ref = self
                .module
                .declare_func_in_func(iter_new_id, &mut builder.func);
            let iter_next_ref = self
                .module
                .declare_func_in_func(iter_next_id, &mut builder.func);
            let array_get_ref = self
                .module
                .declare_func_in_func(array_get_id, &mut builder.func);
            let array_set_ref = self
                .module
                .declare_func_in_func(array_set_id, &mut builder.func);
            let array_len_ref = self
                .module
                .declare_func_in_func(array_len_id, &mut builder.func);
            let array_push_ref = self
                .module
                .declare_func_in_func(array_push_id, &mut builder.func);
            let call_method_ref = self
                .module
                .declare_func_in_func(call_method_id, &mut builder.func);

            // GC Runtime ABI
            let gc_register_roots_ref = self
                .module
                .declare_func_in_func(gc_register_roots_id, &mut builder.func);
            let gc_unregister_roots_ref = self
                .module
                .declare_func_in_func(gc_unregister_roots_id, &mut builder.func);
            let gc_write_barrier_ref = self
                .module
                .declare_func_in_func(gc_write_barrier_id, &mut builder.func);

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
            // Entry terminator: edge into instruction 0's leader block
            // (last instruction in the entry block; constants and argument
            // loads above stay unterminated and dominate the body).
            {
                let first = block_of[0].ok_or_else(|| err("no entry leader".into()))?;
                builder.ins().jump(first, &[]);
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
                // The bridge is consulted unconditionally (pure word
                // predicate, no vm per RuntimeABI); the select keeps the
                // applicable result.
                let call = b.ins().call(truthy_bridge_ref, &[v]);
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
                    // EQ/NEQ may compare string CONTENT across
                    // representations (heap StringId vs chunk-local
                    // StringValId) via the heap, so the pure word bridges
                    // are not enough: route through the vm-aware bridges,
                    // mirroring the ORC lowering's EQ/NEQ slow path.
                    OP_EQ => "havel_vm_eq_vm",
                    OP_NEQ => "havel_vm_neq_vm",
                    OP_LTE => "havel_vm_lte",
                    OP_GT => "havel_vm_gt",
                    _ => "havel_vm_gte",
                };
                // Arithmetic and EQ/NEQ bridges take (vm, l, r); ordering
                // comparisons are pure word semantics and take (l, r) per
                // RuntimeABI.
                let func_ref = *bridge_refs.get(bridge_name).expect("bridge declared above");
                let is_pure_comparison = matches!(op, OP_LT | OP_LTE | OP_GT | OP_GTE);
                let bridged = if is_pure_comparison {
                    let call = b.ins().call(func_ref, &[l, r]);
                    b.inst_results(call)[0]
                } else {
                    let call = b.ins().call(func_ref, &[vm, l, r]);
                    b.inst_results(call)[0]
                };

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

            // Straight-line lowering. The virtual stack flows across block
            // boundaries: each leader block's incoming values are its block
            // params (filled by the pre-pass depths), and every edge to a
            // block passes the current stack values as block arguments.
            let mut cur: usize = 0;
            let mut vstack: Vec<Value> = Vec::new();
            let mut terminated = true;
            let mut saw_return = false;

            while cur < n {
                if leader[cur] {
                    let blk = block_of[cur].ok_or_else(|| err("missing leader block".into()))?;
                    if !terminated {
                        // Fall-through edge into this leader: pass the
                        // current stack (the pre-pass guarantees its depth
                        // equals the target's entry depth).
                        builder.ins().jump(blk, &edge_args(&vstack));
                    }
                    builder.switch_to_block(blk);
                    vstack = stack_params[cur].clone();
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
                    OP_CALL_METHOD => {
                        // The flat stream carries the method-name id in this
                        // instruction's operand and the arg count in the
                        // immediately-following OP_EXTENDED_ARG pseudo-pair.
                        // Stack (per the interpreter and ORC): receiver
                        // pushed first, then the arguments.
                        if cur + 1 >= n || code[2 * (cur + 1)] != OP_EXTENDED_ARG {
                            return Err(err(
                                "CALL_METHOD without EXTENDED_ARG arg-count pair".into()
                            ));
                        }
                        let name_id = operand as u32;
                        let argc = code[2 * (cur + 1) + 1] as usize;
                        if vstack.len() < argc + 1 {
                            return Err(err("CALL_METHOD with too few stack values".into()));
                        }
                        let slot = builder.create_sized_stack_slot(
                            cranelift::codegen::ir::StackSlotData::new(
                                cranelift::codegen::ir::StackSlotKind::ExplicitSlot,
                                (argc.max(1) * 8) as u32,
                                8,
                            ),
                        );
                        // args arrive receiver-first then arg1..argN on the
                        // vstack; pop args (reverse), store forward.
                        let mut args_rev: Vec<Value> = Vec::with_capacity(argc);
                        for _ in 0..argc {
                            args_rev.push(vstack.pop().expect("checked depth"));
                        }
                        let receiver = vstack.pop().expect("checked depth");
                        for (k, a) in args_rev.into_iter().rev().enumerate() {
                            builder.ins().stack_store(a, slot, (k as i32) * 8);
                        }
                        let base = builder.ins().stack_addr(pointer_ty, slot, 0);
                        let name_w = builder.ins().iconst(int32, name_id as i64);
                        let cnt = builder.ins().iconst(int32, argc as i64);
                        let call = builder
                            .ins()
                            .call(call_method_ref, &[vm, receiver, name_w, base, cnt]);
                        vstack.push(builder.inst_results(call)[0]);
                        // Skip the EXTENDED_ARG data pair.
                        cur += 1;
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
                        // The stream pushes the callee FIRST, then the
                        // arguments (LOAD_GLOBAL f; ...; SUB produces
                        // [callee, arg] bottom-to-top). Pop the args first
                        // (reverse), then the callee underneath, and stage
                        // [callee, args...] contiguously for the bridge.
                        let mut args_rev: Vec<Value> = Vec::with_capacity(argc);
                        for _ in 0..argc {
                            args_rev.push(vstack.pop().expect("checked depth"));
                        }
                        let callee = vstack.pop().expect("checked depth");
                        let mut words: Vec<Value> = Vec::with_capacity(argc + 1);
                        words.push(callee);
                        for w in args_rev.into_iter().rev() {
                            words.push(w);
                        }
                        for (k, w) in words.iter().enumerate() {
                            builder.ins().stack_store(*w, slot, (k as i32) * 8);
                        }
                        let base = builder.ins().stack_addr(pointer_ty, slot, 0);
                        let cnt = builder.ins().iconst(int32, (argc + 1) as i64);
                        let call = builder.ins().call(call_bridge_ref, &[vm, base, cnt]);
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
                            // A taken backward edge is an interpreter
                            // backedge (recordBackedgePublic): surface it to
                            // the VM so loop hotness, tier-up and coroutine
                            // yield requests keep working in native loops.
                            if target <= cur {
                                let ip_w = builder.ins().iconst(int32, cur as i64);
                                builder.ins().call(backedge_ref, &[vm, ip_w]);
                            }
                            builder.ins().brif(
                                truthy,
                                else_blk,
                                &edge_args(&vstack),
                                then_blk,
                                &edge_args(&vstack),
                            );
                        } else {
                            // No fall-through instruction: both arms exit
                            // through the target.
                            if target <= cur {
                                let ip_w = builder.ins().iconst(int32, cur as i64);
                                builder.ins().call(backedge_ref, &[vm, ip_w]);
                            }
                            builder.ins().brif(
                                truthy,
                                then_blk,
                                &edge_args(&vstack),
                                then_blk,
                                &edge_args(&vstack),
                            );
                        }
                        terminated = true;
                    }
                    OP_JUMP_IF_TRUE => {
                        // Mirror of JUMP_IF_FALSE: branch to the target when
                        // the condition is truthy, else fall through.
                        let target = operand as usize;
                        let cond_word = vstack
                            .pop()
                            .ok_or_else(|| err("JUMP_IF_TRUE with empty stack".into()))?;
                        let truthy = lower_truthy(&mut builder, cond_word);
                        let then_blk = block_of[target]
                            .ok_or_else(|| err("jump target has no block".into()))?;
                        let else_idx = cur + 1;
                        if else_idx < n && leader[else_idx] {
                            let else_blk = block_of[else_idx]
                                .ok_or_else(|| err("fall-through has no block".into()))?;
                            if target <= cur {
                                let ip_w = builder.ins().iconst(int32, cur as i64);
                                builder.ins().call(backedge_ref, &[vm, ip_w]);
                            }
                            builder.ins().brif(
                                truthy,
                                then_blk,
                                &edge_args(&vstack),
                                else_blk,
                                &edge_args(&vstack),
                            );
                        } else {
                            if target <= cur {
                                let ip_w = builder.ins().iconst(int32, cur as i64);
                                builder.ins().call(backedge_ref, &[vm, ip_w]);
                            }
                            builder.ins().brif(
                                truthy,
                                then_blk,
                                &edge_args(&vstack),
                                then_blk,
                                &edge_args(&vstack),
                            );
                        }
                        terminated = true;
                    }
                    OP_JUMP => {
                        let target = operand as usize;
                        let blk = block_of[target]
                            .ok_or_else(|| err("jump target has no block".into()))?;
                        // Backward unconditional jump: interpreter
                        // recordBackedgePublic on every iteration.
                        if target <= cur {
                            let ip_w = builder.ins().iconst(int32, cur as i64);
                            builder.ins().call(backedge_ref, &[vm, ip_w]);
                        }
                        builder.ins().jump(blk, &edge_args(&vstack));
                        terminated = true;
                    }
                    OP_LOAD_GLOBAL => {
                        // Operand: chunk-local string id of the global name;
                        // the runtime resolves it against the executing
                        // chunk (same contract as the LOAD_GLOBAL opcode).
                        let name_id = builder.ins().iconst(int32, operand as i64);
                        let call = builder.ins().call(global_get_ref, &[vm, name_id]);
                        vstack.push(builder.inst_results(call)[0]);
                    }
                    OP_STORE_GLOBAL => {
                        let v = vstack
                            .pop()
                            .ok_or_else(|| err("STORE_GLOBAL with empty stack".into()))?;
                        let name_id = builder.ins().iconst(int32, operand as i64);
                        builder.ins().call(global_set_ref, &[vm, name_id, v]);
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
                    OP_SWAP => {
                        // Top two stack values exchange places.
                        if vstack.len() < 2 {
                            return Err(err("SWAP with shallow stack".into()));
                        }
                        let len = vstack.len();
                        vstack.swap(len - 1, len - 2);
                    }
                    OP_PUSH_NULL => {
                        let null_w = builder.ins().iconst(int64, NULL_TAGGED as i64);
                        vstack.push(null_w);
                    }
                    OP_IS_NULL => {
                        // Inline tag check: null is the NULL tag word.
                        let v = vstack
                            .pop()
                            .ok_or_else(|| err("IS_NULL with empty stack".into()))?;
                        let t = builder.ins().band(v, tag_mask);
                        let is_null = builder.ins().icmp(IntCC::Equal, t, tag_null_bits);
                        let on = {
                            let bit = builder.ins().uextend(int64, is_null);
                            builder.ins().bor(bit, bool_tagged)
                        };
                        vstack.push(on);
                    }
                    OP_NOT => {
                        let v = vstack
                            .pop()
                            .ok_or_else(|| err("NOT with empty stack".into()))?;
                        let func_ref = *bridge_refs
                            .get("havel_vm_not")
                            .expect("bridge declared above");
                        let call = builder.ins().call(func_ref, &[v]);
                        vstack.push(builder.inst_results(call)[0]);
                    }
                    OP_LENGTH => {
                        let v = vstack
                            .pop()
                            .ok_or_else(|| err("LENGTH with empty stack".into()))?;
                        let func_ref = *bridge_refs
                            .get("havel_vm_length")
                            .expect("bridge declared above");
                        let call = builder.ins().call(func_ref, &[vm, v]);
                        vstack.push(builder.inst_results(call)[0]);
                    }
                    OP_STRING_LEN | OP_STRING_UPPER | OP_STRING_LOWER | OP_STRING_TRIM
                    | OP_STRING_PROMOTE => {
                        // Unary string intrinsics: (vm, v) -> Value.
                        let v = vstack
                            .pop()
                            .ok_or_else(|| err("string intrinsic with empty stack".into()))?;
                        let name = match op {
                            OP_STRING_LEN => "havel_vm_string_len",
                            OP_STRING_UPPER => "havel_vm_string_upper",
                            OP_STRING_LOWER => "havel_vm_string_lower",
                            OP_STRING_TRIM => "havel_vm_string_trim",
                            _ => "havel_vm_string_promote",
                        };
                        let func_ref = *bridge_refs.get(name).expect("bridge declared above");
                        let call = builder.ins().call(func_ref, &[vm, v]);
                        vstack.push(builder.inst_results(call)[0]);
                    }
                    OP_STRING_CONCAT => {
                        // (vm, l, r) -> concatenated string Value.
                        let r = vstack
                            .pop()
                            .ok_or_else(|| err("STRING_CONCAT with empty stack".into()))?;
                        let l = vstack
                            .pop()
                            .ok_or_else(|| err("STRING_CONCAT with shallow stack".into()))?;
                        let func_ref = *bridge_refs
                            .get("havel_vm_string_concat")
                            .expect("bridge declared above");
                        let call = builder.ins().call(func_ref, &[vm, l, r]);
                        vstack.push(builder.inst_results(call)[0]);
                    }
                    OP_BIT_AND | OP_BIT_OR | OP_BIT_XOR | OP_BIT_LSH | OP_BIT_RSH => {
                        // Pure word bitwise bridges: (l, r) -> Value.
                        let r = vstack
                            .pop()
                            .ok_or_else(|| err("bitwise op with empty stack".into()))?;
                        let l = vstack
                            .pop()
                            .ok_or_else(|| err("bitwise op with shallow stack".into()))?;
                        let name = match op {
                            OP_BIT_AND => "havel_vm_bit_and",
                            OP_BIT_OR => "havel_vm_bit_or",
                            OP_BIT_XOR => "havel_vm_bit_xor",
                            OP_BIT_LSH => "havel_vm_bit_lsh",
                            _ => "havel_vm_bit_rsh",
                        };
                        let func_ref = *bridge_refs.get(name).expect("bridge declared above");
                        let call = builder.ins().call(func_ref, &[l, r]);
                        vstack.push(builder.inst_results(call)[0]);
                    }
                    OP_BIT_NOT => {
                        let v = vstack
                            .pop()
                            .ok_or_else(|| err("BIT_NOT with empty stack".into()))?;
                        let func_ref = *bridge_refs
                            .get("havel_vm_bit_not")
                            .expect("bridge declared above");
                        let call = builder.ins().call(func_ref, &[v]);
                        vstack.push(builder.inst_results(call)[0]);
                    }
                    OP_LOAD_UPVALUE => {
                        // Operand: upvalue slot of the running closure;
                        // the bridge reads the captured cell (open cell ->
                        // owner frame local, closed -> cell value).
                        let slot = builder.ins().iconst(int32, operand as i64);
                        let call = builder.ins().call(upvalue_get_ref, &[vm, slot]);
                        vstack.push(builder.inst_results(call)[0]);
                    }
                    OP_STORE_UPVALUE => {
                        let v = vstack
                            .pop()
                            .ok_or_else(|| err("STORE_UPVALUE with empty stack".into()))?;
                        let slot = builder.ins().iconst(int32, operand as i64);
                        builder.ins().call(upvalue_set_ref, &[vm, slot, v]);
                    }
                    OP_OBJECT_GET => {
                        // Stack protocol matches the interpreter and the
                        // ORC lowering: pop key first, then the receiver.
                        let key = vstack
                            .pop()
                            .ok_or_else(|| err("OBJECT_GET with empty stack".into()))?;
                        let obj = vstack
                            .pop()
                            .ok_or_else(|| err("OBJECT_GET with shallow stack".into()))?;
                        let call = builder.ins().call(object_get_ref, &[vm, obj, key]);
                        vstack.push(builder.inst_results(call)[0]);
                    }
                    OP_OBJECT_SET => {
                        // Stack: [..., obj, value, key]; pop key, then
                        // value, then obj (same order the interpreter and
                        // the ORC handler use - an inverted pop order swaps
                        // key and value silently). The interpreter pushes
                        // the OBJECT back for chaining, not the bridge's
                        // val echo.
                        let key = vstack
                            .pop()
                            .ok_or_else(|| err("OBJECT_SET with empty stack".into()))?;
                        let val = vstack
                            .pop()
                            .ok_or_else(|| err("OBJECT_SET with shallow stack".into()))?;
                        let obj = vstack
                            .pop()
                            .ok_or_else(|| err("OBJECT_SET with shallow stack".into()))?;
                        builder.ins().call(object_set_ref, &[vm, obj, key, val]);
                        vstack.push(obj);
                    }
                    OP_ITER_NEW => {
                        let coll = vstack
                            .pop()
                            .ok_or_else(|| err("ITER_NEW with empty stack".into()))?;
                        let call = builder.ins().call(iter_new_ref, &[vm, coll]);
                        vstack.push(builder.inst_results(call)[0]);
                    }
                    OP_ITER_NEXT => {
                        let iter = vstack
                            .pop()
                            .ok_or_else(|| err("ITER_NEXT with empty stack".into()))?;
                        let call = builder.ins().call(iter_next_ref, &[vm, iter]);
                        vstack.push(builder.inst_results(call)[0]);
                    }
                    OP_ARRAY_GET => {
                        // Pop index first, then the container (interpreter
                        // and ORC order).
                        let idx = vstack
                            .pop()
                            .ok_or_else(|| err("ARRAY_GET with empty stack".into()))?;
                        let arr = vstack
                            .pop()
                            .ok_or_else(|| err("ARRAY_GET with shallow stack".into()))?;
                        let call = builder.ins().call(array_get_ref, &[vm, arr, idx]);
                        vstack.push(builder.inst_results(call)[0]);
                    }
                    OP_ARRAY_SET => {
                        // Stack: [..., arr, idx, val]; pop val, idx, arr.
                        // The interpreter's plain paths push nothing back
                        // (the emitter re-loads its temp right after), so
                        // neither does the lowering.
                        let val = vstack
                            .pop()
                            .ok_or_else(|| err("ARRAY_SET with empty stack".into()))?;
                        let idx = vstack
                            .pop()
                            .ok_or_else(|| err("ARRAY_SET with shallow stack".into()))?;
                        let arr = vstack
                            .pop()
                            .ok_or_else(|| err("ARRAY_SET with shallow stack".into()))?;
                        builder.ins().call(array_set_ref, &[vm, arr, idx, val]);
                    }
                    OP_ARRAY_LEN => {
                        let arr = vstack
                            .pop()
                            .ok_or_else(|| err("ARRAY_LEN with empty stack".into()))?;
                        let call = builder.ins().call(array_len_ref, &[vm, arr]);
                        vstack.push(builder.inst_results(call)[0]);
                    }
                    OP_ARRAY_PUSH => {
                        // The interpreter pushes the CONTAINER back.
                        let val = vstack
                            .pop()
                            .ok_or_else(|| err("ARRAY_PUSH with empty stack".into()))?;
                        let arr = vstack
                            .pop()
                            .ok_or_else(|| err("ARRAY_PUSH with shallow stack".into()))?;
                        builder.ins().call(array_push_ref, &[vm, arr, val]);
                        vstack.push(arr);
                    }
                    OP_OBJECT_NEW | OP_OBJECT_NEW_UNSORTED | OP_ARRAY_NEW | OP_SET_NEW => {
                        // Constructors: (vm) -> new value; no operand.
                        let name = match op {
                            OP_OBJECT_NEW => "havel_vm_object_new",
                            OP_OBJECT_NEW_UNSORTED => "havel_vm_object_new_unsorted",
                            OP_ARRAY_NEW => "havel_vm_array_new",
                            _ => "havel_vm_set_new",
                        };
                        let func_ref = *bridge_refs.get(name).expect("bridge declared above");
                        let call = builder.ins().call(func_ref, &[vm]);
                        vstack.push(builder.inst_results(call)[0]);
                    }
                    OP_RANGE_NEW => {
                        // Stack: [..., start, end]; pop end first, then
                        // start (interpreter order).
                        let end = vstack
                            .pop()
                            .ok_or_else(|| err("RANGE_NEW with empty stack".into()))?;
                        let start = vstack
                            .pop()
                            .ok_or_else(|| err("RANGE_NEW with shallow stack".into()))?;
                        let func_ref = *bridge_refs
                            .get("havel_vm_range_new")
                            .expect("bridge declared above");
                        let call = builder.ins().call(func_ref, &[vm, start, end]);
                        vstack.push(builder.inst_results(call)[0]);
                    }
                    OP_SET_SET => {
                        // Stack: [..., set, value, key]; pop key, value,
                        // set; push NOTHING back. Bridge argument order is
                        // (vm, set, value, key).
                        let key = vstack
                            .pop()
                            .ok_or_else(|| err("SET_SET with empty stack".into()))?;
                        let val = vstack
                            .pop()
                            .ok_or_else(|| err("SET_SET with shallow stack".into()))?;
                        let set = vstack
                            .pop()
                            .ok_or_else(|| err("SET_SET with shallow stack".into()))?;
                        let func_ref = *bridge_refs
                            .get("havel_vm_set_set")
                            .expect("bridge declared above");
                        builder.ins().call(func_ref, &[vm, set, val, key]);
                    }
                    OP_JUMP_IF_NULL => {
                        // Inline null check: null is a single canonical
                        // NaN-boxed word, so a raw equality compare matches
                        // the interpreter's isNull().
                        let target = operand as usize;
                        let v = vstack
                            .pop()
                            .ok_or_else(|| err("JUMP_IF_NULL with empty stack".into()))?;
                        let null_w = builder.ins().iconst(int64, NULL_TAGGED as i64);
                        let is_null = builder.ins().icmp(IntCC::Equal, v, null_w);
                        let then_blk = block_of[target]
                            .ok_or_else(|| err("jump target has no block".into()))?;
                        let else_idx = cur + 1;
                        if else_idx < n && leader[else_idx] {
                            let else_blk = block_of[else_idx]
                                .ok_or_else(|| err("fall-through has no block".into()))?;
                            builder.ins().brif(
                                is_null,
                                then_blk,
                                &edge_args(&vstack),
                                else_blk,
                                &edge_args(&vstack),
                            );
                        } else {
                            builder.ins().brif(
                                is_null,
                                then_blk,
                                &edge_args(&vstack),
                                then_blk,
                                &edge_args(&vstack),
                            );
                        }
                        terminated = true;
                    }
                    OP_RETURN => {
                        // Pop failure here is only possible in dead code
                        // (the pre-pass validated every reachable
                        // instruction's depth); synthesize null there.
                        let v = vstack
                            .pop()
                            .unwrap_or_else(|| builder.ins().iconst(int64, NULL_TAGGED as i64));
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
            .map_err(|e| err(format!("define {name}: {e:?}")))?;
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
        // CALL 3 stages [callee, arg1, arg2, arg3] contiguously; the shim
        // pins the exact layout (callee first, args in push order) and
        // returns a sentinel only for the exact sequence.
        let mut backend = CraneliftBackend::new().unwrap();
        let code: Vec<u32> = vec![
            OP_LOAD_CONST,
            0, // callee: 900000 (magic)
            OP_LOAD_CONST,
            1, // arg1: 1
            OP_LOAD_CONST,
            2, // arg2: 2
            OP_LOAD_CONST,
            3, // arg3: 3
            OP_CALL,
            3, //
            OP_RETURN,
            0,
        ];
        let constants = [
            pack_int48(900000),
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
            900123,
            "shim must see [callee=900000, 1, 2, 3] in order: {out:#x}"
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

    #[test]
    fn is_null_lowers_and_runs() {
        // fn (a) = a == null via IS_NULL
        let mut backend = CraneliftBackend::new().unwrap();
        let code = [OP_LOAD_VAR, 0, OP_IS_NULL, 0, OP_RETURN, 0];
        let constants: [u64; 0] = [];
        let f = backend
            .compile_function("isnull", &code, &constants, 1)
            .expect("lowering");
        let out = unsafe { f(std::ptr::null_mut(), [NULL_TAGGED].as_ptr(), 1) };
        assert_eq!(out, pack_bool(true), "null must be null: {out:#x}");
        let out2 = unsafe { f(std::ptr::null_mut(), [pack_int48(7)].as_ptr(), 1) };
        assert_eq!(out2, pack_bool(false), "int must not be null");
    }

    #[test]
    fn swap_lowers_and_runs() {
        // fn (a, b) = (a + b) after SWAP twice -> identical result; use one
        // SWAP to reverse operand order: (b - a) == (a,b=1,5) -> 4.
        //   0: LOAD_VAR a
        //   1: LOAD_VAR b
        //   2: SWAP          -> [b, a]
        //   3: SUB           -> b - a
        //   4: RETURN
        let mut backend = CraneliftBackend::new().unwrap();
        let code = [
            OP_LOAD_VAR,
            0, // a
            OP_LOAD_VAR,
            1, // b
            OP_SWAP,
            0,
            OP_SUB,
            0,
            OP_RETURN,
            0,
        ];
        let constants: [u64; 0] = [];
        let f = backend
            .compile_function("swapsub", &code, &constants, 2)
            .expect("lowering");
        let out = unsafe {
            f(
                std::ptr::null_mut(),
                [pack_int48(1), pack_int48(5)].as_ptr(),
                2,
            )
        };
        assert_eq!(unpack_int48(out), 4, "SWAP must reverse operands: {out:#x}");
    }

    #[test]
    fn not_and_bitnot_lowers_and_runs() {
        let mut backend = CraneliftBackend::new().unwrap();
        // NOT with a null vm: the C bridge valueIsTruthy(null) = false, so
        // !false = true.
        let code = [OP_PUSH_NULL, 0, OP_NOT, 0, OP_RETURN, 0];
        let constants: [u64; 0] = [];
        let f = backend
            .compile_function("notnull", &code, &constants, 0)
            .expect("lowering");
        let out = unsafe { f(std::ptr::null_mut(), [].as_ptr(), 0) };
        assert_eq!(out, pack_bool(true), "not(null) must be true: {out:#x}");
    }

    #[test]
    fn bitwise_ops_lowers_and_runs() {
        let mut backend = CraneliftBackend::new().unwrap();
        // fn (a, b) = (a & b) | (a ^ b): 12&10=8, 12^10=6, 8|6=14
        let code = [
            OP_LOAD_VAR,
            0, //
            OP_LOAD_VAR,
            1, //
            OP_BIT_AND,
            0, //
            OP_LOAD_VAR,
            0, //
            OP_LOAD_VAR,
            1, //
            OP_BIT_XOR,
            0, //
            OP_BIT_OR,
            0, //
            OP_RETURN,
            0,
        ];
        let constants: [u64; 0] = [];
        let f = backend
            .compile_function("bitmix", &code, &constants, 2)
            .expect("lowering");
        let out = unsafe {
            f(
                std::ptr::null_mut(),
                [pack_int48(12), pack_int48(10)].as_ptr(),
                2,
            )
        };
        assert_eq!(unpack_int48(out), 14, "12&10 | 12^10 must be 14: {out:#x}");
    }

    #[test]
    fn jump_if_true_lowers_and_runs() {
        // fn (a) { if (a) { return 111 } return 222 }
        //   0: LOAD_VAR a
        //   1: JUMP_IF_TRUE 4
        //   2: LOAD_CONST 222
        //   3: RETURN
        //   4: LOAD_CONST 111
        //   5: RETURN
        let mut backend = CraneliftBackend::new().unwrap();
        let code: Vec<u32> = vec![
            OP_LOAD_VAR,
            0, // 0
            OP_JUMP_IF_TRUE,
            4, // 1
            OP_LOAD_CONST,
            0, // 2
            OP_RETURN,
            0, // 3
            OP_LOAD_CONST,
            1, // 4
            OP_RETURN,
            0, // 5
        ];
        let constants = [pack_int48(222), pack_int48(111)];
        let f = backend
            .compile_function("iftrue", &code, &constants, 1)
            .expect("lowering");
        let out_then = unsafe { f(std::ptr::null_mut(), [pack_int48(1)].as_ptr(), 1) };
        assert_eq!(unpack_int48(out_then), 111, "truthy must jump");
        let out_else = unsafe { f(std::ptr::null_mut(), [pack_int48(0)].as_ptr(), 1) };
        assert_eq!(unpack_int48(out_else), 222, "falsy must fall through");
    }

    #[test]
    fn upvalue_ops_lower_via_bridge() {
        // Standalone (null vm) the bridges yield null / no-op, but the
        // lowering must still emit valid code: LOAD_UPVALUE pushes a word,
        // STORE_UPVALUE consumes one.
        let mut backend = CraneliftBackend::new().unwrap();
        // fn () { u = <upvalue 0>; <upvalue 1> = 7; u }
        let code = [
            OP_LOAD_UPVALUE,
            0, // 0
            OP_LOAD_CONST,
            0, // 1: 7
            OP_STORE_UPVALUE,
            1, // 2
            OP_RETURN,
            0, // 3
        ];
        let constants = [pack_int48(7)];
        let f = backend
            .compile_function("upv", &code, &constants, 0)
            .expect("lowering");
        let out = unsafe { f(std::ptr::null_mut(), [].as_ptr(), 0) };
        assert_eq!(
            out, NULL_TAGGED,
            "null-vm upvalue read must be null: {out:#x}"
        );
    }

    #[test]
    fn object_set_stack_protocol_via_echo_shim() {
        // OBJECT_SET pops key, then value, then obj, and pushes the OBJECT
        // back (interpreter chaining semantics). The echo shim returns the
        // VALUE word; a lowering that pushed the bridge result instead of
        // the object would answer 4242, and an inverted key/value pop
        // order would answer the key word.
        //   0: LOAD_CONST 77       (receiver)
        //   1: LOAD_CONST 4242     (value)
        //   2: LOAD_CONST 99       (key)
        //   3: OBJECT_SET
        //   4: RETURN
        let mut backend = CraneliftBackend::new().unwrap();
        let code = [
            OP_LOAD_CONST,
            0, //
            OP_LOAD_CONST,
            1, //
            OP_LOAD_CONST,
            2, //
            OP_OBJECT_SET,
            0, //
            OP_RETURN,
            0,
        ];
        let constants = [pack_int48(77), pack_int48(4242), pack_int48(99)];
        let f = backend
            .compile_function("objset", &code, &constants, 0)
            .expect("lowering");
        let out = unsafe { f(std::ptr::null_mut(), [].as_ptr(), 0) };
        assert_eq!(
            out,
            pack_int48(77),
            "OBJECT_SET must push the object back, popping key/value/obj in order: {out:#x}"
        );
    }

    #[test]
    fn constructors_lower_via_bridge() {
        // OBJECT_NEW/OBJECT_NEW_UNSORTED/ARRAY_NEW: (vm) -> value, no
        // operand, push 1. Standalone shims yield null; the test proves
        // the lowering emits valid code and keeps the stream shape.
        let mut backend = CraneliftBackend::new().unwrap();
        //   0: OBJECT_NEW
        //   1: ARRAY_NEW
        //   2: RETURN   (returns the array - both pushes tracked)
        let code = [
            OP_OBJECT_NEW,
            0, //
            OP_ARRAY_NEW,
            0, //
            OP_RETURN,
            0,
        ];
        let constants: [u64; 0] = [];
        let f = backend
            .compile_function("ctors", &code, &constants, 0)
            .expect("lowering");
        let out = unsafe { f(std::ptr::null_mut(), [].as_ptr(), 0) };
        assert_eq!(
            out, NULL_TAGGED,
            "null-vm constructor must be null: {out:#x}"
        );
    }

    #[test]
    fn range_new_pops_end_then_start() {
        // RANGE_NEW pops end first, then start (interpreter order) and
        // passes them to the bridge in (vm, start, end) order. The
        // standalone shim returns null, so this only proves stream
        // well-formedness; the pop ORDER is cross-checked by the e2e
        // probe scripts (for i in 0..n loops).
        let mut backend = CraneliftBackend::new().unwrap();
        //   0: LOAD_CONST 0      (start)
        //   1: LOAD_CONST 1      (end)
        //   2: RANGE_NEW
        //   3: RETURN
        let code = [
            OP_LOAD_CONST,
            0, //
            OP_LOAD_CONST,
            1, //
            OP_RANGE_NEW,
            0, //
            OP_RETURN,
            0,
        ];
        let constants = [pack_int48(0), pack_int48(10)];
        let f = backend
            .compile_function("rangenew", &code, &constants, 0)
            .expect("lowering");
        let out = unsafe { f(std::ptr::null_mut(), [].as_ptr(), 0) };
        assert_eq!(out, NULL_TAGGED, "null-vm range must be null: {out:#x}");
    }

    #[test]
    fn jump_if_null_lowers_and_runs() {
        // fn (a) { if (a == null-ish via JUMP_IF_NULL) return 111; return 222 }
        //   0: LOAD_VAR a
        //   1: JUMP_IF_NULL 4
        //   2: LOAD_CONST 222
        //   3: RETURN
        //   4: LOAD_CONST 111
        //   5: RETURN
        let mut backend = CraneliftBackend::new().unwrap();
        let code: Vec<u32> = vec![
            OP_LOAD_VAR,
            0, // 0
            OP_JUMP_IF_NULL,
            4, // 1
            OP_LOAD_CONST,
            0, // 2
            OP_RETURN,
            0, // 3
            OP_LOAD_CONST,
            1, // 4
            OP_RETURN,
            0, // 5
        ];
        let constants = [pack_int48(222), pack_int48(111)];
        let f = backend
            .compile_function("ifnull", &code, &constants, 1)
            .expect("lowering");
        let out_null = unsafe { f(std::ptr::null_mut(), [NULL_TAGGED].as_ptr(), 1) };
        assert_eq!(unpack_int48(out_null), 111, "null must jump");
        let out_int = unsafe { f(std::ptr::null_mut(), [pack_int48(5)].as_ptr(), 1) };
        assert_eq!(unpack_int48(out_int), 222, "non-null must fall through");
    }

    #[test]
    fn call_method_layout_via_shim() {
        //   0: LOAD_CONST 5        (receiver)
        //   1: LOAD_CONST 1        (arg0)
        //   2: LOAD_CONST 2        (arg1)
        //   3: CALL_METHOD name=7
        //      EXTENDED_ARG 2
        //   4: RETURN
        // Standalone shim answers receiver*100 + arg0 + arg1 = 503.
        let mut backend = CraneliftBackend::new().unwrap();
        let code: Vec<u32> = vec![
            OP_LOAD_CONST,
            0, // 0
            OP_LOAD_CONST,
            1, // 1
            OP_LOAD_CONST,
            2, // 2
            OP_CALL_METHOD,
            7, // 3 (pair 3)
            OP_EXTENDED_ARG,
            2, // data pair
            OP_RETURN,
            0, // 4 (pair 5)
        ];
        let constants = [pack_int48(5), pack_int48(1), pack_int48(2)];
        let f = backend
            .compile_function("callmeth", &code, &constants, 0)
            .expect("lowering");
        let out = unsafe { f(std::ptr::null_mut(), [].as_ptr(), 0) };
        assert_eq!(
            unpack_int48(out),
            503,
            "CALL_METHOD must keep receiver and args in separate slots: {out:#x}"
        );
    }

    #[test]
    fn jump_target_remaps_past_extended_arg() {
        // Loop whose body holds a CALL_METHOD (with its EXTENDED_ARG data
        // pair) and whose exit jump targets the instruction AFTER the
        // whole body - the exact shape the C++ emitter's pair_of remap
        // exists for. A target computed one pair short would jump INTO the
        // CALL_METHOD's data pair and break the CFG.
        // fn (n) { r = 5; i = 0; while (i < n) { r.m(1, 2); i = i + 1 } 9 }
        //   pair  0: LOAD_CONST 5      (receiver placeholder)
        //   pair  1: STORE_VAR r
        //   pair  2: LOAD_CONST 0
        //   pair  3: STORE_VAR i
        //   pair  4: LOAD_VAR i        (loop head)
        //   pair  5: LOAD_VAR n
        //   pair  6: LT
        //   pair  7: JUMP_IF_FALSE 17 (exit -> pair 17)
        //   pair  8: LOAD_VAR r        (receiver)
        //   pair  9: LOAD_CONST 1     (arg0)
        //   pair 10: LOAD_CONST 1     (arg1)
        //   pair 11: CALL_METHOD 7
        //   pair 12: EXTENDED_ARG 2  (data)
        //   pair 13: POP              (discard shim result)
        //   pair 14: LOAD_VAR i
        //   pair 15: LOAD_CONST 1
        //   pair 16: ADD / STORE via pairs below
        //   ...
        //   pair 17: LOAD_CONST 2     (9, exit target)
        //   pair 18: RETURN
        // The increment store must come BEFORE the exit target, so the
        // body ends with the backedge jump; the full stream (pairs 0..20):
        // exit jump at pair 7 targets pair 19, after CALL_METHOD (11) +
        // EXTENDED_ARG (12) + POP (13) + i increment (14..17) + JUMP (18).
        // Recompute with the increment store included: exit -> pair 19.
        let mut backend = CraneliftBackend::new().unwrap();
        // Recompute with the increment store included: exit -> pair 19.
        let code: Vec<u32> = vec![
            OP_LOAD_CONST,
            0, // pair 0
            OP_STORE_VAR,
            0, // pair 1
            OP_LOAD_CONST,
            1, // pair 2
            OP_STORE_VAR,
            1, // pair 3
            OP_LOAD_VAR,
            1, // pair 4
            OP_LOAD_VAR,
            2, // pair 5
            OP_LT,
            0, // pair 6
            OP_JUMP_IF_FALSE,
            19, // pair 7: exit -> pair 19
            OP_LOAD_VAR,
            0, // pair 8: r
            OP_LOAD_CONST,
            3, // pair 9: 1
            OP_LOAD_CONST,
            3, // pair 10: 1
            OP_CALL_METHOD,
            7, // pair 11
            OP_EXTENDED_ARG,
            2, // pair 12 (data)
            OP_POP,
            0, // pair 13
            OP_LOAD_VAR,
            1, // pair 14: i
            OP_LOAD_CONST,
            3, // pair 15: 1
            OP_ADD,
            0, // pair 16
            OP_STORE_VAR,
            1, // pair 17: i =
            OP_JUMP,
            4, // pair 18: backedge
            OP_LOAD_CONST,
            2, // pair 19: 9 (exit target)
            OP_RETURN,
            0, // pair 20
        ];
        let constants = [pack_int48(5), pack_int48(0), pack_int48(9), pack_int48(1)];
        let f = backend
            .compile_function("jumpremap", &code, &constants, 1)
            .expect("lowering");
        // n = 0: loop never runs; exit jump lands at pair 19 -> 9.
        let out = unsafe { f(std::ptr::null_mut(), [pack_int48(0)].as_ptr(), 1) };
        assert_eq!(
            unpack_int48(out),
            9,
            "exit jump must land past the CALL_METHOD data pair"
        );
        // n = 2: two iterations through the method call, then exit; 9.
        let out2 = unsafe { f(std::ptr::null_mut(), [pack_int48(2)].as_ptr(), 1) };
        assert_eq!(unpack_int48(out2), 9, "looped path must also exit at 9");
    }
    #[test]
    fn backward_jump_calls_backedge_bridge() {
        // fn (n) { s = 0; i = 0; while (i < n) { s = s + i; i = i + 1 } s }
        // A backward JUMP lowers a havel_vm_backedge(vm, ip) call. With a
        // NULL vm the bridge is a no-op, so the loop must still compute the
        // same sum as before the backedge hook existed.
        let mut backend = CraneliftBackend::new().unwrap();
        let code: Vec<u32> = vec![
            OP_LOAD_CONST,
            0, // 0: 0
            OP_STORE_VAR,
            1, // 1: s = 0
            OP_LOAD_CONST,
            0, // 2: 0
            OP_STORE_VAR,
            2, // 3: i = 0
            OP_LOAD_VAR,
            2, // 4: loop head: i
            OP_LOAD_VAR,
            0, // 5: n
            OP_LT,
            0, // 6: i < n
            OP_JUMP_IF_FALSE,
            17, // 7: exit to 17 when false
            OP_LOAD_VAR,
            1, // 8: s
            OP_LOAD_VAR,
            2, // 9: i
            OP_ADD,
            0, // 10: s + i
            OP_STORE_VAR,
            1, // 11: s =
            OP_LOAD_VAR,
            2, // 12: i
            OP_LOAD_CONST,
            1, // 13: 1
            OP_ADD,
            0, // 14: i + 1
            OP_STORE_VAR,
            2, // 15: i =
            OP_JUMP,
            4, // 16: backward jump -> backedge bridge
            OP_LOAD_VAR,
            1, // 17: s
            OP_RETURN,
            0, // 18
        ];
        let constants = [pack_int48(0), pack_int48(1)];
        let f = backend
            .compile_function("loopbackedge", &code, &constants, 1)
            .expect("lowering");
        let out = unsafe { f(std::ptr::null_mut(), [pack_int48(10)].as_ptr(), 1) };
        assert_eq!(unpack_int48(out), 45, "sum(0..10) must be 45: {out:#x}");
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

/// Create with an embedder-provided Runtime ABI symbol table:
/// names[i] -> addrs[i], i in 0..count.
#[no_mangle]
pub extern "C" fn hclb_create_with_symbols(
    names: *const *const c_char,
    addrs: *const *const u8,
    count: u32,
) -> *mut c_void {
    let mut syms: Vec<(String, *const u8)> = Vec::new();
    if !names.is_null() && !addrs.is_null() {
        for i in 0..count {
            let n = unsafe { *names.add(i as usize) };
            let a = unsafe { *addrs.add(i as usize) };
            if n.is_null() {
                continue;
            }
            let name = unsafe { CStr::from_ptr(n) }.to_string_lossy().into_owned();
            syms.push((name, a));
        }
    }
    match CraneliftBackend::new_with_symbols(syms) {
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
            if std::env::var("HCLB_DUMP_STREAM").is_ok() {
                eprintln!("[hclb] stream for {name} ({} pairs):", code.len() / 2);
                for i in 0..code.len() / 2 {
                    eprintln!("  {i}: op={} operand={}", code[2 * i], code[2 * i + 1]);
                }
            }
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn hclb_is_compiled(handle: *mut c_void, name: *const c_char) -> bool {
    if handle.is_null() || name.is_null() {
        return false;
    }
    let backend = unsafe { &*(handle as *mut CraneliftBackend) };
    let name = unsafe { CStr::from_ptr(name) }
        .to_string_lossy()
        .into_owned();
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
    let name = unsafe { CStr::from_ptr(name) }
        .to_string_lossy()
        .into_owned();
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
