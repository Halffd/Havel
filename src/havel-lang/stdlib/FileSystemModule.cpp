/*
 * FileSystemModule.cpp - Fixed version
 *
 * File system module implementation using VMApi system.
 * Provides file and directory operations with clean host service integration.
 */
#include "FileSystemModule.hpp"
#include "../../host/filesystem/FileSystemService.hpp"
#include <cmath>
#include <sstream>

using namespace havel::compiler;

namespace havel::stdlib {

void registerFileSystemModule(havel::compiler::VMApi &api) {
  // Helper: convert BytecodeValue to string
  auto toString = [](const BytecodeValue &v) -> std::string {
    if (std::holds_alternative<std::string>(v))
      return std::get<std::string>(v);
    if (std::holds_alternative<int64_t>(v))
      return std::to_string(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v)) {
      double val = std::get<double>(v);
      if (val == std::floor(val) && std::abs(val) < 1e15) {
        return std::to_string(static_cast<long long>(val));
      }
      std::ostringstream oss;
      oss.precision(15);
      oss << val;
      return oss.str();
    }
    if (std::holds_alternative<bool>(v))
      return std::get<bool>(v) ? "true" : "false";
    return "";
  };

  // Helper: convert BytecodeValue to number
  auto toNumber = [](const BytecodeValue &v) -> double {
    if (std::holds_alternative<int64_t>(v))
      return static_cast<double>(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v))
      return std::get<double>(v);
    return 0.0;
  };

  // Create file system service instance
  static havel::host::FileSystemService fs;

  // Basic file operations for testing

  // fs.readFile(path) - Read entire file contents
  api.registerFunction(
      "fs.readFile",
      [toString](const std::vector<BytecodeValue> &args) -> BytecodeValue {
        if (args.size() != 1) {
          throw std::runtime_error(
              "fs.readFile() requires exactly one argument (path)");
        }

        std::string path = toString(args[0]);
        std::string content = fs.readFile(path);
        return BytecodeValue(content);
      });

  // fs.writeFile(path, content) - Write content to file
  api.registerFunction(
      "fs.writeFile",
      [toString](const std::vector<BytecodeValue> &args) -> BytecodeValue {
        if (args.size() != 2) {
          throw std::runtime_error(
              "fs.writeFile() requires exactly two arguments (path, content)");
        }

        std::string path = toString(args[0]);
        std::string content = toString(args[1]);
        bool success = fs.writeFile(path, content);
        return BytecodeValue(success);
      });

  // fs.exists(path) - Check if path exists
  api.registerFunction(
      "fs.exists",
      [toString](const std::vector<BytecodeValue> &args) -> BytecodeValue {
        if (args.size() != 1) {
          throw std::runtime_error(
              "fs.exists() requires exactly one argument (path)");
        }

        std::string path = toString(args[0]);
        bool exists = fs.exists(path);
        return BytecodeValue(exists);
      });

  // fs.listDirectory(path) - List directory contents
  api.registerFunction(
      "fs.listDirectory",
      [toString,
       &api](const std::vector<BytecodeValue> &args) -> BytecodeValue {
        if (args.size() != 1) {
          throw std::runtime_error(
              "fs.listDirectory() requires exactly one argument (path)");
        }

        std::string path = toString(args[0]);
        auto entries = fs.listDirectory(path);

        auto array = api.makeArray();
        for (const auto &entry : entries) {
          // Convert FileInfo to object
          auto entryObj = api.makeObject();
          api.setField(entryObj, "name", BytecodeValue(entry.name));
          api.setField(entryObj, "path", BytecodeValue(entry.path));
          api.setField(entryObj, "isFile", BytecodeValue(entry.isFile));
          api.setField(entryObj, "isDirectory",
                       BytecodeValue(entry.isDirectory));
          api.setField(entryObj, "size", BytecodeValue(entry.size));
          api.setField(entryObj, "modifiedTime",
                       BytecodeValue(entry.modifiedTime));
          api.push(array, entryObj);
        }
        return array;
      });

  // Create fs object with methods
  auto fsObj = api.makeObject();

  // Add methods
  api.setField(fsObj, "readFile", api.makeFunctionRef("fs.readFile"));
  api.setField(fsObj, "writeFile", api.makeFunctionRef("fs.writeFile"));
  api.setField(fsObj, "exists", api.makeFunctionRef("fs.exists"));
  api.setField(fsObj, "listDirectory", api.makeFunctionRef("fs.listDirectory"));

  // Register global fs object
  api.setGlobal("fs", fsObj);
}

} // namespace havel::stdlib
