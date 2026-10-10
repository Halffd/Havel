/*
 * PythonModule.cpp - optional CPython bridge, exposed as py.*
 *
 * Links libpython only when this module is built and loaded. havel_core and
 * havel_lang never reference CPython: the interpreter is reached exclusively
 * through this dlopen'd module, mirroring the seam used to keep Qt out of the
 * embeddable core.
 *
 * Every entry point takes the GIL for the duration of the call and converts
 * Python exceptions into std::runtime_error, which is how host modules report
 * script-visible failures.
 *
 * Opaque Python objects (numpy arrays, socket objects, ...) have no direct
 * Havel representation. They are wrapped as an object carrying __py_id,
 * __py_type and __py_repr, with the PyObject kept alive in a side registry
 * keyed by __py_id. Use py.get/py.set/py.method to work with them, and
 * py.release when done. This avoids storing raw PyObject* inside VM-managed
 * values, which have no finalizer hook.
 */

#include "c/ModulePlugin.h"

#ifdef HAVEL_MODULE_PLUGIN

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "havel-lang/compiler/vm/VMApi.hpp"
#include "utils/Logger.hpp"

#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel::modules::python {

using compiler::Value;
using compiler::VMApi;

namespace {

// ---------------------------------------------------------------------------
// Interpreter lifetime and GIL
// ---------------------------------------------------------------------------

// Defined below; drops bridge-owned references before finalizing.
void shutdownPython();

// RAII: bring the interpreter up on first use and hold the GIL for the call.
class PyGuard {
public:
  PyGuard() {
    static std::once_flag once;
    std::call_once(once, [] {
      if (!Py_IsInitialized()) {
        Py_InitializeEx(0);
      }
      // CPython retains most of its allocations until Py_FinalizeEx runs, and
      // references the bridge still owns would survive it. Registered after the
      // sanitizer installs its own exit hook, so atexit's reverse-order
      // execution tears the interpreter down before the leak check runs.
      std::atexit([] { shutdownPython(); });
    });
    gil_ = PyGILState_Ensure();
  }

  ~PyGuard() { PyGILState_Release(gil_); }

  PyGuard(const PyGuard &) = delete;
  PyGuard &operator=(const PyGuard &) = delete;

private:
  PyGILState_STATE gil_{PyGILState_UNLOCKED};
};

// Owning reference to the namespace shared by py.run and py.eval, so a name
// bound by py.run is visible to a later py.eval. File-scope rather than a
// function-local static so shutdownPython() can drop it before finalizing.
PyObject *g_namespace = nullptr;

PyObject *sharedNamespace() {
  static std::once_flag once;
  std::call_once(once, [] {
    g_namespace = PyDict_New();
    if (!g_namespace) return;
    PyObject *builtins = PyEval_GetBuiltins();  // borrowed
    if (builtins) PyDict_SetItemString(g_namespace, "__builtins__", builtins);
    PyObject *name = PyUnicode_FromStringAndSize("__havel__", 8);
    if (name) {
      PyDict_SetItemString(g_namespace, "__name__", name);
      Py_DECREF(name);
    }
  });
  return g_namespace;
}

// ---------------------------------------------------------------------------
// Opaque handle registry
// ---------------------------------------------------------------------------

std::mutex g_handles_mutex;
std::unordered_map<int64_t, PyObject *> g_handles;  // owning references
int64_t g_next_handle = 1;

int64_t retainHandle(PyObject *o) {
  Py_INCREF(o);
  std::lock_guard<std::mutex> lock(g_handles_mutex);
  int64_t id = g_next_handle++;
  g_handles.emplace(id, o);
  return id;
}

// Borrowed reference; the caller must hold the GIL.
PyObject *lookupHandle(int64_t id) {
  std::lock_guard<std::mutex> lock(g_handles_mutex);
  auto it = g_handles.find(id);
  return it == g_handles.end() ? nullptr : it->second;
}

// Takes ownership of a borrowed reference and always consumes it.
void releaseHandle(int64_t id) {
  PyObject *o = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_handles_mutex);
    auto it = g_handles.find(id);
    if (it == g_handles.end()) return;
    o = it->second;
    g_handles.erase(it);
  }
  Py_XDECREF(o);
}

size_t liveHandleCount() {
  std::lock_guard<std::mutex> lock(g_handles_mutex);
  return g_handles.size();
}

// Drop every bridge-owned reference, then finalize. Handles the script never
// released would otherwise keep their objects -- and the types those objects
// reference -- alive past Py_FinalizeEx, which ASan reports as leaks.
void shutdownPython() {
  if (!Py_IsInitialized()) return;
  PyGILState_STATE gil = PyGILState_Ensure();
  std::unordered_map<int64_t, PyObject *> handles;
  {
    std::lock_guard<std::mutex> lock(g_handles_mutex);
    handles.swap(g_handles);
  }
  for (auto &entry : handles) {
    Py_XDECREF(entry.second);
  }
  Py_CLEAR(g_namespace);
  // Release before finalizing: Py_FinalizeEx destroys the thread state, and
  // PyGILState_Release on a dead thread state aborts with
  // "auto-releasing thread-state, but no thread-state for this thread".
  PyGILState_Release(gil);
  Py_FinalizeEx();
}

// ---------------------------------------------------------------------------
// Error translation
// ---------------------------------------------------------------------------

// Consumes any pending Python error and throws it as a Havel runtime error.
void throwPythonError(const std::string &context) {
  if (!PyErr_Occurred()) {
    throw std::runtime_error(context + ": unknown Python failure");
  }
  PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
  PyErr_Fetch(&type, &value, &traceback);
  PyErr_NormalizeException(&type, &value, &traceback);

  std::string detail = "unknown";
  if (value) {
    PyObject *text = PyObject_Str(value);
    if (text) {
      const char *utf8 = PyUnicode_AsUTF8(text);
      if (utf8) detail = utf8;
      Py_DECREF(text);
    } else {
      PyErr_Clear();
    }
  }

  std::string typeName = "Exception";
  if (type) {
    PyObject *name = PyObject_GetAttrString(type, "__name__");
    if (name) {
      const char *utf8 = PyUnicode_AsUTF8(name);
      if (utf8) typeName = utf8;
      Py_DECREF(name);
    } else {
      PyErr_Clear();
    }
  }

  Py_XDECREF(type);
  Py_XDECREF(value);
  Py_XDECREF(traceback);
  PyErr_Clear();

  throw std::runtime_error(context + ": " + typeName + ": " + detail);
}

// ---------------------------------------------------------------------------
// Havel -> Python
// ---------------------------------------------------------------------------

PyObject *toPython(const VMApi &api, const Value &v) {
  if (v.isNull()) {
    Py_RETURN_NONE;
  }
  if (v.isBool()) {
    return PyBool_FromLong(v.asBool() ? 1 : 0);
  }
  if (v.isInt()) {
    return PyLong_FromLongLong(static_cast<long long>(v.asInt()));
  }
  if (v.isDouble()) {
    return PyFloat_FromDouble(v.asDouble());
  }
  if (v.isStringValId() || v.isStringId()) {
    std::string s = api.toString(v);
    return PyUnicode_FromStringAndSize(s.data(), static_cast<Py_ssize_t>(s.size()));
  }
  if (v.isArrayId()) {
    uint32_t n = api.length(v);
    PyObject *list = PyList_New(n);
    if (!list) return nullptr;
    for (uint32_t i = 0; i < n; ++i) {
      PyObject *item = toPython(api, api.getAt(v, i));
      if (!item) {
        Py_DECREF(list);
        return nullptr;
      }
      PyList_SET_ITEM(list, i, item);  // steals item
    }
    return list;
  }
  if (v.isObjectId()) {
    PyObject *dict = PyDict_New();
    if (!dict) return nullptr;
    for (const std::string &key : api.getObjectKeys(v)) {
      PyObject *item = toPython(api, api.getField(v, key));
      if (!item) {
        Py_DECREF(dict);
        return nullptr;
      }
      if (PyDict_SetItemString(dict, key.c_str(), item) != 0) {
        Py_DECREF(item);
        Py_DECREF(dict);
        return nullptr;
      }
      Py_DECREF(item);
    }
    return dict;
  }
  // Enum, function, and any other Havel value: expose its string form.
  std::string s = api.toString(v);
  return PyUnicode_FromStringAndSize(s.data(), static_cast<Py_ssize_t>(s.size()));
}

// ---------------------------------------------------------------------------
// Python -> Havel
// ---------------------------------------------------------------------------

// Wraps an object with no direct Havel representation. Caller holds the GIL.
Value wrapOpaque(const VMApi &api, PyObject *o) {
  Value obj = api.makeObject();

  std::string typeName = "object";
  PyObject *name = PyObject_GetAttrString(reinterpret_cast<PyObject *>(Py_TYPE(o)), "__name__");
  if (name) {
    const char *utf8 = PyUnicode_AsUTF8(name);
    if (utf8) typeName = utf8;
    Py_DECREF(name);
  } else {
    PyErr_Clear();
  }

  std::string repr = "<object>";
  PyObject *text = PyObject_Repr(o);
  if (text) {
    const char *utf8 = PyUnicode_AsUTF8(text);
    if (utf8) repr = utf8;
    Py_DECREF(text);
  } else {
    PyErr_Clear();
  }

  api.setField(obj, "__py_id", Value::makeInt(retainHandle(o)));
  api.setField(obj, "__py_type", api.makeString(typeName));
  api.setField(obj, "__py_repr", api.makeString(repr));
  return obj;
}

Value fromPython(const VMApi &api, PyObject *o) {
  if (!o || o == Py_None) return Value::makeNull();

  // bool must precede int: PyBool is a subclass of PyLong.
  if (PyBool_Check(o)) return Value::makeBool(o == Py_True);

  if (PyLong_Check(o)) {
    int overflow = 0;
    long long asInt = PyLong_AsLongLongAndOverflow(o, &overflow);
    if (overflow == 0 && !(asInt == -1 && PyErr_Occurred())) {
      return Value::makeInt(static_cast<int64_t>(asInt));
    }
    PyErr_Clear();
    double asDouble = PyFloat_AsDouble(o);
    if (!PyErr_Occurred()) return Value::makeDouble(asDouble);
    PyErr_Clear();
    return wrapOpaque(api, o);
  }

  if (PyFloat_Check(o)) return Value::makeDouble(PyFloat_AsDouble(o));

  if (PyUnicode_Check(o)) {
    Py_ssize_t size = 0;
    const char *utf8 = PyUnicode_AsUTF8AndSize(o, &size);
    if (!utf8) {
      throwPythonError("py: reading string result");
    }
    return api.makeString(std::string(utf8, static_cast<size_t>(size)));
  }

  if (PyBytes_Check(o)) {
    char *data = nullptr;
    Py_ssize_t size = 0;
    if (PyBytes_AsStringAndSize(o, &data, &size) != 0) {
      throwPythonError("py: reading bytes result");
    }
    return api.makeString(std::string(data, static_cast<size_t>(size)));
  }

  if (PyList_Check(o) || PyTuple_Check(o)) {
    Py_ssize_t n = PySequence_Size(o);
    if (n < 0) throwPythonError("py: sizing sequence result");
    Value arr = api.makeArray();
    for (Py_ssize_t i = 0; i < n; ++i) {
      PyObject *item = PySequence_GetItem(o, i);  // new reference
      if (!item) throwPythonError("py: reading sequence element");
      Value converted = fromPython(api, item);
      Py_DECREF(item);
      api.push(arr, converted);
    }
    return arr;
  }

  if (PyDict_Check(o)) {
    Value obj = api.makeObject();
    PyObject *key = nullptr;
    PyObject *item = nullptr;
    Py_ssize_t pos = 0;
    while (PyDict_Next(o, &pos, &key, &item)) {
      PyObject *keyText = PyObject_Str(key);
      if (!keyText) throwPythonError("py: converting dict key to string");
      const char *utf8 = PyUnicode_AsUTF8(keyText);
      if (!utf8) {
        Py_DECREF(keyText);
        throwPythonError("py: reading dict key");
      }
      std::string field(utf8);
      Py_DECREF(keyText);
      api.setField(obj, field, fromPython(api, item));
    }
    return obj;
  }

  return wrapOpaque(api, o);
}

// ---------------------------------------------------------------------------
// Argument helpers
// ---------------------------------------------------------------------------

// Builds the positional-argument tuple for PyObject_CallObject. Returns a new
// reference, or throws with context on failure.
PyObject *argsToTuple(const VMApi &api, const std::vector<Value> &args, size_t first,
                      const std::string &context) {
  const size_t n = args.size() - first;
  PyObject *tuple = PyTuple_New(static_cast<Py_ssize_t>(n));
  if (!tuple) throwPythonError(context);
  for (size_t i = 0; i < n; ++i) {
    PyObject *item = toPython(api, args[first + i]);
    if (!item) {
      Py_DECREF(tuple);
      throwPythonError(context);
    }
    PyTuple_SET_ITEM(tuple, static_cast<Py_ssize_t>(i), item);  // steals item
  }
  return tuple;
}

// Accepts either the raw __py_id integer or the wrapper object returned by a
// conversion, so callers can pass whichever they have.
int64_t requireHandleId(const VMApi &api, const Value &v) {
  if (v.isInt()) return v.asInt();
  if (v.isObjectId() && api.hasField(v, "__py_id")) {
    Value id = api.getField(v, "__py_id");
    if (id.isInt()) return id.asInt();
  }
  throw std::runtime_error(
      "py: expected a Python handle (the wrapper object or its __py_id)");
}

PyObject *requireHandle(const VMApi &api, const Value &v, const std::string &context) {
  PyObject *o = lookupHandle(requireHandleId(api, v));
  if (!o) {
    throw std::runtime_error(context + ": handle is not live (already released?)");
  }
  return o;
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

Value pyVersion(const VMApi &api, const std::vector<Value> &) {
  PyGuard guard;
  const char *raw = Py_GetVersion();
  std::string full = raw ? raw : "";
  size_t space = full.find(' ');
  if (space != std::string::npos) full = full.substr(0, space);
  return api.makeString(full);
}

Value pyAvailable(const VMApi &, const std::vector<Value> &) {
  PyGuard guard;
  return Value::makeBool(Py_IsInitialized() != 0);
}

Value pyRun(const VMApi &api, const std::vector<Value> &args) {
  if (args.empty() || !(args[0].isStringValId() || args[0].isStringId())) {
    throw std::runtime_error("py.run(source) requires a string");
  }
  PyGuard guard;
  PyObject *ns = sharedNamespace();
  if (!ns) throw std::runtime_error("py.run: could not create the Python namespace");

  std::string source = api.toString(args[0]);
  PyObject *result = PyRun_String(source.c_str(), Py_file_input, ns, ns);
  if (!result) throwPythonError("py.run");
  Py_DECREF(result);
  return Value::makeNull();
}

Value pyEval(const VMApi &api, const std::vector<Value> &args) {
  if (args.empty()) throw std::runtime_error("py.eval(expression) requires an argument");
  PyGuard guard;
  PyObject *ns = sharedNamespace();
  if (!ns) throw std::runtime_error("py.eval: could not create the Python namespace");

  std::string source = api.toString(args[0]);
  PyObject *result = PyRun_String(source.c_str(), Py_eval_input, ns, ns);
  if (!result) throwPythonError("py.eval");
  Value converted = fromPython(api, result);
  Py_DECREF(result);
  return converted;
}

Value pyCall(const VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 2) {
    throw std::runtime_error("py.call(module, function, args...) requires a module and a name");
  }
  PyGuard guard;

  std::string moduleName = api.toString(args[0]);
  PyObject *module = PyImport_ImportModule(moduleName.c_str());
  if (!module) throwPythonError("py.call: importing '" + moduleName + "'");

  std::string funcName = api.toString(args[1]);
  PyObject *func = PyObject_GetAttrString(module, funcName.c_str());
  if (!func) {
    Py_DECREF(module);
    PyErr_Clear();
    throw std::runtime_error("py.call: '" + moduleName + "' has no attribute '" + funcName + "'");
  }

  PyObject *tuple = argsToTuple(api, args, 2, "py.call: converting arguments");
  PyObject *result = PyObject_CallObject(func, tuple);
  Py_DECREF(tuple);
  Py_DECREF(func);
  Py_DECREF(module);

  if (!result) throwPythonError("py.call: calling " + moduleName + "." + funcName);
  Value converted = fromPython(api, result);
  Py_DECREF(result);
  return converted;
}

Value pyGet(const VMApi &api, const std::vector<Value> &args) {
  if (args.size() != 2) throw std::runtime_error("py.get(handle, attribute) requires two arguments");
  PyGuard guard;
  PyObject *target = requireHandle(api, args[0], "py.get");
  std::string attribute = api.toString(args[1]);

  PyObject *value = PyObject_GetAttrString(target, attribute.c_str());
  if (!value) {
    PyErr_Clear();
    throw std::runtime_error("py.get: no attribute '" + attribute + "'");
  }
  Value converted = fromPython(api, value);
  Py_DECREF(value);
  return converted;
}

Value pySet(const VMApi &api, const std::vector<Value> &args) {
  if (args.size() != 3) {
    throw std::runtime_error("py.set(handle, attribute, value) requires three arguments");
  }
  PyGuard guard;
  PyObject *target = requireHandle(api, args[0], "py.set");
  std::string attribute = api.toString(args[1]);

  PyObject *value = toPython(api, args[2]);
  if (!value) throwPythonError("py.set: converting value");

  int rc = PyObject_SetAttrString(target, attribute.c_str(), value);
  Py_DECREF(value);
  if (rc != 0) throwPythonError("py.set: assigning '" + attribute + "'");
  return Value::makeNull();
}

Value pyMethod(const VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 2) {
    throw std::runtime_error("py.method(handle, name, args...) requires a handle and a method name");
  }
  PyGuard guard;
  PyObject *target = requireHandle(api, args[0], "py.method");
  std::string method = api.toString(args[1]);

  PyObject *func = PyObject_GetAttrString(target, method.c_str());
  if (!func) {
    PyErr_Clear();
    throw std::runtime_error("py.method: no method '" + method + "'");
  }

  PyObject *tuple = argsToTuple(api, args, 2, "py.method: converting arguments");
  PyObject *result = PyObject_CallObject(func, tuple);
  Py_DECREF(tuple);
  Py_DECREF(func);

  if (!result) throwPythonError("py.method: calling '" + method + "'");
  Value converted = fromPython(api, result);
  Py_DECREF(result);
  return converted;
}

Value pyRelease(const VMApi &api, const std::vector<Value> &args) {
  if (args.size() != 1) throw std::runtime_error("py.release(handle) requires one argument");
  PyGuard guard;
  releaseHandle(requireHandleId(api, args[0]));
  return Value::makeNull();
}

Value pyHandles(const VMApi &, const std::vector<Value> &) {
  PyGuard guard;
  return Value::makeInt(static_cast<int64_t>(liveHandleCount()));
}

Value pyStr(const VMApi &api, const std::vector<Value> &args) {
  if (args.size() != 1) throw std::runtime_error("py.str(handle) requires one argument");
  PyGuard guard;
  PyObject *target = requireHandle(api, args[0], "py.str");
  PyObject *text = PyObject_Str(target);
  if (!text) throwPythonError("py.str");
  const char *utf8 = PyUnicode_AsUTF8(text);
  std::string result = utf8 ? utf8 : "";
  Py_DECREF(text);
  return api.makeString(result);
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void registerPythonModule(const VMApi &api) {
  auto reg = [api](const char *name, auto fn) {
    api.registerFunction(name, [api, fn](const std::vector<Value> &args) mutable {
      return fn(api, args);
    });
  };

  reg("py.version", pyVersion);
  reg("py.available", pyAvailable);
  reg("py.run", pyRun);
  reg("py.eval", pyEval);
  reg("py.call", pyCall);
  reg("py.get", pyGet);
  reg("py.set", pySet);
  reg("py.method", pyMethod);
  reg("py.release", pyRelease);
  reg("py.handles", pyHandles);
  reg("py.str", pyStr);

  havel::debug("python", "CPython bridge registered");
}

}  // namespace
}  // namespace havel::modules::python

HAVEL_MODULE_PLUGIN_EAGER(python, "1.0.0", "CPython bridge (py.*) for calling Python from Havel",
    havel::modules::python::registerPythonModule(*api);
)

#endif  // HAVEL_MODULE_PLUGIN