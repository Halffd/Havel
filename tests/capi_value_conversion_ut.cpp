// C-ABI value conversion round-trips: HavelValue* <-> VM Value.
//
// The qt.*/gtk.* extension wrappers (ExtensionFunctionWrapper::callWrapper)
// depend on these conversions; before the C-API restoration every string,
// handle, array and object crossing the boundary silently became null,
// which is why qt.* was presence-only. These tests pin the contract:
//
//   - null/bool/int/float convert by value both ways
//   - strings intern through the VM (result) and copy out (arg)
//   - handles round-trip with POINTER IDENTITY through the registry:
//     widgetNew(...) gives the script an opaque object that converts back
//     to the original HavelValue
//   - arrays and objects convert recursively both directions
//   - cycles degrade to null at the depth limit instead of hanging
//   - reference counts stay balanced (built with ASan+LSan, so leaks fail
//     the test run)

#include "havel-lang/compiler/module/HavelAPI.hpp"
#include "extensions/HavelValue.h"
#include "havel-lang/compiler/vm/VM.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace havel::compiler {
namespace {

class CApiValueConversionTest : public ::testing::Test {
  protected:
    VM vm;
};

TEST_F(CApiValueConversionTest, PrimitivesConvertBothWays) {
    // null
    HavelValue* n = havel_new_null();
    EXPECT_TRUE(havelValueToValue(&vm, n).isNull());
    havel_decref(n);

    // bool
    HavelValue* b = havel_new_bool(1);
    Value bv = havelValueToValue(&vm, b);
    ASSERT_TRUE(bv.isBool());
    EXPECT_TRUE(bv.asBool());
    havel_decref(b);
    HavelValue* bBack = valueToHavelValue(&vm, bv);
    ASSERT_EQ(havel_get_type(bBack), HAVEL_BOOL);
    EXPECT_EQ(havel_get_bool(bBack), 1);
    havel_decref(bBack);

    // int
    HavelValue* i = havel_new_int(-42);
    Value iv = havelValueToValue(&vm, i);
    ASSERT_TRUE(iv.isInt());
    EXPECT_EQ(iv.asInt(), -42);
    havel_decref(i);

    // float
    HavelValue* f = havel_new_float(1.5);
    Value fv = havelValueToValue(&vm, f);
    ASSERT_TRUE(fv.isDouble());
    EXPECT_DOUBLE_EQ(fv.asDouble(), 1.5);
    havel_decref(f);

    // arg direction: VM int -> C int
    HavelValue* aBack = valueToHavelValue(&vm, Value::makeInt(7));
    ASSERT_EQ(havel_get_type(aBack), HAVEL_INT);
    EXPECT_EQ(havel_get_int(aBack), 7);
    havel_decref(aBack);
}

TEST_F(CApiValueConversionTest, StringsRoundTripThroughVM) {
    // result direction interns through the VM's string pool
    HavelValue* s = havel_new_string("hello capi");
    Value sv = havelValueToValue(&vm, s);
    ASSERT_TRUE(sv.isStringValId() || sv.isStringId());
    EXPECT_EQ(vm.resolveStringKey(sv), "hello capi");
    havel_decref(s);

    // arg direction copies out
    Value sv2 = Value::makeStringId(vm.createRuntimeString("outbound").id);
    HavelValue* back = valueToHavelValue(&vm, sv2);
    ASSERT_EQ(havel_get_type(back), HAVEL_STRING);
    EXPECT_STREQ(havel_get_string(back), "outbound");
    havel_decref(back);
}

TEST_F(CApiValueConversionTest, HandleRoundTripsWithIdentity) {
    // The contract qt widget APIs rely on: a HAVEL_HANDLE result becomes
    // an opaque VM object that converts back to the SAME HavelValue.
    static int handle_payload = 0;
    HavelValue* h = havel_new_handle(&handle_payload, nullptr);
    ASSERT_NE(h, nullptr);

    Value wrapped = havelValueToValue(&vm, h);
    ASSERT_TRUE(wrapped.isObjectId());

    // The marker field is what the arg-direction conversion keys on.
    auto oref = ObjectRef{wrapped.asObjectId(), true};
    Value marker = vm.getHostObjectField(oref, "__capi_handle");
    ASSERT_TRUE(marker.isInt());

    HavelValue* back = valueToHavelValue(&vm, wrapped);
    ASSERT_EQ(back, h); // pointer identity
    EXPECT_EQ(havel_get_handle(back), &handle_payload);
    havel_decref(back);

    havel_decref(h);
    // The registry still pins its own reference, so `h` stays valid until
    // process exit; LSan sees it as reachable, not leaked.
}

TEST_F(CApiValueConversionTest, ArrayResultConvertsRecursively) {
    // [1, true, "x", [2]]
    HavelValue* inner = havel_new_array(1);
    havel_array_push(inner, havel_new_int(2));
    HavelValue* outer = havel_new_array(4);
    havel_array_push(outer, havel_new_int(1));
    havel_array_push(outer, havel_new_bool(1));
    havel_array_push(outer, havel_new_string("x"));
    havel_array_push(outer, inner);
    havel_decref(inner);

    Value av = havelValueToValue(&vm, outer);
    ASSERT_TRUE(av.isArrayId());
    auto aref = ArrayRef{av.asArrayId()};
    ASSERT_EQ(vm.getHostArrayLength(aref), 4u);
    EXPECT_TRUE(vm.getHostArrayValue(aref, 0).isInt());
    EXPECT_EQ(vm.getHostArrayValue(aref, 0).asInt(), 1);
    EXPECT_TRUE(vm.getHostArrayValue(aref, 1).isBool());
    Value third = vm.getHostArrayValue(aref, 2);
    EXPECT_EQ(vm.resolveStringKey(third), "x");
    Value nested = vm.getHostArrayValue(aref, 3);
    ASSERT_TRUE(nested.isArrayId());
    auto nref = ArrayRef{nested.asArrayId()};
    ASSERT_EQ(vm.getHostArrayLength(nref), 1u);
    EXPECT_EQ(vm.getHostArrayValue(nref, 0).asInt(), 2);

    havel_decref(outer);
}

TEST_F(CApiValueConversionTest, ArrayArgConvertsOut) {
    auto aref = vm.createHostArray();
    vm.pushHostArrayValue(aref, Value::makeInt(7));
    vm.pushHostArrayValue(aref, Value::makeDouble(0.25));

    HavelValue* c = valueToHavelValue(&vm, Value::makeArrayId(aref.id));
    ASSERT_EQ(havel_get_type(c), HAVEL_ARRAY);
    ASSERT_EQ(havel_array_length(c), 2u);
    EXPECT_EQ(havel_get_int(havel_array_get(c, 0)), 7);
    EXPECT_DOUBLE_EQ(havel_get_float(havel_array_get(c, 1)), 0.25);
    havel_decref(c);
}

TEST_F(CApiValueConversionTest, ObjectBothDirections) {
    // result direction: C object -> VM host object
    HavelValue* obj = havel_new_object();
    havel_object_set(obj, "a", havel_new_int(1));
    HavelValue* sval = havel_new_string("s");
    havel_object_set(obj, "b", sval);
    havel_decref(sval);

    Value ov = havelValueToValue(&vm, obj);
    ASSERT_TRUE(ov.isObjectId());
    auto oref = ObjectRef{ov.asObjectId(), true};
    auto keys = vm.getHostObjectKeys(oref);
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_EQ(vm.getHostObjectField(oref, "a").asInt(), 1);
    EXPECT_EQ(vm.resolveStringKey(vm.getHostObjectField(oref, "b")), "s");
    havel_decref(obj);

    // arg direction: plain VM object -> C object value
    auto voref = vm.createHostObject();
    vm.setHostObjectField(voref, "k", Value::makeInt(3));
    HavelValue* cout = valueToHavelValue(&vm, Value::makeObjectId(voref.id));
    ASSERT_EQ(havel_get_type(cout), HAVEL_OBJECT);
    EXPECT_EQ(havel_get_int(havel_object_get(cout, "k")), 3);
    havel_decref(cout);
}

TEST_F(CApiValueConversionTest, CycleDegradesToNullNotHang) {
    // A VM array containing itself must terminate via the depth limit.
    auto aref = vm.createHostArray();
    vm.pushHostArrayValue(aref, Value::makeArrayId(aref.id));

    HavelValue* c = valueToHavelValue(&vm, Value::makeArrayId(aref.id));
    ASSERT_EQ(havel_get_type(c), HAVEL_ARRAY);
    ASSERT_EQ(havel_array_length(c), 1u);
    HavelValue* elem = havel_array_get(c, 0);
    ASSERT_NE(elem, nullptr);
    // Somewhere down the nesting the depth guard turned the cycle into
    // nulls; the outermost element is either another array layer or the
    // terminal null.
    EXPECT_TRUE(havel_get_type(elem) == HAVEL_NULL ||
                havel_get_type(elem) == HAVEL_ARRAY);
    havel_decref(c);
}

TEST_F(CApiValueConversionTest, NullVMDegradesToPrimitives) {
    // Without a VM the conversions still handle primitives but degrade
    // everything that needs interning/allocation.
    HavelValue* s = havel_new_string("no vm");
    EXPECT_TRUE(havelValueToValue(nullptr, s).isNull());
    havel_decref(s);

    HavelValue* h = havel_new_handle(nullptr, nullptr);
    EXPECT_TRUE(havelValueToValue(nullptr, h).isNull());
    havel_decref(h);

    // Arg direction without a VM: strings/arrays degrade to null, ints
    // still convert.
    EXPECT_EQ(havel_get_type(valueToHavelValue(nullptr, Value::makeInt(5))),
              HAVEL_INT);
}

} // namespace
} // namespace havel::compiler
