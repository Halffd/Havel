#include "StringModule.hpp"
#include "../compiler/vm/VM.hpp"
#include "../compiler/prototypes/PrototypeRegistry.hpp"
#include <regex>

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

    api.registerFunction("string.chr", [api](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("string.chr() requires 1 argument");
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

    struct CursorData {
        std::string str;
        std::vector<int64_t> codepoints;
        std::vector<size_t> byteOffsets;
        std::vector<int> byteLens;
        size_t idx = 0;
    };

    api.registerFunction("string._cursor", [api](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("string._cursor() requires 1 argument");
        const std::string* strPtr = api.getStringPtr(args[0]);
        std::string tempStr;
        const std::string& str = strPtr ? *strPtr : (tempStr = api.toString(args[0]));

        auto cursorObj = api.makeObject();
        auto data = new CursorData();
        data->str = str;

        size_t i = 0;
        while (i < str.size()) {
            unsigned char c = static_cast<unsigned char>(str[i]);
            int64_t cp;
            int byteLen = 1;
            if (c < 0x80) {
                cp = static_cast<int64_t>(c);
                byteLen = 1;
            } else if ((c & 0xE0) == 0xC0) {
                cp = static_cast<int64_t>(c & 0x1F);
                if (i + 1 < str.size()) cp = (cp << 6) | (static_cast<unsigned char>(str[i+1]) & 0x3F);
                byteLen = 2;
            } else if ((c & 0xF0) == 0xE0) {
                cp = static_cast<int64_t>(c & 0x0F);
                if (i + 1 < str.size()) cp = (cp << 6) | (static_cast<unsigned char>(str[i+1]) & 0x3F);
                if (i + 2 < str.size()) cp = (cp << 6) | (static_cast<unsigned char>(str[i+2]) & 0x3F);
                byteLen = 3;
            } else if ((c & 0xF8) == 0xF0) {
                cp = static_cast<int64_t>(c & 0x07);
                if (i + 1 < str.size()) cp = (cp << 6) | (static_cast<unsigned char>(str[i+1]) & 0x3F);
                if (i + 2 < str.size()) cp = (cp << 6) | (static_cast<unsigned char>(str[i+2]) & 0x3F);
                if (i + 3 < str.size()) cp = (cp << 6) | (static_cast<unsigned char>(str[i+3]) & 0x3F);
                byteLen = 4;
            } else {
                cp = static_cast<int64_t>(c);
                byteLen = 1;
            }
            data->codepoints.push_back(cp);
            data->byteOffsets.push_back(i);
            data->byteLens.push_back(byteLen);
            i += byteLen;
        }

        api.vm().setHostObjectNativeData(cursorObj.asObjectId(), data, [](void* ptr) { delete static_cast<CursorData*>(ptr); });
        api.setField(cursorObj, "str", args[0]);
        api.setField(cursorObj, "idx", Value::makeInt(0));
        return cursorObj;
    });

    api.registerFunction("string._cursor_cp", [api](const std::vector<Value> &args) {
        if (args.size() < 2)
            throw std::runtime_error("string._cursor_cp() requires cursor and offset");
        auto cursorObj = args[0];
        int64_t offset = args[1].isInt() ? args[1].asInt() : 0;
        auto* data = static_cast<CursorData*>(api.vm().getHostObjectNativeData(cursorObj.asObjectId()));
        if (!data) throw std::runtime_error("invalid cursor");
        int64_t idx = data->idx + offset;
        if (idx >= 0 && idx < (int64_t)data->codepoints.size()) {
            return Value::makeInt(data->codepoints[idx]);
        }
        return Value::makeInt(-1);
    });

    api.registerFunction("string._cursor_current", [api](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("string._cursor_current() requires cursor");
        auto cursorObj = args[0];
        auto* data = static_cast<CursorData*>(api.vm().getHostObjectNativeData(cursorObj.asObjectId()));
        if (!data) throw std::runtime_error("invalid cursor");
        if (data->idx < data->codepoints.size()) {
            return Value::makeInt(data->codepoints[data->idx]);
        }
        return Value::makeInt(-1);
    });

    api.registerFunction("string._cursor_advance", [api](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("string._cursor_advance() requires cursor");
        auto cursorObj = args[0];
        auto* data = static_cast<CursorData*>(api.vm().getHostObjectNativeData(cursorObj.asObjectId()));
        if (!data) throw std::runtime_error("invalid cursor");
        if (data->idx >= data->codepoints.size()) {
            return Value::makeInt(-1);
        }
        int64_t cp = data->codepoints[data->idx];
        data->idx++;
        return Value::makeInt(cp);
    });

    api.registerFunction("string._cursor_bytePos", [api](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("string._cursor_bytePos() requires cursor");
        auto cursorObj = args[0];
        auto* data = static_cast<CursorData*>(api.vm().getHostObjectNativeData(cursorObj.asObjectId()));
        if (!data) throw std::runtime_error("invalid cursor");
        if (data->idx < data->byteOffsets.size()) {
            return Value::makeInt(static_cast<int64_t>(data->byteOffsets[data->idx]));
        }
        return Value::makeInt(static_cast<int64_t>(data->str.size()));
    });

    api.registerFunction("string._cursor_byteLen", [api](const std::vector<Value> &args) {
        if (args.size() < 2)
            throw std::runtime_error("string._cursor_byteLen() requires cursor and index");
        auto cursorObj = args[0];
        int64_t idx = args[1].isInt() ? args[1].asInt() : 0;
        auto* data = static_cast<CursorData*>(api.vm().getHostObjectNativeData(cursorObj.asObjectId()));
        if (!data) throw std::runtime_error("invalid cursor");
        if (idx >= 0 && idx < (int64_t)data->byteLens.size()) {
            return Value::makeInt(data->byteLens[idx]);
        }
        return Value::makeInt(1);
    });

    api.registerFunction("string._cursor_len", [api](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("string._cursor_len() requires cursor");
        auto cursorObj = args[0];
        auto* data = static_cast<CursorData*>(api.vm().getHostObjectNativeData(cursorObj.asObjectId()));
        if (!data) throw std::runtime_error("invalid cursor");
        return Value::makeInt(static_cast<int64_t>(data->codepoints.size()));
    });

    api.registerFunction("string._cursor_setIdx", [api](const std::vector<Value> &args) {
        if (args.size() < 2)
            throw std::runtime_error("string._cursor_setIdx() requires cursor and index");
        auto cursorObj = args[0];
        int64_t idx = args[1].isInt() ? args[1].asInt() : 0;
        auto* data = static_cast<CursorData*>(api.vm().getHostObjectNativeData(cursorObj.asObjectId()));
        if (!data) throw std::runtime_error("invalid cursor");
        if (idx < 0) idx = 0;
        if (idx > (int64_t)data->codepoints.size()) idx = data->codepoints.size();
        data->idx = idx;
        return Value::makeInt(idx);
    });

    api.registerFunction("string._cursor_idxAtByte", [api](const std::vector<Value> &args) {
        if (args.size() < 2)
            throw std::runtime_error("string._cursor_idxAtByte() requires cursor and bytePos");
        auto cursorObj = args[0];
        int64_t bp = args[1].isInt() ? args[1].asInt() : 0;
        auto* data = static_cast<CursorData*>(api.vm().getHostObjectNativeData(cursorObj.asObjectId()));
        if (!data) throw std::runtime_error("invalid cursor");
        size_t lo = 0;
        size_t hi = data->byteOffsets.size();
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            if (data->byteOffsets[mid] < (size_t)bp) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return Value::makeInt(static_cast<int64_t>(lo));
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
    api.setField(strObj, "cursor", api.makeFunctionRef("string._cursor"));
    api.setField(strObj, "cp", api.makeFunctionRef("string._cursor_cp"));
    api.setField(strObj, "current", api.makeFunctionRef("string._cursor_current"));
    api.setField(strObj, "advance", api.makeFunctionRef("string._cursor_advance"));
    api.setField(strObj, "bytePos", api.makeFunctionRef("string._cursor_bytePos"));
    api.setField(strObj, "byteLen", api.makeFunctionRef("string._cursor_byteLen"));
    api.setField(strObj, "len", api.makeFunctionRef("string._cursor_len"));
    api.setField(strObj, "setIdx", api.makeFunctionRef("string._cursor_setIdx"));
    api.setField(strObj, "idxAtByte", api.makeFunctionRef("string._cursor_idxAtByte"));
    api.setGlobal("string", strObj);
    api.setGlobal("String", strObj);

    if (!api.vm().hasHostFunction("string.len")) {
        compiler::prototypes::registerStringPrototype(api.vm());
    }
    finalizeStringNamespace(api);
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
        exports = vm.loadModule("string");
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
