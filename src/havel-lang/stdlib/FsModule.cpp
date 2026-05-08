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
#include <mutex>
#include <set>

#ifndef _WIN32
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <fnmatch.h>
#endif

#include "havel-lang/core/Value.hpp"

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace fs = std::filesystem;

namespace havel::stdlib {

static Value createFileObject(const fs::path &path, VMApi &api) {
  auto obj = api.makeObject();

  api.setField(obj, "name", api.makeString(path.filename().string()));
  api.setField(obj, "path", api.makeString(path.string()));

  std::string ext = path.extension().string();
  if (!ext.empty() && ext[0] == '.') 
    ext = ext.substr(1);
  api.setField(obj, "extension", api.makeString(ext));

    std::error_code ec;
    auto fsize = fs::file_size(path, ec);
  api.setField(obj, "size",
      Value::makeInt(ec ? 0 : static_cast<int64_t>(fsize)));

  auto lwt = fs::last_write_time(path, ec);
  if (!ec) {
    auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
      std::chrono::file_clock::to_sys(lwt));
    api.setField(obj, "modified",
        Value::makeInt(static_cast<int64_t>(sctp.time_since_epoch().count())));
  } else {
    api.setField(obj, "modified", Value::makeInt(0));
  }

#ifndef _WIN32
  struct stat st;
  if (::stat(path.c_str(), &st) == 0) {
    api.setField(obj, "access",
        Value::makeInt(static_cast<int64_t>(st.st_atime)));
    api.setField(obj, "birthDate",
        Value::makeInt(static_cast<int64_t>(st.st_ctime)));
    api.setField(obj, "permissions",
        Value::makeInt(static_cast<int64_t>(st.st_mode & 07777)));
  } else {
    api.setField(obj, "access", Value::makeInt(0));
    api.setField(obj, "birthDate", Value::makeInt(0));
    api.setField(obj, "permissions", Value::makeInt(0));
  }
#endif

  auto isDir = fs::is_directory(path, ec);
  api.setField(obj, "isDir", Value::makeBool(isDir));
  api.setField(obj, "isFile", Value::makeBool(!isDir));
  api.setField(obj, "isSymlink",
      Value::makeBool(fs::is_symlink(path, ec)));

  return obj;
}

static Value createStatObject(const fs::path &path, VMApi &api) {
  auto obj = api.makeObject();

#ifndef _WIN32
  struct stat st;
  if (::stat(path.c_str(), &st) != 0) {
    if (::lstat(path.c_str(), &st) != 0) {
      return Value::makeNull();
    }
  }

  api.setField(obj, "dev", Value::makeInt(static_cast<int64_t>(st.st_dev)));
  api.setField(obj, "ino", Value::makeInt(static_cast<int64_t>(st.st_ino)));
  api.setField(obj, "mode", Value::makeInt(static_cast<int64_t>(st.st_mode)));
  api.setField(obj, "nlink", Value::makeInt(static_cast<int64_t>(st.st_nlink)));
  api.setField(obj, "uid", Value::makeInt(static_cast<int64_t>(st.st_uid)));
  api.setField(obj, "gid", Value::makeInt(static_cast<int64_t>(st.st_gid)));
  api.setField(obj, "size", Value::makeInt(static_cast<int64_t>(st.st_size)));
  api.setField(obj, "atime", Value::makeInt(static_cast<int64_t>(st.st_atime)));
  api.setField(obj, "mtime", Value::makeInt(static_cast<int64_t>(st.st_mtime)));
  api.setField(obj, "ctime", Value::makeInt(static_cast<int64_t>(st.st_ctime)));

  api.setField(obj, "isFile",
      Value::makeBool(S_ISREG(st.st_mode)));
  api.setField(obj, "isDir",
      Value::makeBool(S_ISDIR(st.st_mode)));
  api.setField(obj, "isSymlink",
      Value::makeBool(S_ISLNK(st.st_mode)));
  api.setField(obj, "isCharDevice",
      Value::makeBool(S_ISCHR(st.st_mode)));
  api.setField(obj, "isBlockDevice",
      Value::makeBool(S_ISBLK(st.st_mode)));
  api.setField(obj, "isFifo",
      Value::makeBool(S_ISFIFO(st.st_mode)));
  api.setField(obj, "isSocket",
      Value::makeBool(S_ISSOCK(st.st_mode)));
  api.setField(obj, "permissions",
      Value::makeInt(static_cast<int64_t>(st.st_mode & 07777)));

  std::error_code ec;
  auto lwt = fs::last_write_time(path, ec);
  if (!ec) {
    auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
      std::chrono::file_clock::to_sys(lwt));
    api.setField(obj, "birthDate",
        Value::makeInt(static_cast<int64_t>(sctp.time_since_epoch().count())));
  } else {
    api.setField(obj, "birthDate", Value::makeInt(static_cast<int64_t>(st.st_ctime)));
  }
#endif

  return obj;
}

static void walkDir(const fs::path &dir, std::vector<fs::path> &results, VMApi &api) {
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(dir, ec)) {
        results.push_back(entry.path());
        if (entry.is_directory()) {
            walkDir(entry.path(), results, api);
        }
    }
}

static bool globMatch(const std::string &pattern, const std::string &name) {
#ifndef _WIN32
    return fnmatch(pattern.c_str(), name.c_str(), FNM_PATHNAME | FNM_PERIOD) == 0;
#else
    (void)pattern; (void)name;
    return false;
#endif
}

static void globSearch(const fs::path &dir, const std::string &pattern,
                       bool recursive, std::vector<fs::path> &results) {
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(dir, ec)) {
        std::string name = entry.path().filename().string();
        if (globMatch(pattern, name)) {
            results.push_back(entry.path());
        }
        if (entry.is_directory() && recursive) {
            globSearch(entry.path(), pattern, recursive, results);
        }
    }
}

struct FileHandle {
    int fd;
    std::string path;
    std::string mode;
    off_t pos;
    bool open;
};

static std::set<FileHandle *> &getFileHandles() {
    static std::set<FileHandle *> handles;
    return handles;
}

static FileHandle *getHandle(const Value &v, VMApi &api) {
  if (!v.isObjectId()) return nullptr;
  if (!api.hasField(v, "__handle_ptr")) return nullptr;
  auto ptrVal = api.getField(v, "__handle_ptr");
  if (!ptrVal.isInt()) return nullptr;
  return reinterpret_cast<FileHandle *>(static_cast<intptr_t>(ptrVal.asInt()));
}

static std::set<std::string> &getLockedFiles() {
    static std::set<std::string> locked;
    return locked;
}

static std::mutex &getFileLockMutex() {
    static std::mutex mtx;
    return mtx;
}

void registerFsModule(VMApi &api) {
    // fs.exists
    api.registerFunction(
        "fs.exists", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeBool(false);
            std::string path = api.resolveString(args[0]);
            return Value::makeBool(fs::exists(path));
        });

    // fs.isDir
    api.registerFunction(
        "fs.isDir", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeBool(false);
            std::string path = api.resolveString(args[0]);
            std::error_code ec;
            return Value::makeBool(fs::is_directory(path, ec));
        });

    // fs.isFile
    api.registerFunction(
        "fs.isFile", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeBool(false);
            std::string path = api.resolveString(args[0]);
            std::error_code ec;
            return Value::makeBool(fs::is_regular_file(path, ec));
        });

    // fs.isSymlink
    api.registerFunction(
        "fs.isSymlink", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeBool(false);
            std::string path = api.resolveString(args[0]);
            std::error_code ec;
            return Value::makeBool(fs::is_symlink(path, ec));
        });

    // fs.size
    api.registerFunction(
        "fs.size", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeInt(-1);
            std::string path = api.resolveString(args[0]);
            std::error_code ec;
            auto sz = fs::file_size(path, ec);
            if (ec)
                return Value::makeInt(-1);
            return Value::makeInt(static_cast<int64_t>(sz));
        });

    // fs.read
    api.registerFunction(
        "fs.read", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeNull();
            std::string path = api.resolveString(args[0]);
            std::ifstream file(path);
            if (!file.is_open())
                return Value::makeNull();
            std::stringstream ss;
            ss << file.rdbuf();
            std::string content = ss.str();
            return api.makeString(content);
        });

    // fs.readDir
    api.registerFunction(
        "fs.readDir", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeNull();
            std::string path = api.resolveString(args[0]);
            std::error_code ec;
  auto arr = api.makeArray();
  for (const auto &entry : fs::directory_iterator(path, ec)) {
    auto fileObj = createFileObject(entry.path(), api);
    api.push(arr, fileObj);
  }
  return arr;
});

    // fs.readLines
    api.registerFunction(
        "fs.readLines", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeNull();
            std::string path = api.resolveString(args[0]);
            std::ifstream file(path);
            if (!file.is_open())
                return Value::makeNull();
  auto arr = api.makeArray();
  std::string line;
  while (std::getline(file, line)) {
    api.push(arr, api.makeString(line));
  }
  return arr;
});

    // fs.write
    api.registerFunction(
        "fs.write", [&api](const std::vector<Value> &args) {
            if (args.size() < 2)
                return Value::makeBool(false);
            std::string path = api.resolveString(args[0]);
            std::string content = api.resolveString(args[1]);
            std::ofstream file(path);
            if (!file.is_open())
                return Value::makeBool(false);
            file << content;
            return Value::makeBool(true);
        });

    // fs.append
    api.registerFunction(
        "fs.append", [&api](const std::vector<Value> &args) {
            if (args.size() < 2)
                return Value::makeBool(false);
            std::string path = api.resolveString(args[0]);
            std::string content = api.resolveString(args[1]);
            std::ofstream file(path, std::ios::app);
            if (!file.is_open())
                return Value::makeBool(false);
            file << content;
            return Value::makeBool(true);
        });

    // fs.touch
    api.registerFunction(
        "fs.touch", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeNull();
            std::string path = api.resolveString(args[0]);
            std::ofstream file(path, std::ios::app);
            if (!file.is_open())
                return Value::makeNull();
            return createFileObject(path, api);
        });

    // fs.mkdir
    api.registerFunction(
        "fs.mkdir", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeBool(false);
            std::string path = api.resolveString(args[0]);
            std::error_code ec;
            return Value::makeBool(fs::create_directory(path, ec));
        });

    // fs.mkdirAll
    api.registerFunction(
        "fs.mkdirAll", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeBool(false);
            std::string path = api.resolveString(args[0]);
            std::error_code ec;
            fs::create_directories(path, ec);
            return Value::makeBool(!ec);
        });

    // fs.delete (legacy name)
    api.registerFunction(
        "fs.delete", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeBool(false);
            std::string path = api.resolveString(args[0]);
            std::error_code ec;
            return Value::makeBool(fs::remove(path, ec));
        });

    // fs.rm (same as delete, more conventional)
    api.registerFunction(
        "fs.rm", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeBool(false);
            std::string path = api.resolveString(args[0]);
            std::error_code ec;
            return Value::makeBool(fs::remove(path, ec));
        });

    // fs.copy
    api.registerFunction(
        "fs.copy", [&api](const std::vector<Value> &args) {
            if (args.size() < 2)
                return Value::makeBool(false);
            std::string src = api.resolveString(args[0]);
            std::string dst = api.resolveString(args[1]);
            std::error_code ec;
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            return Value::makeBool(!ec);
        });

    // fs.copyDir (recursive directory copy)
    api.registerFunction(
        "fs.copyDir", [&api](const std::vector<Value> &args) {
            if (args.size() < 2)
                return Value::makeBool(false);
            std::string src = api.resolveString(args[0]);
            std::string dst = api.resolveString(args[1]);
            std::error_code ec;
            fs::copy(src, dst, fs::copy_options::recursive, ec);
            return Value::makeBool(!ec);
        });

    // fs.move
    api.registerFunction(
        "fs.move", [&api](const std::vector<Value> &args) {
            if (args.size() < 2)
                return Value::makeBool(false);
            std::string src = api.resolveString(args[0]);
            std::string dst = api.resolveString(args[1]);
            std::error_code ec;
            fs::rename(src, dst, ec);
            return Value::makeBool(!ec);
        });

    // fs.rename (same as move, explicit name)
    api.registerFunction(
        "fs.rename", [&api](const std::vector<Value> &args) {
            if (args.size() < 2)
                return Value::makeBool(false);
            std::string src = api.resolveString(args[0]);
            std::string dst = api.resolveString(args[1]);
            std::error_code ec;
            fs::rename(src, dst, ec);
            return Value::makeBool(!ec);
        });

    // fs.rmdir (directory removal, recursive if second arg is true)
    api.registerFunction(
        "fs.rmdir", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeBool(false);
            std::string path = api.resolveString(args[0]);
            std::error_code ec;
            bool recursive = args.size() > 1 && api.toBool(args[1]);
            if (recursive) {
                fs::remove_all(path, ec);
                return Value::makeBool(!ec);
            }
            return Value::makeBool(fs::remove(path, ec));
        });

    // fs.stat (detailed file metadata)
    api.registerFunction(
        "fs.stat", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeNull();
            std::string path = api.resolveString(args[0]);
            return createStatObject(path, api);
        });

    // fs.symlink (create symbolic link)
    api.registerFunction(
        "fs.symlink", [&api](const std::vector<Value> &args) {
            if (args.size() < 2)
                return Value::makeBool(false);
            std::string target = api.resolveString(args[0]);
            std::string link = api.resolveString(args[1]);
            std::error_code ec;
            fs::create_symlink(target, link, ec);
            return Value::makeBool(!ec);
        });

    // fs.readlink (read symbolic link target)
    api.registerFunction(
        "fs.readlink", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeNull();
            std::string path = api.resolveString(args[0]);
            std::error_code ec;
            auto target = fs::read_symlink(path, ec);
            if (ec)
                return Value::makeNull();
            return api.makeString(target.string());
        });

    // fs.chmod
    api.registerFunction(
        "fs.chmod", [&api](const std::vector<Value> &args) {
            if (args.size() < 2)
                return Value::makeBool(false);
            std::string path = api.resolveString(args[0]);
            int mode = 0644;
            if (args[1].isInt()) {
                mode = static_cast<int>(args[1].asInt());
            } else if (args[1].isDouble()) {
                mode = static_cast<int>(args[1].asDouble());
            }
#ifndef _WIN32
            int ret = ::chmod(path.c_str(), static_cast<mode_t>(mode));
            return Value::makeBool(ret == 0);
#else
            (void)path; (void)mode;
            return Value::makeBool(false);
#endif
        });

    // fs.walk (recursive directory walk, returns flat array of paths)
    api.registerFunction(
        "fs.walk", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeNull();
            std::string path = api.resolveString(args[0]);
            std::error_code ec;
            if (!fs::is_directory(path, ec))
                return Value::makeNull();
  std::vector<fs::path> results;
  walkDir(path, results, api);
  auto arr = api.makeArray();
  for (const auto &p : results) {
    api.push(arr, api.makeString(p.string()));
  }
  return arr;
});

// fs.traverse (same as walk but returns FileObjects instead of strings)
  api.registerFunction(
  "fs.traverse", [&api](const std::vector<Value> &args) {
  if (args.empty())
    return Value::makeNull();
  std::string path = api.resolveString(args[0]);
  std::error_code ec;
  if (!fs::is_directory(path, ec))
    return Value::makeNull();
  std::vector<fs::path> results;
  walkDir(path, results, api);
  auto arr2 = api.makeArray();
  for (const auto &p : results) {
    auto fileObj = createFileObject(p, api);
    api.push(arr2, fileObj);
  }
  return arr2;
});

    // fs.glob (pattern matching with * and **)
    api.registerFunction(
        "fs.glob", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeNull();
            std::string pattern = api.resolveString(args[0]);
            std::string dir = ".";
            if (args.size() > 1) {
                dir = api.resolveString(args[1]);
            }
            bool recursive = pattern.find("**") != std::string::npos;
            std::vector<fs::path> results;
            std::error_code ec;
            if (!fs::is_directory(dir, ec))
                return Value::makeNull();
            std::string basePattern = pattern;
            if (recursive) {
                size_t pos = pattern.find("**/");
                if (pos != std::string::npos) {
                    basePattern = pattern.substr(pos + 3);
                    if (basePattern.empty())
                        basePattern = "*";
                }
            }
  globSearch(dir, basePattern, recursive, results);
  auto arr = api.makeArray();
  for (const auto &p : results) {
    api.push(arr, api.makeString(p.string()));
  }
  return arr;
});

    // fs.watch (inotify-based directory watcher)
    api.registerFunction(
        "fs.watch", [&api](const std::vector<Value> &args) {
#ifndef _WIN32
            if (args.size() < 2)
                return Value::makeNull();
            std::string path = api.resolveString(args[0]);
            if (!args[1].isFunctionObjId() && !args[1].isHostFuncId() && !args[1].isClosureId())
                return Value::makeNull();
            auto callback = args[1];
            int inotifyFd = inotify_init1(IN_NONBLOCK);
            if (inotifyFd < 0)
                return Value::makeNull();
            int wd = inotify_add_watch(inotifyFd, path.c_str(),
                IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO);
            if (wd < 0) {
                close(inotifyFd);
                return Value::makeNull();
            }
            auto watchObj = api.makeObject();
            api.setField(watchObj, "fd", api.makeNumber(static_cast<int64_t>(inotifyFd)));
            api.setField(watchObj, "wd", api.makeNumber(static_cast<int64_t>(wd)));
            api.setField(watchObj, "path", api.makeString(path));
            api.setField(watchObj, "callback", callback);
            api.setField(watchObj, "close", api.makeFunctionRef("fs._watchClose"));
  api.registerFunction("fs._watchClose_" + std::to_string(inotifyFd),
                [&api, inotifyFd](const std::vector<Value> &) {
                    close(inotifyFd);
                    return Value::makeBool(true);
                });
            return watchObj;
#else
            (void)args;
            return Value::makeNull();
#endif
        });

    // fs._watchClose (close watch handle)
    api.registerFunction(
        "fs._watchClose", [&api](const std::vector<Value> &args) {
#ifndef _WIN32
            if (args.empty() || !args[0].isObjectId())
                return Value::makeBool(false);
  auto fdVal = api.getField(args[0], "fd");
            if (!fdVal.isInt() && !fdVal.isDouble())
                return Value::makeBool(false);
            int fd = fdVal.isInt() ? static_cast<int>(fdVal.asInt())
                                   : static_cast<int>(fdVal.asDouble());
            close(fd);
            return Value::makeBool(true);
#else
            (void)args;
            return Value::makeBool(false);
#endif
        });

    // fs.watchTree (recursive directory watcher)
    api.registerFunction(
        "fs.watchTree", [&api](const std::vector<Value> &args) {
#ifndef _WIN32
            if (args.size() < 2)
                return Value::makeNull();
            std::string path = api.resolveString(args[0]);
            if (!args[1].isFunctionObjId() && !args[1].isHostFuncId() && !args[1].isClosureId())
                return Value::makeNull();
            auto callback = args[1];
            int inotifyFd = inotify_init1(IN_NONBLOCK);
            if (inotifyFd < 0)
                return Value::makeNull();
            std::vector<int> wds;
            wds.push_back(inotify_add_watch(inotifyFd, path.c_str(),
                IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO));
            std::error_code ec;
            for (const auto &entry : fs::recursive_directory_iterator(path, ec)) {
                if (entry.is_directory()) {
                    wds.push_back(inotify_add_watch(inotifyFd, entry.path().c_str(),
                        IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO));
                }
            }
            auto watchObj = api.makeObject();
            api.setField(watchObj, "fd", api.makeNumber(static_cast<int64_t>(inotifyFd)));
            api.setField(watchObj, "path", api.makeString(path));
            api.setField(watchObj, "callback", callback);
            api.setField(watchObj, "close", api.makeFunctionRef("fs._watchClose"));
            return watchObj;
#else
            (void)args;
            return Value::makeNull();
#endif
        });

    // fs.open (file handle: r, w, w+, a)
    api.registerFunction(
        "fs.open", [&api](const std::vector<Value> &args) {
#ifndef _WIN32
            if (args.empty())
                return Value::makeNull();
            std::string path = api.resolveString(args[0]);
            std::string mode = "r";
            if (args.size() > 1) {
                mode = api.resolveString(args[1]);
            }
            int flags;
            if (mode == "r") {
                flags = O_RDONLY;
            } else if (mode == "w") {
                flags = O_WRONLY | O_CREAT | O_TRUNC;
            } else if (mode == "w+") {
                flags = O_RDWR | O_CREAT | O_TRUNC;
            } else if (mode == "a") {
                flags = O_WRONLY | O_CREAT | O_APPEND;
            } else {
                flags = O_RDONLY;
            }
            int fd = ::open(path.c_str(), flags, 0644);
            if (fd < 0)
                return Value::makeNull();

            auto *handle = new FileHandle{fd, path, mode, 0, true};
            getFileHandles().insert(handle);

            auto handleObj = api.makeObject();
            api.setField(handleObj, "name", api.makeString(path));
            api.setField(handleObj, "path", api.makeString(path));
            api.setField(handleObj, "mode", api.makeString(mode));
            api.setField(handleObj, "__handle_ptr",
                api.makeNumber(static_cast<int64_t>(reinterpret_cast<intptr_t>(handle))));

            api.setField(handleObj, "read", api.makeFunctionRef("fs._handleRead"));
            api.setField(handleObj, "write", api.makeFunctionRef("fs._handleWrite"));
            api.setField(handleObj, "prepend", api.makeFunctionRef("fs._handlePrepend"));
            api.setField(handleObj, "append", api.makeFunctionRef("fs._handleAppend"));
            api.setField(handleObj, "seek", api.makeFunctionRef("fs._handleSeek"));
            api.setField(handleObj, "clear", api.makeFunctionRef("fs._handleClear"));
            api.setField(handleObj, "flush", api.makeFunctionRef("fs._handleFlush"));
            api.setField(handleObj, "close", api.makeFunctionRef("fs._handleClose"));
            api.setField(handleObj, "remove", api.makeFunctionRef("fs._handleRemove"));
            return handleObj;
#else
            (void)args;
            return Value::makeNull();
#endif
        });

    // File handle: read(n)
    api.registerFunction(
        "fs._handleRead", [&api](const std::vector<Value> &args) {
#ifndef _WIN32
            if (args.empty() || !args[0].isObjectId())
                return Value::makeNull();
            auto *handle = getHandle(args[0], api);
            if (!handle || !handle->open)
                return Value::makeNull();
            ssize_t n = 4096;
            if (args.size() > 1) {
                if (args[1].isInt()) n = args[1].asInt();
                else if (args[1].isDouble()) n = static_cast<ssize_t>(args[1].asDouble());
            }
            if (n <= 0) {
                struct stat st;
                if (::fstat(handle->fd, &st) == 0)
                    n = st.st_size;
                else
                    n = 4096;
            }
            std::vector<char> buf(n);
            ssize_t bytesRead = ::read(handle->fd, buf.data(), n);
            if (bytesRead < 0)
                return Value::makeNull();
            return api.makeString(std::string(buf.data(), bytesRead));
#else
            (void)args;
            return Value::makeNull();
#endif
        });

    // File handle: write(data)
    api.registerFunction(
        "fs._handleWrite", [&api](const std::vector<Value> &args) {
#ifndef _WIN32
            if (args.size() < 2 || !args[0].isObjectId())
                return Value::makeBool(false);
            auto *handle = getHandle(args[0], api);
            if (!handle || !handle->open)
                return Value::makeBool(false);
            std::string data = api.resolveString(args[1]);
            ssize_t written = ::write(handle->fd, data.c_str(), data.size());
            return Value::makeBool(written >= 0);
#else
            (void)args;
            return Value::makeBool(false);
#endif
        });

    // File handle: prepend(data)
    api.registerFunction(
        "fs._handlePrepend", [&api](const std::vector<Value> &args) {
            if (args.size() < 2 || !args[0].isObjectId())
                return Value::makeBool(false);
            auto *handle = getHandle(args[0], api);
            if (!handle || !handle->open)
                return Value::makeBool(false);
            std::string data = api.resolveString(args[1]);
  std::string existing = api.resolveString(
      api.getField(args[0], "path"));
            std::ifstream ifs(existing);
            if (!ifs.is_open())
                return Value::makeBool(false);
            std::string content((std::istreambuf_iterator<char>(ifs)),
                                std::istreambuf_iterator<char>());
            ifs.close();
            std::ofstream ofs(existing, std::ios::trunc);
            if (!ofs.is_open())
                return Value::makeBool(false);
            ofs << data << content;
            return Value::makeBool(true);
        });

    // File handle: append(data)
    api.registerFunction(
        "fs._handleAppend", [&api](const std::vector<Value> &args) {
#ifndef _WIN32
            if (args.size() < 2 || !args[0].isObjectId())
                return Value::makeBool(false);
            auto *handle = getHandle(args[0], api);
            if (!handle || !handle->open)
                return Value::makeBool(false);
            std::string data = api.resolveString(args[1]);
            ::lseek(handle->fd, 0, SEEK_END);
            ssize_t written = ::write(handle->fd, data.c_str(), data.size());
            return Value::makeBool(written >= 0);
#else
            (void)args;
            return Value::makeBool(false);
#endif
        });

    // File handle: seek(position)
    api.registerFunction(
        "fs._handleSeek", [&api](const std::vector<Value> &args) {
#ifndef _WIN32
            if (args.empty() || !args[0].isObjectId())
                return Value::makeBool(false);
            auto *handle = getHandle(args[0], api);
            if (!handle || !handle->open)
                return Value::makeBool(false);
            off_t pos = 0;
            if (args.size() > 1) {
                if (args[1].isInt()) pos = static_cast<off_t>(args[1].asInt());
                else if (args[1].isDouble()) pos = static_cast<off_t>(args[1].asDouble());
            }
            handle->pos = ::lseek(handle->fd, pos, SEEK_SET);
            return Value::makeBool(true);
#else
            (void)args;
            return Value::makeBool(false);
#endif
        });

    // File handle: clear()
    api.registerFunction(
        "fs._handleClear", [&api](const std::vector<Value> &args) {
#ifndef _WIN32
            if (args.empty() || !args[0].isObjectId())
                return Value::makeBool(false);
            auto *handle = getHandle(args[0], api);
            if (!handle || !handle->open)
                return Value::makeBool(false);
            if (::ftruncate(handle->fd, 0) != 0)
                return Value::makeBool(false);
            ::lseek(handle->fd, 0, SEEK_SET);
            return Value::makeBool(true);
#else
            (void)args;
            return Value::makeBool(false);
#endif
        });

    // File handle: flush()
    api.registerFunction(
        "fs._handleFlush", [&api](const std::vector<Value> &args) {
#ifndef _WIN32
            if (args.empty() || !args[0].isObjectId())
                return Value::makeBool(false);
            auto *handle = getHandle(args[0], api);
            if (!handle || !handle->open)
                return Value::makeBool(false);
            return Value::makeBool(::fsync(handle->fd) == 0);
#else
            (void)args;
            return Value::makeBool(false);
#endif
        });

    // File handle: close()
    api.registerFunction(
        "fs._handleClose", [&api](const std::vector<Value> &args) {
#ifndef _WIN32
            if (args.empty() || !args[0].isObjectId())
                return Value::makeBool(false);
            auto *handle = getHandle(args[0], api);
            if (!handle || !handle->open)
                return Value::makeBool(false);
            ::close(handle->fd);
  handle->open = false;
  getFileHandles().erase(handle);
  delete handle;
  api.setField(args[0], "__handle_ptr", Value::makeInt(0));
  return Value::makeBool(true);
#else
            (void)args;
            return Value::makeBool(false);
#endif
        });

    // File handle: remove() (close and delete file)
    api.registerFunction(
        "fs._handleRemove", [&api](const std::vector<Value> &args) {
#ifndef _WIN32
            if (args.empty() || !args[0].isObjectId())
                return Value::makeBool(false);
            auto *handle = getHandle(args[0], api);
            if (!handle)
                return Value::makeBool(false);
            std::string path = handle->path;
            if (handle->open) {
                ::close(handle->fd);
                handle->open = false;
            }
            getFileHandles().erase(handle);
            delete handle;
            std::error_code ec;
            return Value::makeBool(fs::remove(path, ec));
#else
            (void)args;
            return Value::makeBool(false);
#endif
        });

    // fs.atomicWrite (write to temp then rename)
    api.registerFunction(
        "fs.atomicWrite", [&api](const std::vector<Value> &args) {
            if (args.size() < 2)
                return Value::makeBool(false);
            std::string path = api.resolveString(args[0]);
            std::string content = api.resolveString(args[1]);
            std::string tmpPath = path + ".tmp." + std::to_string(::getpid());
            {
                std::ofstream file(tmpPath);
                if (!file.is_open())
                    return Value::makeBool(false);
                file << content;
                if (!file.flush())
                    return Value::makeBool(false);
            }
            std::error_code ec;
            fs::rename(tmpPath, path, ec);
            return Value::makeBool(!ec);
        });

    // fs.tempFile (create temporary file, returns {path, fd})
    api.registerFunction(
        "fs.tempFile", [&api](const std::vector<Value> &args) {
#ifndef _WIN32
            std::string tmpl = "/tmp/havel_XXXXXX";
            if (!args.empty()) {
                tmpl = api.resolveString(args[0]);
            }
            std::vector<char> tmplBuf(tmpl.begin(), tmpl.end());
            tmplBuf.push_back('\0');
            int fd = ::mkstemp(tmplBuf.data());
            if (fd < 0)
                return Value::makeNull();
            std::string resultPath(tmplBuf.data());
            auto obj = api.makeObject();
            api.setField(obj, "path", api.makeString(resultPath));
            api.setField(obj, "fd", api.makeNumber(static_cast<int64_t>(fd)));
            api.setField(obj, "read", api.makeFunctionRef("fs._handleRead"));
            api.setField(obj, "write", api.makeFunctionRef("fs._handleWrite"));
            api.setField(obj, "close", api.makeFunctionRef("fs._handleClose"));

            auto *handle = new FileHandle{fd, resultPath, "w+", 0, true};
            getFileHandles().insert(handle);
            api.setField(obj, "__handle_ptr",
                api.makeNumber(reinterpret_cast<intptr_t>(handle)));
            return obj;
#else
            (void)args;
            return Value::makeNull();
#endif
        });

    // fs.lock (advisory file lock via flock)
    api.registerFunction(
        "fs.lock", [&api](const std::vector<Value> &args) {
#ifndef _WIN32
            if (args.empty())
                return Value::makeBool(false);
            std::string path = api.resolveString(args[0]);
            int fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
            if (fd < 0)
                return Value::makeBool(false);
            int ret = ::flock(fd, LOCK_EX);
            if (ret != 0) {
                ::close(fd);
                return Value::makeBool(false);
            }
            std::lock_guard<std::mutex> lk(getFileLockMutex());
            getLockedFiles().insert(path);
            ::close(fd);
            return Value::makeBool(true);
#else
            (void)args;
            return Value::makeBool(false);
#endif
        });

    // fs.tryLock (non-blocking advisory file lock)
    api.registerFunction(
        "fs.tryLock", [&api](const std::vector<Value> &args) {
#ifndef _WIN32
            if (args.empty())
                return Value::makeBool(false);
            std::string path = api.resolveString(args[0]);
            int fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
            if (fd < 0)
                return Value::makeBool(false);
            int ret = ::flock(fd, LOCK_EX | LOCK_NB);
            if (ret != 0) {
                ::close(fd);
                return Value::makeBool(false);
            }
            std::lock_guard<std::mutex> lk(getFileLockMutex());
            getLockedFiles().insert(path);
            ::close(fd);
            return Value::makeBool(true);
#else
            (void)args;
            return Value::makeBool(false);
#endif
        });

    // fs.isLocked
    api.registerFunction(
        "fs.isLocked", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeBool(false);
            std::string path = api.resolveString(args[0]);
            std::lock_guard<std::mutex> lk(getFileLockMutex());
            return Value::makeBool(getLockedFiles().count(path) > 0);
        });

    // Create fs namespace object
    auto fsObj = api.makeObject();
    api.setField(fsObj, "exists", api.makeFunctionRef("fs.exists"));
    api.setField(fsObj, "isDir", api.makeFunctionRef("fs.isDir"));
    api.setField(fsObj, "isFile", api.makeFunctionRef("fs.isFile"));
    api.setField(fsObj, "isSymlink", api.makeFunctionRef("fs.isSymlink"));
    api.setField(fsObj, "size", api.makeFunctionRef("fs.size"));
    api.setField(fsObj, "read", api.makeFunctionRef("fs.read"));
    api.setField(fsObj, "readDir", api.makeFunctionRef("fs.readDir"));
    api.setField(fsObj, "readLines", api.makeFunctionRef("fs.readLines"));
    api.setField(fsObj, "write", api.makeFunctionRef("fs.write"));
    api.setField(fsObj, "append", api.makeFunctionRef("fs.append"));
    api.setField(fsObj, "touch", api.makeFunctionRef("fs.touch"));
    api.setField(fsObj, "mkdir", api.makeFunctionRef("fs.mkdir"));
    api.setField(fsObj, "mkdirAll", api.makeFunctionRef("fs.mkdirAll"));
    api.setField(fsObj, "delete", api.makeFunctionRef("fs.delete"));
    api.setField(fsObj, "rm", api.makeFunctionRef("fs.rm"));
    api.setField(fsObj, "copy", api.makeFunctionRef("fs.copy"));
    api.setField(fsObj, "copyDir", api.makeFunctionRef("fs.copyDir"));
    api.setField(fsObj, "move", api.makeFunctionRef("fs.move"));
    api.setField(fsObj, "rename", api.makeFunctionRef("fs.rename"));
    api.setField(fsObj, "rmdir", api.makeFunctionRef("fs.rmdir"));
    api.setField(fsObj, "stat", api.makeFunctionRef("fs.stat"));
    api.setField(fsObj, "symlink", api.makeFunctionRef("fs.symlink"));
    api.setField(fsObj, "readlink", api.makeFunctionRef("fs.readlink"));
    api.setField(fsObj, "chmod", api.makeFunctionRef("fs.chmod"));
    api.setField(fsObj, "walk", api.makeFunctionRef("fs.walk"));
    api.setField(fsObj, "traverse", api.makeFunctionRef("fs.traverse"));
    api.setField(fsObj, "glob", api.makeFunctionRef("fs.glob"));
    api.setField(fsObj, "watch", api.makeFunctionRef("fs.watch"));
    api.setField(fsObj, "watchTree", api.makeFunctionRef("fs.watchTree"));
    api.setField(fsObj, "open", api.makeFunctionRef("fs.open"));
    api.setField(fsObj, "atomicWrite", api.makeFunctionRef("fs.atomicWrite"));
    api.setField(fsObj, "tempFile", api.makeFunctionRef("fs.tempFile"));
    api.setField(fsObj, "lock", api.makeFunctionRef("fs.lock"));
    api.setField(fsObj, "tryLock", api.makeFunctionRef("fs.tryLock"));
    api.setField(fsObj, "isLocked", api.makeFunctionRef("fs.isLocked"));
    api.setGlobal("fs", fsObj);
}

} // namespace havel::stdlib
