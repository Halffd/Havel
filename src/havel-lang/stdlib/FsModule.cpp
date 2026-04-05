/*
 * FsModule.cpp - File system stdlib for VM (VMApi)
 * Pure VM implementation using VMApi
 */
#include "FsModule.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "havel-lang/core/Value.hpp"
#include "havel-lang/compiler/vm/VM.hpp"

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace fs = std::filesystem;

namespace havel::stdlib {

// Helper: extract string from Value using VM's resolveStringKey
static std::string valueToString(const Value &v, VMApi &api) {
  return api.vm.resolveStringKey(v);
}

// Helper: create StringId from std::string
static Value makeStringId(const std::string &s, VMApi &api) {
  auto strRef = api.vm.getHeap().allocateString(s);
  return Value::makeStringId(strRef.id);
}

// Helper: create FileObject as a host object with file info
static Value createFileObject(const fs::path &path, VMApi &api) {
  auto &vm = api.vm;
  auto objRef = vm.createHostObject();

  // name, path, extension - use heap StringId
  vm.setHostObjectField(objRef, "name", makeStringId(path.filename().string(), api));
  vm.setHostObjectField(objRef, "path", makeStringId(path.string(), api));
  
  std::string ext = path.extension().string();
  if (!ext.empty() && ext[0] == '.')
    ext = ext.substr(1);
  vm.setHostObjectField(objRef, "extension", makeStringId(ext, api));

  // Size
  std::error_code ec;
  auto fsize = fs::file_size(path, ec);
  vm.setHostObjectField(objRef, "size",
                        Value::makeInt(ec ? 0 : static_cast<int64_t>(fsize)));

  // Timestamps - use std::time_t for portability
  auto ctime = fs::last_write_time(path, ec);
  if (!ec) {
    auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
        std::chrono::file_clock::to_sys(ctime));
    vm.setHostObjectField(objRef, "modified", 
                          Value::makeInt(static_cast<int64_t>(sctp.time_since_epoch().count())));
  } else {
    vm.setHostObjectField(objRef, "modified", Value::makeInt(0));
  }
  vm.setHostObjectField(objRef, "created", Value::makeInt(0));
  vm.setHostObjectField(objRef, "access", Value::makeInt(0));

  // isDir / isFile
  auto isDir = fs::is_directory(path, ec);
  vm.setHostObjectField(objRef, "isDir", Value::makeBool(isDir));
  vm.setHostObjectField(objRef, "isFile", Value::makeBool(!isDir));

  return Value::makeObjectId(objRef.id);
}

void registerFsModule(VMApi &api) {
  // File existence checks
  api.registerFunction(
      "fs.exists", [&api](const std::vector<Value> &args) {
        if (args.empty())
          return Value::makeBool(false);
        std::string path = valueToString(args[0], api);
        return Value::makeBool(fs::exists(path));
      });

  api.registerFunction(
      "fs.isDir", [&api](const std::vector<Value> &args) {
        if (args.empty())
          return Value::makeBool(false);
        std::string path = valueToString(args[0], api);
        std::error_code ec;
        return Value::makeBool(fs::is_directory(path, ec));
      });

  api.registerFunction(
      "fs.isFile", [&api](const std::vector<Value> &args) {
        if (args.empty())
          return Value::makeBool(false);
        std::string path = valueToString(args[0], api);
        std::error_code ec;
        return Value::makeBool(fs::is_regular_file(path, ec));
      });

  // Reading
  api.registerFunction(
      "fs.read", [&api](const std::vector<Value> &args) {
        if (args.empty())
          return Value::makeNull();
        std::string path = valueToString(args[0], api);
        std::ifstream file(path);
        if (!file.is_open())
          return Value::makeNull();
        std::stringstream ss;
        ss << file.rdbuf();
        std::string content = ss.str();
        return makeStringId(content, api);
      });

  api.registerFunction(
      "fs.readDir", [&api](const std::vector<Value> &args) {
        if (args.empty())
          return Value::makeNull();
        std::string path = valueToString(args[0], api);
        std::error_code ec;
        auto arrRef = api.vm.createHostArray();
        for (const auto &entry : fs::directory_iterator(path, ec)) {
          auto fileObj = createFileObject(entry.path(), api);
          api.vm.pushHostArrayValue(arrRef, fileObj);
        }
        return Value::makeArrayId(arrRef.id);
      });

  api.registerFunction(
      "fs.readLines", [&api](const std::vector<Value> &args) {
        if (args.empty())
          return Value::makeNull();
        std::string path = valueToString(args[0], api);
        std::ifstream file(path);
        if (!file.is_open())
          return Value::makeNull();
        auto arrRef = api.vm.createHostArray();
        std::string line;
        while (std::getline(file, line)) {
          api.vm.pushHostArrayValue(arrRef, makeStringId(line, api));
        }
        return Value::makeArrayId(arrRef.id);
      });

  // Writing
  api.registerFunction(
      "fs.write", [&api](const std::vector<Value> &args) {
        if (args.size() < 2)
          return Value::makeNull();
        std::string path = valueToString(args[0], api);
        std::string content = valueToString(args[1], api);
        std::ofstream file(path);
        if (!file.is_open())
          return Value::makeNull();
        file << content;
        return Value::makeBool(true);
      });

  api.registerFunction(
      "fs.append", [&api](const std::vector<Value> &args) {
        if (args.size() < 2)
          return Value::makeNull();
        std::string path = valueToString(args[0], api);
        std::string content = valueToString(args[1], api);
        std::ofstream file(path, std::ios::app);
        if (!file.is_open())
          return Value::makeNull();
        file << content;
        return Value::makeBool(true);
      });

  api.registerFunction(
      "fs.touch", [&api](const std::vector<Value> &args) {
        if (args.empty())
          return Value::makeNull();
        std::string path = valueToString(args[0], api);
        std::ofstream file(path, std::ios::app);
        if (!file.is_open())
          return Value::makeNull();
        return createFileObject(path, api);
      });

  // Directory operations
  api.registerFunction(
      "fs.mkdir", [&api](const std::vector<Value> &args) {
        if (args.empty())
          return Value::makeBool(false);
        std::string path = valueToString(args[0], api);
        std::error_code ec;
        return Value::makeBool(fs::create_directory(path, ec));
      });

  api.registerFunction(
      "fs.mkdirAll", [&api](const std::vector<Value> &args) {
        if (args.empty())
          return Value::makeBool(false);
        std::string path = valueToString(args[0], api);
        std::error_code ec;
        fs::create_directories(path, ec);
        return Value::makeBool(!ec);
      });

  // File operations
  api.registerFunction(
      "fs.delete", [&api](const std::vector<Value> &args) {
        if (args.empty())
          return Value::makeBool(false);
        std::string path = valueToString(args[0], api);
        std::error_code ec;
        return Value::makeBool(fs::remove(path, ec));
      });

  api.registerFunction(
      "fs.copy", [&api](const std::vector<Value> &args) {
        if (args.size() < 2)
          return Value::makeBool(false);
        std::string src = valueToString(args[0], api);
        std::string dst = valueToString(args[1], api);
        std::error_code ec;
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        return Value::makeBool(!ec);
      });

  api.registerFunction(
      "fs.move", [&api](const std::vector<Value> &args) {
        if (args.size() < 2)
          return Value::makeBool(false);
        std::string src = valueToString(args[0], api);
        std::string dst = valueToString(args[1], api);
        std::error_code ec;
        fs::rename(src, dst, ec);
        return Value::makeBool(!ec);
      });

  // Create fs namespace object
  auto fsObj = api.makeObject();
  api.setField(fsObj, "exists", api.makeFunctionRef("fs.exists"));
  api.setField(fsObj, "isDir", api.makeFunctionRef("fs.isDir"));
  api.setField(fsObj, "isFile", api.makeFunctionRef("fs.isFile"));
  api.setField(fsObj, "read", api.makeFunctionRef("fs.read"));
  api.setField(fsObj, "readDir", api.makeFunctionRef("fs.readDir"));
  api.setField(fsObj, "readLines", api.makeFunctionRef("fs.readLines"));
  api.setField(fsObj, "write", api.makeFunctionRef("fs.write"));
  api.setField(fsObj, "append", api.makeFunctionRef("fs.append"));
  api.setField(fsObj, "touch", api.makeFunctionRef("fs.touch"));
  api.setField(fsObj, "mkdir", api.makeFunctionRef("fs.mkdir"));
  api.setField(fsObj, "mkdirAll", api.makeFunctionRef("fs.mkdirAll"));
  api.setField(fsObj, "delete", api.makeFunctionRef("fs.delete"));
  api.setField(fsObj, "copy", api.makeFunctionRef("fs.copy"));
  api.setField(fsObj, "move", api.makeFunctionRef("fs.move"));
  api.setGlobal("fs", fsObj);
}

} // namespace havel::stdlib
