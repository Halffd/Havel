#pragma once

// ===== Runtime ABI (TODO.md #27) =====
//
// The stable internal ABI between generated code (LLVM/ORC JIT, AOT objects,
// future Cranelift backends) and the Havel runtime. Generated native code
// must call the runtime ONLY through these entry points - never by embedding
// VM internals (heap layout, frame arena, Value boxing details).
//
// Single source of truth: the HAVEL_RUNTIME_ABI X-macro below declares every
// symbol once and drives:
//   * the extern "C" declarations backends #include (bottom of this file),
//   * the LLJIT symbol-map registration in BytecodeOrcJIT.cpp,
//   * the runtime ABI drift guard (scripts/check_runtime_abi.sh, ctest
//     runtime-abi-drift-guard), which fails when a definition in the runtime
//     .cpps is missing here or vice versa.
//
// Conventions:
//   * Value arguments/results are raw 64-bit NaN-boxed payload words
//     (Value::rawBits()); the ABI never passes C++ objects.
//   * vm_ptr is an opaque VM* owned by the embedding pipeline.
//   * *_id/name arguments are chunk-local uint32 ids resolved against the
//     executing chunk (strings, globals, fields).
//   * havel::compiler::JITStackFrame (BytecodeOrcJIT.h) is passed as an
//     opaque handle; the ABI never inspects its layout.
//   * Operations that fail (missing global, bad index) yield null/nil or
//     throw via havel_vm_throw_*; they never return garbage.

#include <cstdint>

namespace havel::compiler {
struct JITStackFrame;  // per-function GC/exception frame (BytecodeOrcJIT.h)
}  // namespace havel::compiler

// ENTRY(symbol, return-type, (params), "contract")
#define HAVEL_RUNTIME_ABI(ENTRY) \
  ENTRY(havel_deoptimize, void, (void *, uint64_t, uint64_t, const char *), "Bail from JIT execution back to the interpreter at the current ip. Never returns to the caller.") \
  ENTRY(havel_gc_register_roots, void, (void *, havel::compiler::JITStackFrame*, uint64_t *, uint32_t), "Register `count` raw Value slots (JIT frame locals/stack) as GC roots.") \
  ENTRY(havel_gc_unregister_roots, void, (havel::compiler::JITStackFrame*), "Drop a previously registered root set.") \
  ENTRY(havel_gc_write_barrier, void, (void* vm_ptr, uint64_t new_value_bits), "Pin `new_value_bits` as an external GC root so a stored heap reference survives collection while a JIT frame holds it raw.") \
  ENTRY(havel_vm_array_del, uint64_t, (void* vm_ptr, uint64_t arr_bits, uint64_t idx_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_array_filter, uint64_t, (void* vm_ptr, uint64_t arr_bits, uint64_t fn_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_array_find, uint64_t, (void* vm_ptr, uint64_t arr_bits, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_array_foreach, uint64_t, (void* vm_ptr, uint64_t arr_bits, uint64_t fn_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_array_freeze, uint64_t, (void* vm_ptr, uint64_t arr_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_array_get, uint64_t, (void* vm_ptr, uint64_t arr_bits, uint64_t idx_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_array_has, uint64_t, (void* vm_ptr, uint64_t arr_bits, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_array_len, uint64_t, (void* vm_ptr, uint64_t arr_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_array_map, uint64_t, (void* vm_ptr, uint64_t arr_bits, uint64_t fn_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_array_new, uint64_t, (void* vm_ptr), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_array_pop, uint64_t, (void* vm_ptr, uint64_t arr_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_array_push, void, (void* vm_ptr, uint64_t arr_bits, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_array_reduce, uint64_t, (void* vm_ptr, uint64_t arr_bits, uint64_t fn_bits, uint64_t init_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_array_set, uint64_t, (void* vm_ptr, uint64_t arr_bits, uint64_t idx_bits, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_as_type, uint64_t, (void* vm_ptr, uint64_t val_bits, uint32_t type_name_id), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_await, uint64_t, (void* vm_ptr, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_backedge, void, (void* vm_ptr, uint32_t ip), "Loop-backedge hook: suspension bookkeeping and tiering counters.") \
  ENTRY(havel_vm_begin_module, uint64_t, (void* vm_ptr), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_bit_and, uint64_t, (uint64_t a_bits, uint64_t b_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_bit_lsh, uint64_t, (uint64_t a_bits, uint64_t b_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_bit_not, uint64_t, (uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_bit_or, uint64_t, (uint64_t a_bits, uint64_t b_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_bit_rsh, uint64_t, (uint64_t a_bits, uint64_t b_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_bit_xor, uint64_t, (uint64_t a_bits, uint64_t b_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_call, uint64_t, (void *vm_ptr, uint64_t *args, uint32_t count), "Call a bytecode/JIT function. args[0] is the callee Value; the rest are arguments. Returns the result Value bits.") \
  ENTRY(havel_vm_call_dyn, uint64_t, (void *vm_ptr, uint32_t arg_count), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_call_host, uint64_t, (void* vm_ptr, uint32_t host_idx, uint64_t* args, uint32_t count), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_call_if_function, uint64_t, (void *vm_ptr, uint64_t val_raw), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_call_method, uint64_t, (void* vm_ptr, uint64_t receiver_bits, uint32_t method_name_id, uint64_t* args, uint32_t arg_count), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_call_method_spread, uint64_t, (void *vm_ptr, uint64_t receiver_raw, uint32_t method_name_id, uint32_t lit_before, uint32_t lit_after, uint64_t array_raw), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_call_spread, uint64_t, (void *vm_ptr, uint64_t callee_raw, uint32_t lit_before, uint32_t lit_after, uint64_t array_raw), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_call_super, uint64_t, (void* vm_ptr, uint64_t obj_bits, uint32_t method_id, uint64_t* args, uint32_t arg_count), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_channel_close, uint64_t, (void* vm_ptr, uint64_t chan_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_channel_new, uint64_t, (void* vm_ptr, uint64_t cap_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_channel_recv, uint64_t, (void* vm_ptr, uint64_t chan_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_channel_send, void, (void* vm_ptr, uint64_t chan_bits, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_check_yield, void, (void* vm_ptr), "Periodic scheduler/GC yield check inside hot loops.") \
  ENTRY(havel_vm_class_get_field, uint64_t, (void* vm_ptr, uint64_t obj_bits, uint32_t field_id), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_class_new, uint64_t, (void* vm_ptr, uint32_t type_id, uint32_t parent_type_id, uint32_t field_count), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_class_set_field, uint64_t, (void* vm_ptr, uint64_t obj_bits, uint32_t field_id, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_close_upvalues, void, (void* vm_ptr, uint32_t locals_base), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_closure_new, uint64_t, (void* vm_ptr, uint32_t func_index), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_collection_get_raw, uint64_t, (void* vm_ptr, uint64_t container_bits, uint64_t key_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_collection_get_raw_ic, uint64_t, (void* vm_ptr, uint64_t container_bits, uint64_t key_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_debug, uint64_t, (void* vm_ptr), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_end_module, uint64_t, (void* vm_ptr), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_enum_new, uint64_t, (void* vm_ptr, uint32_t type_id, uint32_t tag, uint32_t payload_count), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_enum_payload, uint64_t, (void* vm_ptr, uint64_t enum_bits, uint32_t idx), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_enum_tag, uint64_t, (void* vm_ptr, uint64_t enum_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_eq, uint64_t, (uint64_t l, uint64_t r), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_export_fn, uint64_t, (void* vm_ptr, uint32_t name_id, uint64_t fn_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_export_var, uint64_t, (void* vm_ptr, uint32_t name_id, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_ffi_call, uint64_t, (void *vm_ptr, uint64_t fn_ptr_raw, uint64_t ret_type_raw, uint64_t param_types_raw, uint64_t args_array_raw, uint32_t arg_count), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_ffi_callback_create, uint64_t, (void* vm_ptr, uint64_t closure_raw, uint64_t ret_type_raw, uint64_t param_types_raw), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_ffi_prepare_cif, void*, (void* fn_ptr, uint64_t ret_type_raw, uint64_t param_types_raw, uint32_t param_count), "Prepare an FFI call interface descriptor for `fn_ptr` with the raw ABI type tags. Returns an opaque CIF handle.") \
  ENTRY(havel_vm_fiber_sleep, uint64_t, (void* vm_ptr, uint64_t ms_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_global_get, uint64_t, (void *vm_ptr, uint32_t name_id), "Resolve a global by chunk string id; a missing global yields null.") \
  ENTRY(havel_vm_global_set, void, (void *vm_ptr, uint32_t name_id, uint64_t value), "Store a global by chunk string id.") \
  ENTRY(havel_vm_go_async, uint64_t, (void* vm_ptr, uint64_t fn_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_gt, uint64_t, (uint64_t l, uint64_t r), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_gte, uint64_t, (uint64_t l, uint64_t r), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_import, uint64_t, (void* vm_ptr, uint64_t path_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_import_wildcard, void, (void* vm_ptr, uint64_t exports_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_init_standalone, void*, (const char** strings, uint32_t count), "Bootstrap a standalone VM for AOT-loaded programs: install core modules and pre-intern `count` strings. Returns an opaque VM*.") \
  ENTRY(havel_vm_init_standalone_with_functions, void*, (const char** strings, uint32_t string_count, const char** func_names, uint32_t func_count, const uint32_t* func_param_counts, const uint32_t* func_local_counts, const uint32_t* func_upvalue_counts, const uint32_t* func_is_generator, const uint32_t* upvalue_indices, const uint32_t* upvalue_captures_local, uint32_t total_upvalues, const uint32_t* func_const_counts, const uint64_t* func_const_data, const uint32_t* func_instr_counts, const uint64_t* func_instr_data, uint32_t num_functions, const char* build_dir), "Bootstrap a standalone VM and load full program bytecode tables (functions, constants, instructions) for AOT execution. Returns an opaque VM*.") \
  ENTRY(havel_vm_interval_start, uint64_t, (void* vm_ptr, uint64_t duration_bits, uint64_t callback_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_interval_stop, uint64_t, (void* vm_ptr, uint64_t interval_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_is, uint64_t, (uint64_t l, uint64_t r), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_is_truthy, int, (uint64_t val_bits), "Truthiness of a raw Value word (null/false/0/empty falsy) for conditional lowering.") \
  ENTRY(havel_vm_iter_new, uint64_t, (void* vm_ptr, uint64_t coll_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_iter_next, uint64_t, (void* vm_ptr, uint64_t iter_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_length, uint64_t, (void* vm_ptr, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_load_class_proto, uint64_t, (void* vm_ptr, uint32_t type_id), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_load_exception, uint64_t, (void* vm_ptr), "Current exception Value bits (for catch blocks).") \
  ENTRY(havel_vm_locals_base, uint32_t, (void* vm_ptr), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_lt, uint64_t, (uint64_t l, uint64_t r), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_lte, uint64_t, (uint64_t l, uint64_t r), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_neq, uint64_t, (uint64_t l, uint64_t r), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_not, uint64_t, (uint64_t v), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_object_delete, uint64_t, (void* vm_ptr, uint64_t obj_bits, uint32_t key_id), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_object_delete_raw, void, (void* vm_ptr, uint64_t obj_bits, uint64_t key_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_object_entries, uint64_t, (void* vm_ptr, uint64_t obj_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_object_get, uint64_t, (void* vm_ptr, uint64_t obj_bits, uint32_t key_id), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_object_get_raw, uint64_t, (void* vm_ptr, uint64_t obj_bits, uint64_t key_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_object_get_raw_ic, uint64_t, (void* vm_ptr, uint64_t obj_bits, uint64_t key_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_object_has, uint64_t, (void* vm_ptr, uint64_t obj_bits, uint32_t key_id), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_object_has_raw, uint64_t, (void* vm_ptr, uint64_t obj_bits, uint64_t key_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_object_keys, uint64_t, (void* vm_ptr, uint64_t obj_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_object_new, uint64_t, (void* vm_ptr), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_object_new_unsorted, uint64_t, (void* vm_ptr), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_object_set, uint64_t, (void* vm_ptr, uint64_t obj_bits, uint32_t key_id, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_object_set_raw, uint64_t, (void* vm_ptr, uint64_t obj_bits, uint64_t key_bits, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_object_values, uint64_t, (void* vm_ptr, uint64_t obj_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_pow, uint64_t, (uint64_t base_bits, uint64_t exp_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_print, uint64_t, (void* vm_ptr, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_prot_cast, uint64_t, (void* vm_ptr, uint64_t value_bits, uint32_t proto_id), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_prot_check, uint64_t, (void* vm_ptr, uint64_t value_bits, uint32_t proto_id), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_range_new, uint64_t, (void* vm_ptr, uint64_t start_bits, uint64_t end_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_range_step_new, uint64_t, (void* vm_ptr, uint64_t start_bits, uint64_t end_bits, uint64_t step_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_set_del, uint64_t, (void* vm_ptr, uint64_t set_bits, uint64_t key_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_set_exception, void, (void* vm_ptr, uint64_t value_bits), "Set the current exception Value.") \
  ENTRY(havel_vm_set_new, uint64_t, (void* vm_ptr), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_set_set, uint64_t, (void* vm_ptr, uint64_t set_bits, uint64_t val_bits, uint64_t key_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_spread, uint64_t, (void* vm_ptr, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_string_concat, uint64_t, (void* vm_ptr, uint64_t l_bits, uint64_t r_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_string_ends, uint64_t, (void* vm_ptr, uint64_t str_bits, uint64_t suf_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_string_find, uint64_t, (void* vm_ptr, uint64_t str_bits, uint64_t sub_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_string_has, uint64_t, (void* vm_ptr, uint64_t str_bits, uint64_t sub_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_string_len, uint64_t, (void* vm_ptr, uint64_t str_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_string_lower, uint64_t, (void* vm_ptr, uint64_t str_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_string_promote, uint64_t, (void* vm_ptr, uint64_t str_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_string_replace, uint64_t, (void* vm_ptr, uint64_t str_bits, uint64_t old_bits, uint64_t new_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_string_split, uint64_t, (void* vm_ptr, uint64_t str_bits, uint64_t delim_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_string_starts, uint64_t, (void* vm_ptr, uint64_t str_bits, uint64_t pre_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_string_sub, uint64_t, (void* vm_ptr, uint64_t str_bits, uint64_t start_bits, uint64_t len_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_string_trim, uint64_t, (void* vm_ptr, uint64_t str_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_string_upper, uint64_t, (void* vm_ptr, uint64_t str_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_struct_get, uint64_t, (void* vm_ptr, uint64_t obj_bits, uint32_t field_id), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_struct_new, uint64_t, (void* vm_ptr, uint32_t type_id, uint64_t* args_bits, uint32_t arg_count), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_struct_set, uint64_t, (void* vm_ptr, uint64_t obj_bits, uint32_t field_id, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_tail_call, uint64_t, (void *vm_ptr, uint64_t *args, uint32_t count), "Tail-call bridge; reuses the current frame when the backend supports it.") \
  ENTRY(havel_vm_thread_join, uint64_t, (void* vm_ptr, uint64_t thread_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_thread_new, uint64_t, (void* vm_ptr, uint32_t func_id), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_thread_recv, uint64_t, (void* vm_ptr, uint64_t thread_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_thread_send, void, (void* vm_ptr, uint64_t thread_bits, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_throw_error, void, (void* vm_ptr, const char* msg), "Raise a textual runtime error. Does not return normally.") \
  ENTRY(havel_vm_throw_from_jit, void, (void* vm_ptr, uint64_t value_bits), "Same as havel_vm_throw_value; explicit JIT intent, never returns normally.") \
  ENTRY(havel_vm_throw_value, void, (void* vm_ptr, uint64_t value_bits), "Raise `value_bits` as a script exception (ScriptThrow). Does not return normally.") \
  ENTRY(havel_vm_time_now, uint64_t, (void* vm_ptr), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_timeout_cancel, uint64_t, (void* vm_ptr, uint64_t timeout_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_timeout_start, uint64_t, (void* vm_ptr, uint64_t delay_bits, uint64_t callback_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_to_bool, uint64_t, (void* vm_ptr, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_to_float, uint64_t, (void* vm_ptr, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_to_int, uint64_t, (void* vm_ptr, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_to_string, uint64_t, (void* vm_ptr, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_try_enter, void, (havel::compiler::JITStackFrame* frame, uint32_t catch_ip, uint32_t finally_ip, uint32_t stack_depth), "Install an exception handler (catch/finally ip + stack depth) on the JIT frame.") \
  ENTRY(havel_vm_try_exit, void, (havel::compiler::JITStackFrame* frame), "Pop the innermost exception handler.") \
  ENTRY(havel_vm_try_find_throw_target, uint32_t, (havel::compiler::JITStackFrame* frame, uint32_t* stack_depth_out, uint32_t* popped_count_out), "Find the landing pad for a pending throw; UINT32_MAX when uncaught.") \
  ENTRY(havel_vm_type_of, uint64_t, (void* vm_ptr, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_upvalue_get, uint64_t, (void* vm_ptr, uint32_t slot), "Read a captured local of the running closure.") \
  ENTRY(havel_vm_upvalue_set, void, (void* vm_ptr, uint32_t slot, uint64_t value), "Write a captured local of the running closure.") \
  ENTRY(havel_vm_yield, uint64_t, (void* vm_ptr, uint64_t val_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \
  ENTRY(havel_vm_yield_resume, uint64_t, (void* vm_ptr, uint64_t co_bits), "Runtime bridge for the corresponding bytecode operation; see the runtime implementation for the exact behavioral contract.") \

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif
extern "C" {

// ===== ABI declarations (generated from HAVEL_RUNTIME_ABI) =====
#define HAVEL_RUNTIME_ABI_DECL(name, ret, args, contract) ret name args;
HAVEL_RUNTIME_ABI(HAVEL_RUNTIME_ABI_DECL)
#undef HAVEL_RUNTIME_ABI_DECL

}
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
