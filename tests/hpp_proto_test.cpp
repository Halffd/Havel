// Havel++ prototype integration test (TODO2.md #16 / #26 item 13).
//
// Proves the .hvp -> native .so path end to end:
//   1. havel-hppc transpiles a .hvp module to a .so exposing the module ABI
//      (havel_module_info / havel_module_register) with the host's build id
//   2. the module loader accepts it (build-identity gate)
//   3. register_fn registers through VMApi; the exported functions are
//      callable with correct Value<->native conversions (int, double,
//      string round-trips)
//
// ABI sharing (TODO2.md #13) holds by construction: the generated code
// registers through VMApi, the same surface every native module uses.

#include "c/ModulePlugin.h"
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using havel::compiler::Value;

// Locate havel-hppc next to this test binary (both land in the build dir).
std::string findHppc() {
    std::vector<char> buf(4096);
    ssize_t n = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
    if (n <= 0) return "";
    std::string exe(buf.data(), static_cast<size_t>(n));
    auto dir = std::filesystem::path(exe).parent_path();
    auto candidate = dir / "havel-hppc";
    if (std::filesystem::exists(candidate)) return candidate.string();
    return "";
}

class HppProtoTest : public ::testing::Test {
protected:
    void SetUp() override {
        hppc_ = findHppc();
        root_ = "/tmp/opencode/hpp-proto-" + std::to_string(::getpid());
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
        std::filesystem::create_directories(root_);
    }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    std::string hppc_;
    std::string root_;
};

TEST_F(HppProtoTest, TranspilesAndLoadsViaModuleAbi) {
    if (hppc_.empty()) {
        GTEST_SKIP() << "havel-hppc not built";
    }
    // 1. The .hvp module: int/double/string exports.
    const std::string hvpPath = root_ + "/mymath.hvp";
    {
        std::ofstream f(hvpPath);
        f << "// module: mymath 1.0.0 \"Havel++ prototype math\"\n"
          << "\n"
          << "export int add(int a, int b) {\n"
          << "    return a + b;\n"
          << "}\n"
          << "\n"
          << "export double scale(double x, double f) {\n"
          << "    return x * f;\n"
          << "}\n"
          << "\n"
          << "export string greet(string name) {\n"
          << "    const std::string out = \"hello \";\n"
          << "    return out + name;\n"
          << "}\n";
    }

    // 2. Transpile with the HOST's build id: the loader rejects plugins
    // whose build_id differs (the build-identity gate).
    const std::string soPath = root_ + "/mymath.so";
    std::string cmd = hppc_ + " \"" + hvpPath + "\" -o \"" + soPath + "\"";
    cmd += " --build-id " + std::string(HAVEL_MODULE_BUILD_ID_STR(HAVEL_MODULE_BUILD_ID));
    cmd += " -I " HAVEL_SRC_DIR "/src -I " HAVEL_SRC_DIR "/src/havel-lang -I " HAVEL_SRC_DIR "/include";
    const int rc = std::system(cmd.c_str());
    ASSERT_EQ(rc, 0) << "havel-hppc failed: " << cmd;
    ASSERT_TRUE(std::filesystem::exists(soPath)) << "no .so produced";

    // 3. The module ABI: dlopen + havel_module_info, build id must match.
    void* dl = dlopen(soPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    ASSERT_NE(dl, nullptr) << "dlopen failed: " << dlerror();
    auto infoFn = reinterpret_cast<const HavelModuleABI* (*)(void)>(
        dlsym(dl, "havel_module_info"));
    ASSERT_NE(infoFn, nullptr) << "no havel_module_info export";
    const HavelModuleABI* abi = infoFn();
    ASSERT_NE(abi, nullptr);
    EXPECT_STREQ(abi->name, "mymath");
    EXPECT_STREQ(abi->build_id, HAVEL_MODULE_BUILD_ID_STR(HAVEL_MODULE_BUILD_ID))
        << "build id mismatch: the loader would reject this plugin";

    // 4. register_fn through VMApi: the exported functions register on the
    // VM and convert Value<->native correctly.
    havel::compiler::VM vm;
    {
        havel::compiler::VMApi api(vm);
        ASSERT_NE(abi->register_fn, nullptr);
        abi->register_fn(static_cast<void*>(&api));
    }
    ASSERT_TRUE(vm.hasHostFunction("mymath.add"));
    ASSERT_TRUE(vm.hasHostFunction("mymath.scale"));
    ASSERT_TRUE(vm.hasHostFunction("mymath.greet"));

    // 5. The module object + function refs, exactly the shape a Havel
    // `use mymath` resolves: setGlobal("mymath", obj) with makeFunctionRef
    // fields; call them through VMApi (the canonical module-call surface).
    havel::compiler::VMApi api(vm);
    const Value modObj = vm.getGlobals().count("mymath")
                             ? vm.getGlobals().at("mymath")
                             : Value::makeNull();
    ASSERT_TRUE(modObj.isObjectId()) << "module global 'mymath' is not an object";
    auto callField = [&](const char* field,
                         std::vector<Value> args) -> Value {
        Value fv = api.getField(modObj, field);
        if (!fv.isHostFuncId()) {
            ADD_FAILURE() << "field " << field << " is not a function ref";
            return Value::makeNull();
        }
        Value r = vm.callFunction(fv, args);
        return r;
    };
    {
        // int: add(19, 23) -> 42
        Value r = callField("add", {Value::makeInt(19), Value::makeInt(23)});
        EXPECT_TRUE(r.isInt());
        EXPECT_EQ(r.asInt(), 42);
    }
    {
        // double: scale(2.5, 4.0) -> 10.0
        Value r = callField("scale", {Value::makeDouble(2.5), Value::makeDouble(4.0)});
        EXPECT_TRUE(r.isDouble());
        EXPECT_NEAR(r.asDouble(), 10.0, 1e-9);
    }
    {
        // string: greet("world") -> "hello world" (heap round-trip)
        auto strRef = vm.getHeap().allocateString("world");
        Value r = callField("greet", {Value::makeStringId(strRef.id)});
        EXPECT_TRUE(r.isStringId() || r.isStringValId());
        EXPECT_EQ(vm.toString(r), "hello world");
    }
}

}  // namespace
