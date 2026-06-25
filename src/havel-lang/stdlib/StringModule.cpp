#include "StringModule.hpp"
#include "../compiler/vm/VM.hpp"
#include <regex>
#include <iostream>

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

void registerStringModule(const VMApi &api) {
    api.registerFunction("string._fromCodePoint", [api](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("string._fromCodePoint() requires 1 argument");
        int64_t cp = args[0].isInt() ? args[0].asInt() : 0;
        std::string result;
        if (cp < 0) return api.makeString("");
        if (cp < 0x80) {
            result += static_cast<char>(cp);
        } else if (cp < 0x800) {
            result += static_cast<char>(0xC0 | (cp >> 6));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            result += static_cast<char>(0xE0 | (cp >> 12));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x110000) {
            result += static_cast<char>(0xF0 | (cp >> 18));
            result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
        return api.makeString(result);
    });

    api.registerFunction("string._codePointLen", [api](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("string._codePointLen() requires 1 argument");
        const std::string* strPtr = api.getStringPtr(args[0]);
        std::string tempStr;
        const std::string& str = strPtr ? *strPtr : (tempStr = api.toString(args[0]));
        int64_t count = 0;
        size_t bytePos = 0;
        while (bytePos < str.size()) {
            unsigned char c = static_cast<unsigned char>(str[bytePos]);
            if (c < 0x80) bytePos += 1;
            else if ((c & 0xE0) == 0xC0) bytePos += 2;
            else if ((c & 0xF0) == 0xE0) bytePos += 3;
            else if ((c & 0xF8) == 0xF0) bytePos += 4;
            else bytePos += 1;
            count++;
        }
        return Value::makeInt(count);
    });

    api.registerFunction("string._toCodePointArray", [api](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("string._toCodePointArray() requires 1 argument");
        std::string s = api.toString(args[0]);
        auto arr = api.makeArray();
        size_t i = 0;
        while (i < s.size()) {
            auto cpArr = api.makeArray();
            unsigned char c = static_cast<unsigned char>(s[i]);
            int64_t cp;
            if (c < 0x80) {
                cp = static_cast<int64_t>(c);
                api.push(cpArr, Value(cp));
                api.push(cpArr, Value(static_cast<int64_t>(i)));
                api.push(cpArr, Value(static_cast<int64_t>(1)));
                i += 1;
            } else if ((c & 0xE0) == 0xC0) {
                cp = static_cast<int64_t>(c & 0x1F);
                if (i + 1 < s.size()) cp = (cp << 6) | (static_cast<unsigned char>(s[i+1]) & 0x3F);
                api.push(cpArr, Value(cp));
                api.push(cpArr, Value(static_cast<int64_t>(i)));
                api.push(cpArr, Value(static_cast<int64_t>(2)));
                i += 2;
            } else if ((c & 0xF0) == 0xE0) {
                cp = static_cast<int64_t>(c & 0x0F);
                if (i + 1 < s.size()) cp = (cp << 6) | (static_cast<unsigned char>(s[i+1]) & 0x3F);
                if (i + 2 < s.size()) cp = (cp << 6) | (static_cast<unsigned char>(s[i+2]) & 0x3F);
                api.push(cpArr, Value(cp));
                api.push(cpArr, Value(static_cast<int64_t>(i)));
                api.push(cpArr, Value(static_cast<int64_t>(3)));
                i += 3;
            } else if ((c & 0xF8) == 0xF0) {
                cp = static_cast<int64_t>(c & 0x07);
                if (i + 1 < s.size()) cp = (cp << 6) | (static_cast<unsigned char>(s[i+1]) & 0x3F);
                if (i + 2 < s.size()) cp = (cp << 6) | (static_cast<unsigned char>(s[i+2]) & 0x3F);
                if (i + 3 < s.size()) cp = (cp << 6) | (static_cast<unsigned char>(s[i+3]) & 0x3F);
                api.push(cpArr, Value(cp));
                api.push(cpArr, Value(static_cast<int64_t>(i)));
                api.push(cpArr, Value(static_cast<int64_t>(4)));
                i += 4;
            } else {
                cp = static_cast<int64_t>(c);
                api.push(cpArr, Value(cp));
                api.push(cpArr, Value(static_cast<int64_t>(i)));
                api.push(cpArr, Value(static_cast<int64_t>(1)));
                i += 1;
            }
            api.push(arr, cpArr);
        }
        return arr;
    });

    api.registerFunction("string._regexReplace", [api](const std::vector<Value>& args) {
        if (args.size() < 3)
            throw std::runtime_error("string._regexReplace() requires string, pattern, and replacement");
        std::string s = api.toString(args[0]);
        std::string pattern = api.toString(args[1]);
        std::string replacement = api.toString(args[2]);
        try {
            std::regex re(pattern);
            std::string result = std::regex_replace(s, re, replacement);
            return api.makeString(std::move(result));
        } catch (const std::regex_error&) {
            return Value::makeNull();
        }
    });

    api.registerFunction("replace", [api](const std::vector<Value>& args) {
        std::cerr << "[DBG-replace] argc=" << args.size();
        for (size_t i = 0; i < args.size() && i < 5; ++i) {
            std::cerr << " args[" << i << "]=";
            if (args[i].isStringValId()) std::cerr << "StringValId:" << args[i].asStringValId();
            else if (args[i].isStringId()) std::cerr << "StringId:" << args[i].asStringId();
            else if (args[i].isInt()) std::cerr << "int:" << args[i].asInt();
            else if (args[i].isNull()) std::cerr << "null";
            else std::cerr << "other";
        }
        std::cerr << std::endl;
        if (args.size() < 3)
            throw std::runtime_error("replace() requires string, pattern, and replacement");
        std::string s = api.toString(args[0]);
        std::string pattern = api.toString(args[1]);
        std::string replacement = api.toString(args[2]);
        bool isRegex = !pattern.empty() && pattern.front() == '/' && pattern.size() > 2 && pattern.back() == '/';
        if (isRegex) {
            std::string regexPattern = pattern.substr(1, pattern.size() - 2);
            try {
                std::regex re(regexPattern);
                std::string result = std::regex_replace(s, re, replacement);
                return api.makeString(std::move(result));
            } catch (const std::regex_error&) {
                return Value::makeNull();
            }
        }
        size_t pos = s.find(pattern);
        if (pos == std::string::npos) return api.makeString(s);
        s.replace(pos, pattern.size(), replacement);
        return api.makeString(std::move(s));
    });

    api.registerFunction("string.join", [api](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("string.join() requires at least 1 argument");
        if (!args[0].isArrayId())
            throw std::runtime_error("string.join() first argument must be array");
        std::string delim = (args.size() > 1) ? api.toString(args[1]) : "";
        Value arr = args[0];
        uint32_t len = api.length(arr);
        std::string result;
        for (uint32_t i = 0; i < len; ++i) {
            if (i > 0) result += delim;
            result += api.toString(api.getAt(arr, i));
        }
        return api.makeString(std::move(result));
    });

    auto strObj = api.makeObject();
    api.setField(strObj, "_fromCodePoint", api.makeFunctionRef("string._fromCodePoint"));
    api.setField(strObj, "_codePointLen", api.makeFunctionRef("string._codePointLen"));
    api.setField(strObj, "_toCodePointArray", api.makeFunctionRef("string._toCodePointArray"));
    api.setField(strObj, "_regexReplace", api.makeFunctionRef("string._regexReplace"));
    api.setField(strObj, "fromCodePoint", api.makeFunctionRef("string._fromCodePoint"));
    api.setField(strObj, "codePointLen", api.makeFunctionRef("string._codePointLen"));
    api.setField(strObj, "toCodePointArray", api.makeFunctionRef("string._toCodePointArray"));
    api.setGlobal("string", strObj);
    api.setGlobal("String", strObj);
}

void finalizeStringNamespace(const VMApi &api) {
    auto &vm = api.vm();
    auto it = vm.getGlobals().find("string");
    if (it == vm.getGlobals().end()) return;
    Value strObj = it->second;
    if (!strObj.isObjectId()) return;

    api.setField(strObj, "len", api.makeFunctionRef("string.len"));
    api.setField(strObj, "lower", api.makeFunctionRef("string.lower"));
    api.setField(strObj, "upper", api.makeFunctionRef("string.upper"));
    api.setField(strObj, "trim", api.makeFunctionRef("string.trim"));
    api.setField(strObj, "sub", api.makeFunctionRef("string.sub"));
    api.setField(strObj, "find", api.makeFunctionRef("string.find"));
    api.setField(strObj, "replace", api.makeFunctionRef("string.replace"));
    api.setField(strObj, "split", api.makeFunctionRef("string.split"));
    api.setField(strObj, "join", api.makeFunctionRef("string.join"));
    api.setField(strObj, "includes", api.makeFunctionRef("string.includes"));
    api.setField(strObj, "startswith", api.makeFunctionRef("string.startsWith"));
    api.setField(strObj, "endswith", api.makeFunctionRef("string.endsWith"));
    api.setField(strObj, "startsWith", api.makeFunctionRef("string.startsWith"));
    api.setField(strObj, "endsWith", api.makeFunctionRef("string.endsWith"));
    api.setField(strObj, "codePointAt", api.makeFunctionRef("string.codePointAt"));
    api.setField(strObj, "cpAtByte", api.makeFunctionRef("string.cpAtByte"));
    api.setField(strObj, "cpByteLen", api.makeFunctionRef("string.cpByteLen"));

    Value exports;
    try {
        exports = vm.loadModule("string/string");
    } catch (...) {
    }

    if (exports.isObjectId()) {
        auto *obj = vm.getHeap().object(exports.asObjectId());
        if (obj) {
            for (const auto& [name, value] : *obj) {
                if (name.empty() || name[0] == '_') continue;
                api.setField(strObj, name, value);
            }
        }
    }
}

} // namespace havel::stdlib

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"
HAVEL_MODULE_PLUGIN_EAGER(string, "1.0.0", "String operations stdlib module",
    havel::stdlib::registerStringModule(*api);
)
#endif
