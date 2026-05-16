#include "OptionModule.hpp"

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

void registerOptionModule(const VMApi &api) {
    uint32_t optionTypeId = api.registerEnumType("Option", {"Some", "None"});

    Value noneSingleton = api.makeEnum(optionTypeId, 1, {});
    api.setGlobal("None", noneSingleton);

    api.registerFunction("Some", 1, [api, optionTypeId](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("Some() requires 1 argument");
        return api.makeEnum(optionTypeId, 0, {args[0]});
    });

    api.registerFunction("Option.isSome", 1, [api, optionTypeId](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isEnumId())
            return Value::makeBool(false);
        if (args[0].asEnumTypeId() != optionTypeId)
            return Value::makeBool(false);
        uint32_t tag = api.getEnumTag(args[0]);
        return Value::makeBool(tag == 0);
    });

    api.registerFunction("Option.isNone", 1, [api, optionTypeId](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isEnumId())
            return Value::makeBool(false);
        if (args[0].asEnumTypeId() != optionTypeId)
            return Value::makeBool(false);
        uint32_t tag = api.getEnumTag(args[0]);
        return Value::makeBool(tag == 1);
    });

    api.registerFunction("Option.unwrap", 1, [api, optionTypeId](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isEnumId() || args[0].asEnumTypeId() != optionTypeId)
            throw std::runtime_error("Option.unwrap: not an Option value");
        uint32_t tag = api.getEnumTag(args[0]);
        if (tag == 1)
            throw std::runtime_error("Option.unwrap: called on None");
        return api.getEnumPayload(args[0], 0);
    });

    api.registerFunction("Option.unwrapOr", 2, [api, optionTypeId](const std::vector<Value> &args) {
        if (args.size() < 2)
            throw std::runtime_error("Option.unwrapOr requires 2 arguments");
        if (!args[0].isEnumId() || args[0].asEnumTypeId() != optionTypeId)
            return args[1];
        uint32_t tag = api.getEnumTag(args[0]);
        if (tag == 1)
            return args[1];
        return api.getEnumPayload(args[0], 0);
    });

    auto optObj = api.makeObject();
    api.setField(optObj, "isSome", api.makeFunctionRef("Option.isSome"));
    api.setField(optObj, "isNone", api.makeFunctionRef("Option.isNone"));
    api.setField(optObj, "unwrap", api.makeFunctionRef("Option.unwrap"));
    api.setField(optObj, "unwrapOr", api.makeFunctionRef("Option.unwrapOr"));
    api.setField(optObj, "Some", api.makeFunctionRef("Some"));
    api.setField(optObj, "None", noneSingleton);
    api.setGlobal("Option", optObj);
}

} // namespace havel::stdlib
