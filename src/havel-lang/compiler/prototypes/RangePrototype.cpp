#include "PrototypeRegistry.hpp"

namespace havel::compiler::prototypes {

void registerRangePrototype(VM& vm) {
    auto regProto = [&vm](const std::string& method, size_t arity, BytecodeHostFunction fn) {
        vm.registerHostFunction("range." + method, arity, std::move(fn));
        vm.registerPrototypeMethodByName("range", method, "range." + method);
    };

    regProto("has", 2, [&vm](const std::vector<Value>& args) {
        if (args.size() < 2) return Value::makeBool(false);
        if (!args[0].isRangeId()) return Value::makeBool(false);
        auto* r = vm.getHeap().range(args[0].asRangeId());
        if (!r) return Value::makeBool(false);
        int64_t value = args[1].isInt() ? args[1].asInt()
            : (args[1].isDouble() ? static_cast<int64_t>(args[1].asDouble()) : 0);
        if (r->step > 0) {
            return Value::makeBool(value >= r->start && value < r->end &&
                (value - r->start) % r->step == 0);
        } else if (r->step < 0) {
            return Value::makeBool(value <= r->start && value > r->end &&
                (value - r->start) % r->step == 0);
        }
        return Value::makeBool(false);
    });

    regProto("len", 1, [&vm](const std::vector<Value>& args) {
        if (args.empty()) return Value::makeInt(0);
        if (!args[0].isRangeId()) return Value::makeInt(0);
        auto* r = vm.getHeap().range(args[0].asRangeId());
        if (!r) return Value::makeInt(0);
        if (r->step == 0) return Value::makeInt(0);
        int64_t count = (r->end - r->start) / r->step;
        if (count < 0) count = 0;
        return Value::makeInt(count);
    });

    regProto("start", 1, [&vm](const std::vector<Value>& args) {
        if (args.empty()) return Value::makeNull();
        if (!args[0].isRangeId()) return Value::makeNull();
        auto* r = vm.getHeap().range(args[0].asRangeId());
        return r ? Value::makeInt(r->start) : Value::makeNull();
    });

    regProto("end", 1, [&vm](const std::vector<Value>& args) {
        if (args.empty()) return Value::makeNull();
        if (!args[0].isRangeId()) return Value::makeNull();
        auto* r = vm.getHeap().range(args[0].asRangeId());
        return r ? Value::makeInt(r->end) : Value::makeNull();
    });

    regProto("step", 1, [&vm](const std::vector<Value>& args) {
        if (args.empty()) return Value::makeNull();
        if (!args[0].isRangeId()) return Value::makeNull();
        auto* r = vm.getHeap().range(args[0].asRangeId());
        return r ? Value::makeInt(r->step) : Value::makeNull();
    });

    regProto("toArray", 1, [&vm](const std::vector<Value>& args) {
        if (args.empty()) return Value::makeNull();
        if (!args[0].isRangeId()) return Value::makeNull();
        auto* r = vm.getHeap().range(args[0].asRangeId());
        if (!r || r->step == 0) return Value::makeNull();
        auto resultRef = vm.getHeap().allocateArray();
        auto* result = vm.getHeap().array(resultRef.id);
        if (r->step > 0) {
            for (int64_t i = r->start; i < r->end; i += r->step) {
                result->push_back(Value::makeInt(i));
            }
        } else {
            for (int64_t i = r->start; i > r->end; i += r->step) {
                result->push_back(Value::makeInt(i));
            }
        }
        return Value::makeArrayId(resultRef.id);
    });
}

} // namespace havel::compiler::prototypes
