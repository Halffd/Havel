/*
 * FileManagerModule.cpp
 *
 * Advanced file operations module for Havel language.
 * Host binding - connects language to FileManager.
 */
#include "FileManagerModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "fs/FileManager.hpp"

namespace havel::modules {

void registerFileManagerModule(Environment &env, std::shared_ptr<IHostAPI>) {
  // Create filemanager module object
  auto filemanagerObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // Helper to convert value to string
  auto valueToString = [](const HavelValue &v) -> std::string {
    if (v.isString())
      return v.asString();
    if (v.isNumber()) {
      double val = v.asNumber();
      if (val == std::floor(val) && std::abs(val) < 1e15) {
        return std::to_string(static_cast<long long>(val));
      } else {
        std::ostringstream oss;
        oss.precision(15);
        oss << val;
        std::string s = oss.str();
        if (s.find('.') != std::string::npos) {
          size_t last = s.find_last_not_of('0');
          if (last != std::string::npos && s[last] == '.') {
            s = s.substr(0, last);
          } else if (last != std::string::npos) {
            s = s.substr(0, last + 1);
          }
        }
        return s;
      }
    }
    if (v.isBool())
      return v.asBool() ? "true" : "false";
    return "";
  };

  // =========================================================================
  // File operations
  // =========================================================================

  (*filemanagerObj)["read"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError(
              "filemanager.read() requires file path argument");
        }
        std::string path = valueToString(args[0]);
        try {
          ::FileManager file(path);
          return HavelValue(file.read());
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to read file: " +
                                   std::string(e.what()));
        }
      }));

  (*filemanagerObj)["write"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError(
              "filemanager.write() requires file path and content arguments");
        }
        std::string path = valueToString(args[0]);
        std::string content = valueToString(args[1]);
        try {
          ::FileManager file(path);
          file.write(content);
          return HavelValue(true);
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to write file: " +
                                   std::string(e.what()));
        }
      }));

  (*filemanagerObj)["append"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError(
              "filemanager.append() requires file path and content arguments");
        }
        std::string path = valueToString(args[0]);
        std::string content = valueToString(args[1]);
        try {
          ::FileManager file(path);
          file.append(content);
          return HavelValue(true);
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to append to file: " +
                                   std::string(e.what()));
        }
      }));

  (*filemanagerObj)["exists"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError(
              "filemanager.exists() requires file path argument");
        }
        std::string path = valueToString(args[0]);
        try {
          ::FileManager file(path);
          return HavelValue(file.exists());
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to check file existence: " +
                                   std::string(e.what()));
        }
      }));

  (*filemanagerObj)["delete"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError(
              "filemanager.delete() requires file path argument");
        }
        std::string path = valueToString(args[0]);
        try {
          ::FileManager file(path);
          return HavelValue(file.deleteFile());
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to delete file: " +
                                   std::string(e.what()));
        }
      }));

  (*filemanagerObj)["copy"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError(
              "filemanager.copy() requires source and destination arguments");
        }
        std::string src = valueToString(args[0]);
        std::string dst = valueToString(args[1]);
        try {
          ::FileManager file(src);
          return HavelValue(file.copy(dst));
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to copy file: " +
                                   std::string(e.what()));
        }
      }));

  (*filemanagerObj)["move"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError(
              "filemanager.move() requires source and destination arguments");
        }
        std::string src = valueToString(args[0]);
        std::string dst = valueToString(args[1]);
        try {
          ::FileManager file(src);
          return HavelValue(file.move(dst));
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to move file: " +
                                   std::string(e.what()));
        }
      }));

  (*filemanagerObj)["size"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError(
              "filemanager.size() requires file path argument");
        }
        std::string path = valueToString(args[0]);
        try {
          ::FileManager file(path);
          return HavelValue(static_cast<double>(file.size()));
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to get file size: " +
                                   std::string(e.what()));
        }
      }));

  (*filemanagerObj)["wordCount"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError(
              "filemanager.wordCount() requires file path argument");
        }
        std::string path = valueToString(args[0]);
        try {
          ::FileManager file(path);
          return HavelValue(static_cast<double>(file.wordCount()));
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to get word count: " +
                                   std::string(e.what()));
        }
      }));

  (*filemanagerObj)["lineCount"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError(
              "filemanager.lineCount() requires file path argument");
        }
        std::string path = valueToString(args[0]);
        try {
          ::FileManager file(path);
          return HavelValue(static_cast<double>(file.lineCount()));
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to get line count: " +
                                   std::string(e.what()));
        }
      }));

  (*filemanagerObj)["getChecksum"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError(
              "filemanager.getChecksum() requires file path argument");
        }
        std::string path = valueToString(args[0]);
        try {
          ::FileManager file(path);
          return HavelValue(file.getChecksum());
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to get checksum: " +
                                   std::string(e.what()));
        }
      }));

  (*filemanagerObj)["getMimeType"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError(
              "filemanager.getMimeType() requires file path argument");
        }
        std::string path = valueToString(args[0]);
        try {
          ::FileManager file(path);
          return HavelValue(file.getMimeType());
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to get MIME type: " +
                                   std::string(e.what()));
        }
      }));

  // File object constructor
  (*filemanagerObj)["File"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError(
              "filemanager.File() requires file path argument");
        }
        std::string path = valueToString(args[0]);

        auto fileObj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*fileObj)["path"] = HavelValue(path);

        (*fileObj)["read"] = HavelValue(BuiltinFunction(
            [path](const std::vector<HavelValue> &) -> HavelResult {
              try {
                ::FileManager file(path);
                return HavelValue(file.read());
              } catch (const std::exception &e) {
                return HavelRuntimeError("Failed to read file: " +
                                         std::string(e.what()));
              }
            }));

        (*fileObj)["write"] = HavelValue(BuiltinFunction(
            [path](const std::vector<HavelValue> &args) -> HavelResult {
              if (args.empty()) {
                return HavelRuntimeError(
                    "File.write() requires content argument");
              }
              std::string content =
                  args[0].isString()
                      ? args[0].asString()
                      : std::to_string(static_cast<int>(args[0].asNumber()));
              try {
                ::FileManager file(path);
                file.write(content);
                return HavelValue(true);
              } catch (const std::exception &e) {
                return HavelRuntimeError("Failed to write file: " +
                                         std::string(e.what()));
              }
            }));

        (*fileObj)["exists"] = HavelValue(BuiltinFunction(
            [path](const std::vector<HavelValue> &) -> HavelResult {
              try {
                ::FileManager file(path);
                return HavelValue(file.exists());
              } catch (const std::exception &e) {
                return HavelRuntimeError("Failed to check file existence: " +
                                         std::string(e.what()));
              }
            }));

        (*fileObj)["size"] = HavelValue(BuiltinFunction(
            [path](const std::vector<HavelValue> &) -> HavelResult {
              try {
                ::FileManager file(path);
                return HavelValue(static_cast<double>(file.size()));
              } catch (const std::exception &e) {
                return HavelRuntimeError("Failed to get file size: " +
                                         std::string(e.what()));
              }
            }));

        return HavelValue(fileObj);
      }));

  // Register filemanager module
  env.Define("filemanager", HavelValue(filemanagerObj));
}

} // namespace havel::modules
