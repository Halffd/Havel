/*
 * LuaModule.cpp - optional Lua 5.4 bridge, exposed as lua.*
 *
 * Links liblua only when this module is built and loaded; havel_core and
 * havel_lang never reference Lua - the interpreter is reached exclusively
 * through this dlopen'd module, mirroring the seam used for CPython
 * (PythonModule.cpp) and Qt.
 *
 * Model: one lua_State per lua.load()/lua.eval() environment. Values cross
 * the boundary by conversion only - scalars, strings, arrays and objects
 * convert; anything else is rejected rather than wrapped, because Lua has
 * no opaque-object problem worth a handle registry (userdata stays inside
 * Lua and is worked on from Lua code).
 *
 *   bot = lua.load("game_bot.lua")   -> environment object; every global
 *                                       function materialized as a callable
 *                                       field, so bot.start_automation(x)
 *                                       calls the Lua global directly.
 *   lua.eval("return 1+1")           -> 2
 *   lua.version() / lua.available()
 */

#include "c/ModulePlugin.h"

#ifdef HAVEL_MODULE_PLUGIN

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include "havel-lang/compiler/vm/VMApi.hpp"
#include "utils/Logger.hpp"

#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel::modules::lua {

using compiler::Value;
using compiler::VMApi;

namespace {

// ---------------------------------------------------------------------------
// Environment registry: lua.load()/lua.eval() create a lua_State; the id is
// embedded in the returned environment object so later calls can find it.
// States are released on lua.release or at process shutdown.
// ---------------------------------------------------------------------------

std::unordered_map<int64_t, lua_State *> g_states;
// Registry reference to the table a chunk returned (Lua files commonly end
// with `return M`); the functions on it are called ON the table, not as
// globals.
std::unordered_map<int64_t, int> g_returned_refs;
int64_t g_next_state_id = 1;

lua_State *stateFromArg(const VMApi &api, const Value &v) {
  if (v.isInt()) {
    auto it = g_states.find(v.asInt());
    return it != g_states.end() ? it->second : nullptr;
  }
  if (v.isObjectId()) {
    auto idVal = api.getField(v, "__lua_id");
    if (idVal.isInt()) {
      auto it = g_states.find(idVal.asInt());
      return it != g_states.end() ? it->second : nullptr;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Value conversion: Havel -> Lua
// ---------------------------------------------------------------------------

void pushHavel(const VMApi &api, lua_State *L, const Value &v);

void pushFromArray(const VMApi &api, lua_State *L, const Value &arr) {
  uint32_t n = api.length(arr);
  lua_createtable(L, static_cast<int>(n), 0);
  for (uint32_t i = 0; i < n; ++i) {
    pushHavel(api, L, api.getAt(arr, i));
    lua_rawseti(L, -2, static_cast<lua_Integer>(i) + 1);
  }
}

void pushFromObject(const VMApi &api, lua_State *L, const Value &obj) {
  lua_createtable(L, 0, 0);
  auto keys = api.getObjectKeys(obj);
  for (const auto &k : keys) {
    lua_pushstring(L, k.c_str());
    pushHavel(api, L, api.getField(obj, k));
    lua_rawset(L, -3);
  }
}

void pushHavel(const VMApi &api, lua_State *L, const Value &v) {
  if (v.isNull()) {
    lua_pushnil(L);
  } else if (v.isBool()) {
    lua_pushboolean(L, v.asBool() ? 1 : 0);
  } else if (v.isInt()) {
    lua_pushinteger(L, static_cast<lua_Integer>(v.asInt()));
  } else if (v.isDouble()) {
    lua_pushnumber(L, v.asDouble());
  } else if (v.isStringId() || v.isStringValId()) {
    lua_pushstring(L, api.toString(v).c_str());
  } else if (v.isArrayId()) {
    pushFromArray(api, L, v);
  } else if (v.isObjectId()) {
    pushFromObject(api, L, v);
  } else {
    lua_pushnil(L);
  }
}

// ---------------------------------------------------------------------------
// Value conversion: Lua -> Havel
// ---------------------------------------------------------------------------

Value toHavel(const VMApi &api, lua_State *L, int idx, int depth = 0);

Value toHavelFromTable(const VMApi &api, lua_State *L, int idx, int depth) {
  // Sequences (1..n with no other keys) become arrays; mixed tables become
  // objects with integer keys stringified.
  const int kMaxDepth = 8;
  if (depth > kMaxDepth)
    return Value::makeNull();

  // First pass: length test (lua_geti returns LUA_TNIL for holes).
  bool sequential = true;
  lua_Integer n = 0;
  while (true) {
    lua_pushinteger(L, n + 1);
    lua_rawget(L, idx < 0 ? idx - 1 : idx);
    bool present = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (!present)
      break;
    ++n;
    if (n > 1000000) { // paranoia cap
      sequential = false;
      break;
    }
  }
  // Second pass: any non-sequence key?
  if (sequential) {
    lua_pushnil(L);
    while (lua_next(L, idx < 0 ? idx - 1 : idx) != 0) {
      if (lua_type(L, -2) != LUA_TNUMBER) {
        sequential = false;
        // leave key on stack for lua_next termination
        lua_pop(L, 1);
        continue;
      }
      lua_Integer k = lua_tointeger(L, -2);
      if (k < 1 || k > n) {
        sequential = false;
        lua_pop(L, 1);
        continue;
      }
      lua_pop(L, 1);
    }
  }
  if (sequential) {
    auto arr = api.makeArray();
    for (lua_Integer i = 1; i <= n; ++i) {
      lua_pushinteger(L, i);
      lua_rawget(L, idx < 0 ? idx - 1 : idx);
      api.push(arr, toHavel(api, L, -1, depth + 1));
      lua_pop(L, 1);
    }
    return arr;
  }
  auto obj = api.makeObject();
  lua_pushnil(L);
  while (lua_next(L, idx < 0 ? idx - 1 : idx) != 0) {
    std::string key;
    if (lua_type(L, -2) == LUA_TSTRING) {
      key = lua_tostring(L, -2);
    } else if (lua_type(L, -2) == LUA_TNUMBER) {
      key = std::to_string(lua_tointeger(L, -2));
    } else {
      lua_pop(L, 1);
      continue;
    }
    api.setField(obj, key, toHavel(api, L, -1, depth + 1));
    lua_pop(L, 1);
  }
  return obj;
}

Value toHavel(const VMApi &api, lua_State *L, int idx, int depth) {
  switch (lua_type(L, idx)) {
  case LUA_TNIL:
    return Value::makeNull();
  case LUA_TBOOLEAN:
    return Value::makeBool(lua_toboolean(L, idx) != 0);
  case LUA_TNUMBER: {
    lua_Number d = lua_tonumber(L, idx);
    lua_Integer i = lua_tointeger(L, idx);
    if (static_cast<lua_Number>(i) == d)
      return Value::makeInt(static_cast<int64_t>(i));
    return Value::makeDouble(d);
  }
  case LUA_TSTRING:
    return api.makeString(lua_tostring(L, idx));
  case LUA_TTABLE:
    return toHavelFromTable(api, L, idx, depth);
  default:
    // functions/userdata/threads: not convertible; surface the type name so
    // callers see why instead of a silent null.
    return api.makeString(std::string("<lua:") + luaL_typename(L, idx) + ">");
  }
}

// Call a function stored as a field of the table the chunk returned.
Value callTableMethod(const VMApi &api, lua_State *L, int tableRef,
                      const std::string &name, const std::vector<Value> &args) {
  lua_rawgeti(L, LUA_REGISTRYINDEX, tableRef);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    throw std::runtime_error("lua: returned table is gone");
  }
  lua_getfield(L, -1, name.c_str());
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    throw std::runtime_error("lua: '" + name + "' is not a function");
  }
  for (const auto &a : args)
    pushHavel(api, L, a);
  int rc = lua_pcall(L, static_cast<int>(args.size()), 1, 0);
  if (rc != LUA_OK) {
    std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown lua error";
    lua_pop(L, 2);
    throw std::runtime_error("lua error in '" + name + "': " + err);
  }
  Value out = toHavel(api, L, -1);
  lua_pop(L, 2);
  return out;
}

// ---------------------------------------------------------------------------
// Calling a materialized global: the environment object holds one host-fn
// field per Lua global function. The host fn carries (state id, name).
// ---------------------------------------------------------------------------

Value callGlobal(const VMApi &api, lua_State *L, const std::string &name,
                 const std::vector<Value> &args) {
  lua_getglobal(L, name.c_str());
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1);
    throw std::runtime_error("lua: global '" + name + "' is not a function");
  }
  for (const auto &a : args)
    pushHavel(api, L, a);
  int rc = lua_pcall(L, static_cast<int>(args.size()), 1, 0);
  if (rc != LUA_OK) {
    std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown lua error";
    lua_pop(L, 1);
    throw std::runtime_error("lua error in '" + name + "': " + err);
  }
  Value out = toHavel(api, L, -1);
  lua_pop(L, 1);
  return out;
}

// ---------------------------------------------------------------------------
// Host API
// ---------------------------------------------------------------------------

Value luaVersion(const VMApi &api, const std::vector<Value> &) {
  (void)api;
  return api.makeString(LUA_VERSION);
}

Value luaEval(const VMApi &api, const std::vector<Value> &args) {
  if (args.empty())
    throw std::runtime_error("lua.eval(code) requires code");
  std::string code = api.toString(args[0]);
  lua_State *L = luaL_newstate();
  if (!L)
    throw std::runtime_error("lua: failed to create state");
  luaL_openlibs(L);
  int rc = luaL_dostring(L, code.c_str());
  if (rc != LUA_OK) {
    std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown lua error";
    lua_close(L);
    throw std::runtime_error("lua.eval: " + err);
  }
  Value out = lua_gettop(L) > 0 ? toHavel(api, L, -1) : Value::makeNull();
  lua_close(L);
  return out;
}

// Materialize every global function of the state as a host-fn field on the
// environment object, so `bot.start_automation(x)` is a plain CALL_METHOD.
// The state id travels on the environment object (__lua_id).
int64_t stateIdOf(const VMApi &api, const Value &env) {
  if (!env.isObjectId())
    return 0;
  Value id = api.getField(env, "__lua_id");
  return id.isInt() ? id.asInt() : 0;
}

// Materialize the callable fields of the Lua table at `idx` onto `env`.
// Used twice: for the chunk's returned table (a Lua file that does
// `return M` exposes M's functions) and for the globals table.
void materializeTableFunctions(const VMApi &api, lua_State *L, const Value &env, int idx,
                              int tableRef) {
  if (!lua_istable(L, idx))
    return;
  lua_pushnil(L);
  while (lua_next(L, idx < 0 ? idx - 1 : idx) != 0) {
    if (lua_type(L, -2) == LUA_TSTRING && lua_isfunction(L, -1)) {
      std::string name = lua_tostring(L, -2);
      if (name.rfind("_", 0) != 0) {
        int64_t sid = stateIdOf(api, env);
        // Distinguish the two sources: globals are looked up by name, the
        // returned table's functions are fetched from the table itself.
        bool isGlobal = tableRef == 0;
        std::string regName = "lua.call$" + std::to_string(sid) + "$" +
                              (isGlobal ? "g$" : "t$") + name;
        api.registerFunction(regName, [api, name, sid, tableRef](const std::vector<Value> &cargs) {
          auto it = g_states.find(sid);
          if (it == g_states.end())
            throw std::runtime_error("lua: environment released");
          if (tableRef == 0)
            return callGlobal(api, it->second, name, cargs);
          return callTableMethod(api, it->second, tableRef, name, cargs);
        });
        api.setField(env, name, api.makeFunctionRef(regName));
      }
    }
    lua_pop(L, 1);
  }
}

Value makeEnvironment(const VMApi &api, int64_t stateId) {
  auto obj = api.makeObject();
  api.setField(obj, "__lua_id", Value::makeInt(stateId));

  lua_State *L = g_states[stateId];
  // _G iteration via lua_next on the globals table.
  // Globals: iterate _G and materialize its functions (tableRef 0 = global
  // dispatch).
  lua_pushglobaltable(L);
  materializeTableFunctions(api, L, obj, -1, 0);
  lua_pop(L, 1); // global table
  return obj;
}

Value luaLoad(const VMApi &api, const std::vector<Value> &args) {
  if (args.empty())
    throw std::runtime_error("lua.load(path) requires a path");
  std::string path = api.toString(args[0]);
  lua_State *L = luaL_newstate();
  if (!L)
    throw std::runtime_error("lua: failed to create state");
  luaL_openlibs(L);
  int rc = luaL_dofile(L, path.c_str());
  if (rc != LUA_OK) {
    std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown lua error";
    lua_close(L);
    throw std::runtime_error("lua.load: " + err);
  }
  int64_t id = g_next_state_id++;
  g_states[id] = L;
  Value env = makeEnvironment(api, id);
  // A Lua file that ends with `return M` exposes M's functions rather than
  // globals - materialize those too (the returned table is on the stack).
  if (lua_gettop(L) > 0 && lua_istable(L, -1)) {
    int tableRef = luaL_ref(L, LUA_REGISTRYINDEX); // pops the table
    g_returned_refs[id] = tableRef;
    // Push the table back to iterate it (tableRef is a registry index, not
    // a stack slot).
    lua_rawgeti(L, LUA_REGISTRYINDEX, tableRef);
    materializeTableFunctions(api, L, env, -1, tableRef);
    lua_pop(L, 1);
  }
  return env;
}

Value luaRelease(const VMApi &api, const std::vector<Value> &args) {
  if (args.empty())
    return Value::makeNull();
  lua_State *L = stateFromArg(api, args[0]);
  if (L) {
    for (auto it = g_states.begin(); it != g_states.end(); ++it) {
      if (it->second == L) {
        lua_close(L);
        g_states.erase(it);
        break;
      }
    }
  }
  return Value::makeNull();
}

void shutdownLua() {
  for (auto &kv : g_states)
    lua_close(kv.second);
  g_states.clear();
}

} // namespace

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void registerLuaModule(const VMApi &api) {
  auto reg = [api](const char *name, auto fn) {
    api.registerFunction(
        name, [api, fn](const std::vector<Value> &args) mutable { return fn(api, args); });
  };

  reg("lua.version", luaVersion);
  reg("lua.eval", luaEval);
  reg("lua.load", luaLoad);
  reg("lua.release", luaRelease);

  // Expose the bridge as a global OBJECT named "lua" (the http/fs pattern):
  // the compile-time known-globals list is derived from setGlobal() sites, so
  // without this the identifier `lua` is unresolved at parse time.
  auto luaObj = api.makeObject();
  for (const char *field : {"version", "eval", "load", "release"}) {
    api.setField(luaObj, field, api.makeFunctionRef(std::string("lua.") + field));
  }
  api.setGlobal("lua", luaObj);

  std::atexit(shutdownLua);
  havel::debug("lua", "Lua 5.4 bridge registered");
}

} // namespace havel::modules::lua

HAVEL_MODULE_PLUGIN_EAGER(lua, "1.0.0",
                          "Lua 5.4 bridge (lua.*) for loading and calling Lua scripts from Havel",
    havel::modules::lua::registerLuaModule(*api);
);

#endif // HAVEL_MODULE_PLUGIN
