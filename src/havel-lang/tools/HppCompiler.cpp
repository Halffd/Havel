// havel-hppc - Havel++ prototype compiler (TODO2.md #16 / #26 item 13)
//
// Compiles a .hvp module to a native .so through C++ transpilation:
//
//   .hvp -> generated C++ (ModulePlugin.h boilerplate, Value<->native
//           wrappers, verbatim native bodies) -> clang++ -shared
//
// The generated .so exposes havel_module_info / havel_module_register (the
// native module ABI in src/c/ModulePlugin.h) and loads through the standard
// module loader. Havel reaches it with `use <module>` + dotted calls, the
// same convention every native module follows. The ABI sharing requirement
// (TODO2.md #13) holds by construction: the generated code registers
// through VMApi, not a private runtime.
//
// The .hvp subset (intentionally minimal - TODO2.md #16 defers the full
// syntax):
//
//   // module: <name> <version> "<description>"
//
//   export <type> <name>(<type> <param>, ...) {
//       <C++ body, verbatim>
//   }
//
// Types: int, double, string, void. Function bodies are C++ (Havel++ is
// the native layer; bodies pass through to the C++ compiler unmodified).

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct HppParam {
    std::string type;
    std::string name;
};

struct HppFunction {
    std::string ret;
    std::string name;
    std::vector<HppParam> params;
    std::string body;  // C++, verbatim between the braces
};

struct HppModule {
    std::string name;
    std::string version;
    std::string description;
    std::vector<HppFunction> functions;
};

bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}
bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

void skipWs(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
}

bool readIdent(const std::string& s, size_t& i, std::string& out) {
    skipWs(s, i);
    if (i >= s.size() || !isIdentStart(s[i])) return false;
    size_t start = i;
    while (i < s.size() && isIdentChar(s[i])) i++;
    out = s.substr(start, i - start);
    return true;
}

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> lines;
    std::istringstream in(s);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

// Parse the "// module: <name> <version> \"<desc>\"" header. Returns false
// when the header is absent (a module identity is required: the generated
// .so registers under this name).
bool parseModuleHeader(const std::string& source, HppModule& mod) {
    for (const auto& line : splitLines(source)) {
        size_t i = 0;
        skipWs(line, i);
        if (line.compare(i, 2, "//") != 0) continue;
        i += 2;
        skipWs(line, i);
        const std::string tag = "module:";
        if (line.compare(i, tag.size(), tag) != 0) continue;
        i += tag.size();
        if (!readIdent(line, i, mod.name)) return false;
        // Version: bare token (1.0.0 - dots are not ident chars, read raw)
        skipWs(line, i);
        size_t vstart = i;
        while (i < line.size() && (isIdentChar(line[i]) || line[i] == '.')) i++;
        mod.version = line.substr(vstart, i - vstart);
        // Description: quoted string, optional
        skipWs(line, i);
        if (i < line.size() && line[i] == '"') {
            size_t dstart = ++i;
            while (i < line.size() && line[i] != '"') i++;
            if (i < line.size()) {
                mod.description = line.substr(dstart, i - dstart);
            }
        }
        return true;
    }
    return false;
}

// Parse `export <type> <name>(<params>) {` and the brace-counted body.
// The body passes through verbatim: Havel++ bodies are C++.
bool parseFunction(const std::string& source, size_t& i, HppFunction& fn) {
    std::string kw;
    if (!readIdent(source, i, kw) || kw != "export") return false;
    if (!readIdent(source, i, fn.ret)) return false;
    if (!readIdent(source, i, fn.name)) return false;
    skipWs(source, i);
    if (i >= source.size() || source[i] != '(') return false;
    i++;  // consume '('

    // Params: `type name` pairs, comma separated, until ')'
    while (i < source.size() && source[i] != ')') {
        HppParam p;
        std::string junk;
        if (!readIdent(source, i, p.type)) return false;
        // Optional native qualifiers before the name (const, *): the
        // prototype keeps params primitive; accept and skip them.
        skipWs(source, i);
        while (i < source.size() && (source[i] == '*' || source[i] == '&')) i++;
        if (!readIdent(source, i, p.name)) return false;
        fn.params.push_back(p);
        skipWs(source, i);
        if (i < source.size() && source[i] == ',') i++;
    }
    if (i >= source.size() || source[i] != ')') return false;
    i++;  // consume ')'

    skipWs(source, i);
    if (i >= source.size() || source[i] != '{') return false;
    i++;  // consume '{'

    // Body: verbatim until the matching '}' (brace counting; strings and
    // comments inside the body are C++ and counted naively - the prototype
    // bodies are simple native code).
    int depth = 1;
    size_t bodyStart = i;
    while (i < source.size() && depth > 0) {
        char c = source[i];
        if (c == '{') depth++;
        else if (c == '}') depth--;
        if (depth > 0) i++;
    }
    if (depth != 0) return false;  // unclosed body
    fn.body = source.substr(bodyStart, i - bodyStart);
    i++;  // consume the closing '}'
    return true;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}

// The C++ type for a .hvp primitive in a RETURN position. Strings are by
// value: bodies return temporaries (`return out + name;`), and a
// const-ref return would dangle the moment the full expression ends.
std::string nativeReturnType(const std::string& t) {
    if (t == "int") return "int64_t";
    if (t == "double") return "double";
    if (t == "string") return "std::string";
    return t;  // void passes through
}

// The C++ type for a .hvp primitive in a PARAMETER position. Strings bind
// by const ref: the wrapper materializes them (toString) for the call.
std::string nativeParamType(const std::string& t) {
    if (t == "int") return "int64_t";
    if (t == "double") return "double";
    if (t == "string") return "const std::string&";
    return t;  // void passes through
}

// The argument read expression inside a wrapper (api is the captured
// VMApi copy; strings resolve through the VM's heap).
std::string argRead(const HppParam& p, size_t idx) {
    if (p.type == "int") return "args[" + std::to_string(idx) + "].asInt()";
    if (p.type == "double") return "args[" + std::to_string(idx) + "].asDouble()";
    if (p.type == "string")
        return "hpp_api.toString(args[" + std::to_string(idx) + "])";
    return "args[" + std::to_string(idx) + "]";
}

// The wrapper body: typed params -> impl -> Value.
std::string wrapperBody(const HppFunction& fn, const std::string& implName) {
    std::string callArgs;
    for (size_t i = 0; i < fn.params.size(); ++i) {
        if (i) callArgs += ", ";
        callArgs += argRead(fn.params[i], i);
    }
    std::string call = implName + "(" + callArgs + ")";
    if (fn.ret == "void") return "    " + call + ";\n    return havel::compiler::Value::makeNull();\n";
    if (fn.ret == "string")
        return "    return hpp_api.makeString(" + call + ");\n";
    if (fn.ret == "double")
        return "    return havel::compiler::Value::makeDouble(" + call + ");\n";
    return "    return havel::compiler::Value::makeInt(" + call + ");\n";
}

std::string generateCpp(const HppModule& mod) {
    std::ostringstream out;
    out << "// Generated by havel-hppc from " << mod.name << ".hvp - DO NOT EDIT.\n";
    out << "// Havel++ native module: the bodies below are the .hvp functions,\n";
    out << "// transpiled 1:1; the wrappers bridge havel::compiler::Value to the\n";
    out << "// native types and register through VMApi (the module ABI).\n";
    out << "#define HAVEL_MODULE_PLUGIN 1\n";
    out << "#include \"c/ModulePlugin.h\"\n";
    out << "#include \"havel-lang/compiler/vm/VMApi.hpp\"\n";
    out << "\n#include <cstdint>\n#include <string>\n#include <vector>\n\n";
    out << "using havel::compiler::Value;\n\n";

    // Native implementations, bodies verbatim.
    out << "// ==== native implementations (Havel++ bodies, verbatim) ====\n";
    for (const auto& fn : mod.functions) {
        out << "static " << nativeReturnType(fn.ret) << " hpp_" << fn.name << "(";
        for (size_t i = 0; i < fn.params.size(); ++i) {
            if (i) out << ", ";
            out << nativeParamType(fn.params[i].type) << " " << fn.params[i].name;
        }
        out << ") {\n" << fn.body << "\n}\n\n";
    }

    // Plugin: registration through VMApi.
    out << "HAVEL_MODULE_PLUGIN_IMPL(" << mod.name << ", \"" << mod.version
        << "\", \"" << mod.description << "\",\n";
    for (const auto& fn : mod.functions) {
        (void)fn;
    }
    out << "    auto hpp_api = *api;   // copy: the vm/registry pointers outlive register_fn\n";
    for (const auto& fn : mod.functions) {
        out << "    api->registerFunction(\"" << mod.name << "." << fn.name
            << "\", [hpp_api](const std::vector<Value>& args) -> Value {\n"
            << wrapperBody(fn, "hpp_" + fn.name)
            << "    });\n";
    }
    out << "    auto hpp_obj = api->makeObject();\n";
    out << "    api->setGlobal(\"" << mod.name << "\", hpp_obj);\n";
    out << "    api->setField(hpp_obj, \"__" << mod.name << "_module\", Value::makeBool(true));\n";
    for (const auto& fn : mod.functions) {
        out << "    api->setField(hpp_obj, \"" << fn.name
            << "\", api->makeFunctionRef(\"" << mod.name << "." << fn.name << "\"));\n";
    }
    out << ")\n";
    return out.str();
}

}  // namespace

int main(int argc, char** argv) {
    std::string input;
    std::string output;
    std::string buildId = "0";
    std::string cxx = "clang++";
    std::vector<std::string> includeDirs;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            output = argv[++i];
        } else if (arg == "--build-id" && i + 1 < argc) {
            buildId = argv[++i];
        } else if (arg == "--cxx" && i + 1 < argc) {
            cxx = argv[++i];
        } else if (arg == "-I" && i + 1 < argc) {
            includeDirs.push_back(argv[++i]);
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "havel-hppc: unknown option " << arg << "\n";
            return 2;
        } else {
            input = arg;
        }
    }
    if (input.empty() || output.empty()) {
        std::cerr << "usage: havel-hppc <module.hvp> -o <output.so> "
                     "[--build-id 0x<id>] [--cxx clang++] [-I <dir>]\n";
        return 2;
    }

    std::ifstream in(input, std::ios::binary);
    if (!in) {
        std::cerr << "havel-hppc: cannot open " << input << "\n";
        return 1;
    }
    std::string source((std::istreambuf_iterator<char>(in)), {});

    HppModule mod;
    if (!parseModuleHeader(source, mod) || mod.name.empty()) {
        std::cerr << "havel-hppc: missing '// module: <name> <version> \"<desc>\"' header\n";
        return 1;
    }

    size_t i = 0;
    while (i < source.size()) {
        skipWs(source, i);
        if (i >= source.size()) break;
        // Skip comments and newlines between declarations.
        if (source.compare(i, 2, "//") == 0) {
            while (i < source.size() && source[i] != '\n') i++;
            continue;
        }
        if (source[i] == '\n') { i++; continue; }
        HppFunction fn;
        if (!parseFunction(source, i, fn)) {
            std::cerr << "havel-hppc: parse error at byte " << i
                      << " (expected 'export <type> <name>(<params>) { ... }')\n";
            return 1;
        }
        mod.functions.push_back(fn);
    }

    if (mod.functions.empty()) {
        std::cerr << "havel-hppc: no exported functions in " << input << "\n";
        return 1;
    }

    const std::string cppPath = output + ".cpp";
    {
        std::ofstream cppFile(cppPath, std::ios::binary);
        if (!cppFile) {
            std::cerr << "havel-hppc: cannot write " << cppPath << "\n";
            return 1;
        }
        cppFile << generateCpp(mod);
    }

    std::string cmd = cxx + " -shared -fPIC -std=c++23 -fno-lto";
    cmd += " -DHAVEL_MODULE_BUILD_ID=" + buildId;
    for (const auto& dir : includeDirs) {
        cmd += " -I" + dir;
    }
    cmd += " -Wl,--allow-shlib-undefined";
    cmd += " \"" + cppPath + "\" -o \"" + output + "\"";

    const int rc = std::system(cmd.c_str());
    if (rc != 0) {
        std::cerr << "havel-hppc: C++ compilation failed (" << rc << ")\n";
        return 1;
    }
    std::cout << "havel-hppc: " << mod.name << " -> " << output << " ("
              << mod.functions.size() << " exported functions)\n";
    return 0;
}
