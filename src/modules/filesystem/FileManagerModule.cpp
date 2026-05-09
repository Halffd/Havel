#include "FileManagerModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "host/ServiceRegistry.hpp"
#include "host/filesystem/FileSystemService.hpp"
#include "utils/Logger.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;
using host::FileSystemService;

static const char* MODULE_MARKER = "__filemanager_module";

static bool isModuleObject(const VMApi& api, const Value& val) {
  if (!val.isObjectId()) return false;
  auto marker = api.getField(val, MODULE_MARKER);
  return marker.isBool() && marker.asBool();
}

static std::vector<Value> stripReceiver(const VMApi& api, const std::vector<Value>& args) {
  if (!args.empty() && isModuleObject(api, args[0])) {
    return std::vector<Value>(args.begin() + 1, args.end());
  }
  return args;
}

static std::shared_ptr<FileSystemService> getService() {
  auto svc = host::ServiceRegistry::instance().get<FileSystemService>();
  if (!svc) debug("FileManagerModule: FileSystemService not available");
  return svc;
}

static std::string toString(const VMApi& api, const Value& v) {
  if (v.isStringId() || v.isStringValId()) return api.toString(v);
  if (v.isNull()) return "";
  if (v.isInt()) return std::to_string(v.asInt());
  if (v.isDouble()) return std::to_string(v.asDouble());
  if (v.isBool()) return v.asBool() ? "true" : "false";
  return "";
}

static Value fileInfoToObject(const VMApi& api, const host::FileInfo& info) {
  auto obj = api.makeObject();
  api.setField(obj, "name", api.makeString(info.name));
  api.setField(obj, "path", api.makeString(info.path));
  api.setField(obj, "isFile", Value::makeBool(info.isFile));
  api.setField(obj, "isDirectory", Value::makeBool(info.isDirectory));
  api.setField(obj, "size", Value::makeInt(info.size));
  api.setField(obj, "modifiedTime", Value::makeInt(info.modifiedTime));
  return obj;
}

void registerFileManagerModule(const VMApi& api) {
  HAVEL_BEGIN_MODULE("FileManager");

  HAVEL_REGISTER_FUNCTION(api, "filemanager.read", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return api.makeString("");
    auto svc = getService();
    if (!svc) return api.makeString("");
    try { return api.makeString(svc->readFile(toString(api, args[0]))); } catch (const std::exception& e) { debug("filemanager.read error: {}", e.what()); return api.makeString(""); }
  });

  HAVEL_REGISTER_FUNCTION(api, "filemanager.write", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->writeFile(toString(api, args[0]), toString(api, args[1]))); } catch (const std::exception& e) { debug("filemanager.write error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "filemanager.append", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->appendFile(toString(api, args[0]), toString(api, args[1]))); } catch (const std::exception& e) { debug("filemanager.append error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "filemanager.delete", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->deleteFile(toString(api, args[0]))); } catch (const std::exception& e) { debug("filemanager.delete error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "filemanager.copy", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->copyFile(toString(api, args[0]), toString(api, args[1]))); } catch (const std::exception& e) { debug("filemanager.copy error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "filemanager.move", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->moveFile(toString(api, args[0]), toString(api, args[1]))); } catch (const std::exception& e) { debug("filemanager.move error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "filemanager.list", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return api.makeArray();
    auto svc = getService();
    if (!svc) return api.makeArray();
    try {
      auto entries = svc->listDirectory(toString(api, args[0]));
      auto arr = api.makeArray();
      for (const auto& e : entries) api.push(arr, fileInfoToObject(api, e));
      return arr;
    } catch (const std::exception& e) { debug("filemanager.list error: {}", e.what()); return api.makeArray(); }
  });

  HAVEL_REGISTER_FUNCTION(api, "filemanager.info", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { return fileInfoToObject(api, svc->getFileInfo(toString(api, args[0]))); } catch (const std::exception& e) { debug("filemanager.info error: {}", e.what()); return Value::makeNull(); }
  });

  HAVEL_REGISTER_FUNCTION(api, "filemanager.exists", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->exists(toString(api, args[0]))); } catch (const std::exception& e) { return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "filemanager.isFile", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->isFile(toString(api, args[0]))); } catch (const std::exception& e) { return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "filemanager.isDirectory", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->isDirectory(toString(api, args[0]))); } catch (const std::exception& e) { return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "filemanager.createDir", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->createDirectories(toString(api, args[0]))); } catch (const std::exception& e) { debug("filemanager.createDir error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "filemanager.removeDir", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->deleteDirectory(toString(api, args[0]))); } catch (const std::exception& e) { debug("filemanager.removeDir error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "filemanager.joinPath", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return api.makeString("");
    return api.makeString(FileSystemService::joinPath(toString(api, args[0]), toString(api, args[1])));
  });

  HAVEL_REGISTER_FUNCTION(api, "filemanager.absolutePath", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return api.makeString("");
    return api.makeString(FileSystemService::absolutePath(toString(api, args[0])));
  });

  HAVEL_REGISTER_FUNCTION(api, "filemanager.parentPath", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return api.makeString("");
    return api.makeString(FileSystemService::parentPath(toString(api, args[0])));
  });

  HAVEL_REGISTER_FUNCTION(api, "filemanager.fileName", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return api.makeString("");
    return api.makeString(FileSystemService::fileName(toString(api, args[0])));
  });

  HAVEL_REGISTER_FUNCTION(api, "filemanager.extension", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return api.makeString("");
    return api.makeString(FileSystemService::extension(toString(api, args[0])));
  });

  HAVEL_REGISTER_FUNCTION(api, "filemanager.currentDir", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    return api.makeString(FileSystemService::currentDirectory());
  });

  HAVEL_REGISTER_FUNCTION(api, "filemanager.homeDir", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    return api.makeString(FileSystemService::homeDirectory());
  });

  HAVEL_REGISTER_FUNCTION(api, "filemanager.tempDir", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    return api.makeString(FileSystemService::tempDirectory());
  });

  auto obj = api.makeObject();
  api.setGlobal("filemanager", obj);
  api.setField(obj, MODULE_MARKER, Value::makeBool(true));
  api.setField(obj, "read", api.makeFunctionRef("filemanager.read"));
  api.setField(obj, "write", api.makeFunctionRef("filemanager.write"));
  api.setField(obj, "append", api.makeFunctionRef("filemanager.append"));
  api.setField(obj, "delete", api.makeFunctionRef("filemanager.delete"));
  api.setField(obj, "copy", api.makeFunctionRef("filemanager.copy"));
  api.setField(obj, "move", api.makeFunctionRef("filemanager.move"));
  api.setField(obj, "list", api.makeFunctionRef("filemanager.list"));
  api.setField(obj, "info", api.makeFunctionRef("filemanager.info"));
  api.setField(obj, "exists", api.makeFunctionRef("filemanager.exists"));
  api.setField(obj, "isFile", api.makeFunctionRef("filemanager.isFile"));
  api.setField(obj, "isDirectory", api.makeFunctionRef("filemanager.isDirectory"));
  api.setField(obj, "createDir", api.makeFunctionRef("filemanager.createDir"));
  api.setField(obj, "removeDir", api.makeFunctionRef("filemanager.removeDir"));
  api.setField(obj, "joinPath", api.makeFunctionRef("filemanager.joinPath"));
  api.setField(obj, "absolutePath", api.makeFunctionRef("filemanager.absolutePath"));
  api.setField(obj, "parentPath", api.makeFunctionRef("filemanager.parentPath"));
  api.setField(obj, "fileName", api.makeFunctionRef("filemanager.fileName"));
  api.setField(obj, "extension", api.makeFunctionRef("filemanager.extension"));
  api.setField(obj, "currentDir", api.makeFunctionRef("filemanager.currentDir"));
  api.setField(obj, "homeDir", api.makeFunctionRef("filemanager.homeDir"));
  api.setField(obj, "tempDir", api.makeFunctionRef("filemanager.tempDir"));

  HAVEL_END_MODULE();
}

} // namespace havel::modules
