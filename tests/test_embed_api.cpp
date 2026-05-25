#include "Havel.hpp"
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

    std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===" << std::endl;
    return failed > 0 ? 1 : 0;
}
