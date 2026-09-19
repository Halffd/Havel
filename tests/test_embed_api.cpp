#include "Havel.hpp"
#include "havel-lang/compiler/core/Pipeline.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

static int passed = 0;
static int failed = 0;

#define TEST(name) \
    do { \
        std::cout << "[TEST] " << name << " ... " << std::flush; \
    } while(0)

#define PASS() \
    do { \
        std::cout << "OK" << std::endl; \
        passed++; \
    } while(0)

#define FAIL(msg) \
    do { \
        std::cout << "FAIL: " << msg << std::endl; \
        failed++; \
    } while(0)

static void test_vm_default_construct() {
    TEST("VM default construct");
    try {
        havel::VM vm;
        PASS();
    } catch (const std::exception& e) {
        FAIL(e.what());
    }
}

static void test_vm_load_int() {
    TEST("VM load int literal");
    try {
        havel::VM vm;
        auto result = vm.load("42");
        if (!result) {
            FAIL(result.error);
            return;
        }
        if (!result.value.isInt()) {
            FAIL("expected int, got type=" + std::to_string((int)result.value.type));
            return;
        }
        if (result.value.asInt() != 42) {
            FAIL("expected 42, got " + std::to_string(result.value.asInt()));
            return;
        }
        PASS();
    } catch (const std::exception& e) {
        FAIL(e.what());
    }
}

static void test_vm_load_arithmetic() {
    TEST("VM load arithmetic");
    try {
        havel::VM vm;
        auto result = vm.load("6 * 7");
        if (!result) {
            FAIL(result.error);
            return;
        }
        if (!result.value.isInt()) {
            FAIL("expected int, got type=" + std::to_string((int)result.value.type));
            return;
        }
        if (result.value.asInt() != 42) {
            FAIL("expected 42, got " + std::to_string(result.value.asInt()));
            return;
        }
        PASS();
    } catch (const std::exception& e) {
        FAIL(e.what());
    }
}

static void test_vm_load_float() {
    TEST("VM load float");
    try {
        havel::VM vm;
        auto result = vm.load("3.14");
        if (!result) {
            FAIL(result.error);
            return;
        }
        if (!result.value.isFloat()) {
            FAIL("expected float, got type=" + std::to_string((int)result.value.type));
            return;
        }
        if (std::abs(result.value.asFloat() - 3.14) > 0.001) {
            FAIL("expected 3.14, got " + std::to_string(result.value.asFloat()));
            return;
        }
        PASS();
    } catch (const std::exception& e) {
        FAIL(e.what());
    }
}

static void test_vm_load_string() {
    TEST("VM load string");
    try {
        havel::VM vm;
        auto result = vm.load("\"hello\"");
        if (!result) {
            FAIL(result.error);
            return;
        }
        if (!result.value.isString()) {
            FAIL("expected string, got type=" + std::to_string((int)result.value.type));
            return;
        }
        if (result.value.asString() != "hello") {
            FAIL("expected 'hello', got '" + result.value.asString() + "'");
            return;
        }
        PASS();
    } catch (const std::exception& e) {
        FAIL(e.what());
    }
}

static void test_vm_load_bool() {
    TEST("VM load bool");
    try {
        havel::VM vm;
        auto r1 = vm.load("true");
        if (!r1 || !r1.value.isBool() || !r1.value.asBool()) {
            FAIL("true literal");
            return;
        }
        auto r2 = vm.load("false");
        if (!r2 || !r2.value.isBool() || r2.value.asBool()) {
            FAIL("false literal");
            return;
        }
        PASS();
    } catch (const std::exception& e) {
        FAIL(e.what());
    }
}

static void test_vm_val_decl() {
    TEST("VM val declaration");
    try {
        havel::VM vm;
        auto result = vm.load("val x = 10\nx");
        if (!result) {
            FAIL(result.error);
            return;
        }
        if (!result.value.isInt() || result.value.asInt() != 10) {
            FAIL("expected 10, got " + result.value.toString());
            return;
        }
        PASS();
    } catch (const std::exception& e) {
        FAIL(e.what());
    }
}

static void test_vm_fn_call() {
    TEST("VM function definition and call");
    try {
        havel::VM vm;
        auto result = vm.load("fn add(a, b): a + b\nadd(3, 4)");
        if (!result) {
            FAIL(result.error);
            return;
        }
        if (!result.value.isInt() || result.value.asInt() != 7) {
            FAIL("expected 7, got " + result.value.toString());
            return;
        }
        PASS();
    } catch (const std::exception& e) {
        FAIL(e.what());
    }
}

static void test_vm_get_global() {
    TEST("VM getGlobal after load");
    try {
        havel::VM vm;
        auto result = vm.load("val x = 99");
        if (!result) {
            FAIL(result.error);
            return;
        }
        havel::Value g = vm.getGlobal("x");
        if (!g.isInt() || g.asInt() != 99) {
            FAIL("expected 99, got " + g.toString());
            return;
        }
        PASS();
    } catch (const std::exception& e) {
        FAIL(e.what());
    }
}

static void test_vm_set_global() {
    TEST("VM setGlobal then use in script");
    try {
        havel::VM vm;
        vm.setGlobal("external_val", havel::Value(42));
        auto result = vm.load("external_val + 8");
        if (!result) {
            FAIL(result.error);
            return;
        }
        if (!result.value.isInt() || result.value.asInt() != 50) {
            FAIL("expected 50, got " + result.value.toString());
            return;
        }
        PASS();
    } catch (const std::exception& e) {
        FAIL(e.what());
    }
}

static void test_vm_call_by_name() {
    TEST("VM call function by name");
    try {
        havel::VM vm;
        auto loadResult = vm.load("fn multiply(a, b): a * b");
        if (!loadResult) {
            FAIL(loadResult.error);
            return;
        }
        std::vector<havel::Value> callArgs = {havel::Value(6), havel::Value(7)};
        auto callResult = vm.call(std::string("multiply"), callArgs);
        if (!callResult) {
            FAIL(callResult.error);
            return;
        }
        if (!callResult.value.isInt() || callResult.value.asInt() != 42) {
            FAIL("expected 42, got " + callResult.value.toString());
            return;
        }
        PASS();
    } catch (const std::exception& e) {
        FAIL(e.what());
    }
}

static void test_vm_register_fn() {
    TEST("VM registerFn custom host function");
    try {
        havel::VM vm;
        vm.registerFn("double_it", [](havel::VM&, const std::vector<havel::Value>& args) -> havel::Value {
            if (args.empty()) return havel::Value();
            if (args[0].isInt()) return havel::Value(args[0].asInt() * 2);
            if (args[0].isFloat()) return havel::Value(args[0].asFloat() * 2.0);
            return havel::Value();
        });
        auto result = vm.load("double_it(21)");
        if (!result) {
            FAIL(result.error);
            return;
        }
        if (!result.value.isInt() || result.value.asInt() != 42) {
            FAIL("expected 42, got " + result.value.toString());
            return;
        }
        PASS();
    } catch (const std::exception& e) {
        FAIL(e.what());
    }
}

static void test_vm_register_module() {
    TEST("VM registerModule");
    try {
        havel::VM vm;
        vm.registerModule("mymod", {
            {"add", [](havel::VM&, const std::vector<havel::Value>& args) -> havel::Value {
                int64_t a = args.size() > 0 && args[0].isInt() ? args[0].asInt() : 0;
                int64_t b = args.size() > 1 && args[1].isInt() ? args[1].asInt() : 0;
                return havel::Value(a + b);
            }},
            {"greet", [](havel::VM&, const std::vector<havel::Value>& args) -> havel::Value {
                return havel::Value("hello");
            }}
        });
        auto result = vm.load("mymod.add(10, 20)");
        if (!result) {
            FAIL(result.error);
            return;
        }
        if (!result.value.isInt() || result.value.asInt() != 30) {
            FAIL("expected 30, got " + result.value.toString());
            return;
        }
        PASS();
    } catch (const std::exception& e) {
        FAIL(e.what());
    }
}

static void test_value_truthy() {
    TEST("Value isTruthy");
    try {
        assert(havel::Value().isTruthy() == false);
        assert(havel::Value(nullptr).isTruthy() == false);
        assert(havel::Value(false).isTruthy() == false);
        assert(havel::Value(true).isTruthy() == true);
        assert(havel::Value(0).isTruthy() == false);
        assert(havel::Value(1).isTruthy() == true);
        assert(havel::Value(0.0).isTruthy() == false);
        assert(havel::Value(1.0).isTruthy() == true);
        assert(havel::Value("").isTruthy() == false);
        assert(havel::Value("x").isTruthy() == true);
        PASS();
    } catch (...) {
        FAIL("assertion failed");
    }
}

static void test_value_to_string() {
    TEST("Value toString");
    try {
        assert(havel::Value().toString() == "nil");
        assert(havel::Value(true).toString() == "true");
        assert(havel::Value(false).toString() == "false");
        assert(havel::Value(42).toString() == "42");
        assert(havel::Value("hello").toString() == "hello");
        PASS();
    } catch (...) {
        FAIL("assertion failed");
    }
}

static void test_vm_error_handling() {
    TEST("VM error on bad syntax");
    try {
        havel::VM vm;
        auto result = vm.load("val x = ");
        if (result) {
            FAIL("expected error, got success: " + result.value.toString());
            return;
        }
        if (result.error.empty()) {
            FAIL("expected non-empty error message");
            return;
        }
        PASS();
    } catch (const std::exception& e) {
        FAIL(e.what());
    }
}

// ===== Pipeline integration (TODO2.md #27) =====
//
// Proves the production compilation path runs the CFG optimization pipeline
// (reconstruct -> SimplifyCFG/ConstProp/DCE -> validate -> lower) and that
// the VM executes the resulting optimized bytecode with unchanged semantics.

// Production path end to end: source -> runBytecodePipeline(optimize) -> VM.
// The script contains foldable constants, a dead store, a loop, and a branch,
// so a miscompiled optimization is observable in the result.
static void test_pipeline_optimized_script_executes() {
    TEST("pipeline: runBytecodePipeline with optimizeBytecode executes correctly");
    try {
        havel::compiler::PipelineOptions options;
        options.optimizeBytecode = true;
        // Minimal sanity first: plain top-level return.
        {
            auto r42 = havel::compiler::runBytecodePipeline("return 42", "__main__", options);
            if (!r42.return_value.isInt() || r42.return_value.asInt() != 42) {
                FAIL("sanity: 'return 42' returned non-42 (isNull=" +
                     std::to_string(r42.return_value.isNull() ? 1 : 0) + ")");
                return;
            }
        }
        const std::string src = R"havel(
val dead = 1 + 2
val x = 10 + 5
acc = 0
i = 0
while i < 5 {
    acc += x
    i += 1
}
if x == 15 { acc += 100 }
return acc
)havel";
        auto result = havel::compiler::runBytecodePipeline(src, "__main__", options);
        // x = 15; acc = 5 * 15 = 75; branch taken: 75 + 100 = 175.
        if (!result.return_value.isInt()) {
            FAIL("expected int result, got isNull=" +
                 std::to_string(result.return_value.isNull() ? 1 : 0) +
                 " isBool=" + std::to_string(result.return_value.isBool() ? 1 : 0) +
                 " isDouble=" + std::to_string(result.return_value.isDouble() ? 1 : 0) +
                 "\n--- bytecode ---\n" + result.snapshot.bytecode);
            return;
        }
        if (result.return_value.asInt() != 175) {
            FAIL("expected 175, got " + std::to_string(result.return_value.asInt()));
            return;
        }
        PASS();
    } catch (const std::exception& e) {
        FAIL(e.what());
    }
}

// Same script WITHOUT optimization must produce the identical result.
static void test_pipeline_unoptimized_script_executes() {
    TEST("pipeline: runBytecodePipeline without optimization executes correctly");
    try {
        havel::compiler::PipelineOptions options;
        const std::string src = R"havel(
val dead = 1 + 2
val x = 10 + 5
acc = 0
i = 0
while i < 5 {
    acc += x
    i += 1
}
if x == 15 { acc += 100 }
return acc
)havel";
        auto result = havel::compiler::runBytecodePipeline(src, "__main__", options);
        if (!result.return_value.isInt() || result.return_value.asInt() != 175) {
            FAIL("expected 175");
            return;
        }
        PASS();
    } catch (const std::exception& e) {
        FAIL(e.what());
    }
}

// Chunk-level transform proof: the optimized chunk keeps the CFG form and
// strictly removes instructions (dead stores + pure producers), and the VM
// executes the optimized chunk with the correct result.
static void test_pipeline_optimized_chunk_transforms() {
    TEST("pipeline: compileToBytecodeChunk optimizeBytecode keeps CFG + removes dead code");
    try {
        const std::string src = R"havel(
fn f(a) {
    val unusedLocal = a + 1
    return a * 2
}
val neverUsed = 123
return f(21)
)havel";

        havel::compiler::PipelineOptions opts_off;
        auto chunk_off = havel::compiler::compileToBytecodeChunk(src, "__main__", opts_off);
        if (!chunk_off) {
            FAIL("unoptimized compile failed");
            return;
        }

        havel::compiler::PipelineOptions opts_on;
        opts_on.optimizeBytecode = true;
        auto chunk_on = havel::compiler::compileToBytecodeChunk(src, "__main__", opts_on);
        if (!chunk_on) {
            FAIL("optimized compile failed");
            return;
        }

        size_t inst_off = 0, inst_on = 0;
        bool has_cfg = false;
        for (size_t i = 0; i < chunk_off->getFunctionCount(); ++i) {
            inst_off += chunk_off->getFunctionMutable(static_cast<uint32_t>(i))->instructions.size();
        }
        for (size_t i = 0; i < chunk_on->getFunctionCount(); ++i) {
            auto* fn = chunk_on->getFunctionMutable(static_cast<uint32_t>(i));
            inst_on += fn->instructions.size();
            if (fn->has_cfg()) has_cfg = true;
        }
        if (!has_cfg) {
            FAIL("optimized chunk did not keep CFG form (has_cfg false)");
            return;
        }
        if (inst_on >= inst_off) {
            FAIL("optimized chunk did not shrink: " + std::to_string(inst_off) + " -> " + std::to_string(inst_on));
            return;
        }

        // The optimized chunk still executes correctly on the VM.
        havel::compiler::VM vm;
        auto result = vm.execute(*chunk_on, "__main__");
        if (!result.isInt() || result.asInt() != 42) {
            FAIL("expected 42 from optimized chunk, got " +
                 std::to_string(result.isInt() ? result.asInt() : -1));
            return;
        }
        PASS();
    } catch (const std::exception& e) {
        FAIL(e.what());
    }
}

// Fast integer opcodes and host string cursor functions must keep working
// under the optimized pipeline (TODO2.md #27 acceptance criteria).
static void test_pipeline_optimized_fast_ops_and_string_cursor() {
    TEST("pipeline: fast int ops + string cursor under optimizeBytecode");
    try {
        havel::compiler::PipelineOptions options;
        options.optimizeBytecode = true;
        const std::string src = R"havel(
val a = 19
val b = 23
val sum = a + b
val prod = sum * 2
val q = prod / 4
val r = prod % 5
c = string.cursor("héllo")
string.cursor_advance(c)
val first = string.cursor_current(c)
if sum != 42 { return 0 }
if q != 21 || r != 4 { return 0 }
if first != "é" { return 0 }
return sum
)havel";
        auto result = havel::compiler::runBytecodePipeline(src, "__main__", options);
        if (!result.return_value.isInt() || result.return_value.asInt() != 42) {
            std::string got = result.return_value.isNull()
                                  ? "null"
                              : result.return_value.isInt()
                                  ? std::to_string(result.return_value.asInt())
                              : result.return_value.isDouble()
                                  ? std::to_string(result.return_value.asDouble())
                              : result.return_value.isBool()
                                  ? std::to_string(result.return_value.asBool() ? 1 : 0)
                                  : "other";
            FAIL("expected 42, got " + got + "\n--- bytecode ---\n" + result.snapshot.bytecode);
            return;
        }
        PASS();
    } catch (const std::exception& e) {
        FAIL(e.what());
    }
}

int main() {
    std::cout << "=== Havel Embeddable API Tests ===" << std::endl;

    test_vm_default_construct();
    test_vm_load_int();
    test_vm_load_arithmetic();
    test_vm_load_float();
    test_vm_load_string();
    test_vm_load_bool();
    test_vm_val_decl();
    test_vm_fn_call();
    test_vm_get_global();
    test_vm_set_global();
    test_vm_call_by_name();
    test_vm_register_fn();
    test_vm_register_module();
    test_value_truthy();
    test_value_to_string();
    test_vm_error_handling();
    test_pipeline_optimized_script_executes();
    test_pipeline_unoptimized_script_executes();
    test_pipeline_optimized_chunk_transforms();
    test_pipeline_optimized_fast_ops_and_string_cursor();

    std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===" << std::endl;
    return failed > 0 ? 1 : 0;
}
