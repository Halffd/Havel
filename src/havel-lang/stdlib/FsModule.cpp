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
#include "havel-lang/compiler/vm/VM.hpp"

using havel::compiler::Value;
using havel::compiler::VMApi;
using havel::compiler::ObjectRef;

namespace fs = std::filesystem;

namespace havel::stdlib {

static std::string valueToString(const Value &v, VMApi &api) {
    return api.vm.resolveStringKey(v);
}

static Value makeStringId(const std::string &s, VMApi &api) {
    auto strRef = api.vm.getHeap().allocateString(s);
    return Value::makeStringId(strRef.id);
}

static Value createFileObject(const fs::path &path, VMApi &api) {
    auto &vm = api.vm;
    auto objRef = vm.createHostObject();

    vm.setHostObjectField(objRef, "name", makeStringId(path.filename().string(), api));
    vm.setHostObjectField(objRef, "path", makeStringId(path.string(), api));

    std::string ext = path.extension().string();
    if (!ext.empty() && ext[0] == '.')
        ext = ext.substr(1);
    vm.setHostObjectField(objRef, "extension", makeStringId(ext, api));

    std::error_code ec;
    auto fsize = fs::file_size(path, ec);
    vm.setHostObjectField(objRef, "size",
        Value::makeInt(ec ? 0 : static_cast<int64_t>(fsize)));

    auto ctime = fs::last_write_time(path, ec);
    if (!ec) {
        auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
            std::chrono::file_clock::to_sys(ctime));
        vm.setHostObjectField(objRef, "modified",
            Value::makeInt(static_cast<int64_t>(sctp.time_since_epoch().count())));
    } else {
        vm.setHostObjectField(objRef, "modified", Value::makeInt(0));
    }

    struct stat st;
    if (::stat(path.c_str(), &st) == 0) {
        vm.setHostObjectField(objRef, "access",
            Value::makeInt(static_cast<int64_t>(st.st_atime)));
        vm.setHostObjectField(objRef, "birthDate",
            Value::makeInt(static_cast<int64_t>(st.st_ctime)));
        vm.setHostObjectField(objRef, "permissions",
            Value::makeInt(static_cast<int64_t>(st.st_mode & 07777)));
    } else {
        vm.setHostObjectField(objRef, "access", Value::makeInt(0));
        vm.setHostObjectField(objRef, "birthDate", Value::makeInt(0));
        vm.setHostObjectField(objRef, "permissions", Value::makeInt(0));
    }

    auto isDir = fs::is_directory(path, ec);
    vm.setHostObjectField(objRef, "isDir", Value::makeBool(isDir));
    vm.setHostObjectField(objRef, "isFile", Value::makeBool(!isDir));
    vm.setHostObjectField(objRef, "isSymlink",
        Value::makeBool(fs::is_symlink(path, ec)));

    return Value::makeObjectId(objRef.id);
}

static Value createStatObject(const fs::path &path, VMApi &api) {
    auto &vm = api.vm;
    auto objRef = vm.createHostObject();

    struct stat st;
    if (::stat(path.c_str(), &st) != 0) {
        if (::lstat(path.c_str(), &st) != 0) {
            return Value::makeNull();
        }
    }

    vm.setHostObjectField(objRef, "dev", Value::makeInt(static_cast<int64_t>(st.st_dev)));
    vm.setHostObjectField(objRef, "ino", Value::makeInt(static_cast<int64_t>(st.st_ino)));
    vm.setHostObjectField(objRef, "mode", Value::makeInt(static_cast<int64_t>(st.st_mode)));
    vm.setHostObjectField(objRef, "nlink", Value::makeInt(static_cast<int64_t>(st.st_nlink)));
    vm.setHostObjectField(objRef, "uid", Value::makeInt(static_cast<int64_t>(st.st_uid)));
    vm.setHostObjectField(objRef, "gid", Value::makeInt(static_cast<int64_t>(st.st_gid)));
    vm.setHostObjectField(objRef, "size", Value::makeInt(static_cast<int64_t>(st.st_size)));
    vm.setHostObjectField(objRef, "atime", Value::makeInt(static_cast<int64_t>(st.st_atime)));
    vm.setHostObjectField(objRef, "mtime", Value::makeInt(static_cast<int64_t>(st.st_mtime)));
    vm.setHostObjectField(objRef, "ctime", Value::makeInt(static_cast<int64_t>(st.st_ctime)));

    vm.setHostObjectField(objRef, "isFile",
        Value::makeBool(S_ISREG(st.st_mode)));
    vm.setHostObjectField(objRef, "isDir",
        Value::makeBool(S_ISDIR(st.st_mode)));
    vm.setHostObjectField(objRef, "isSymlink",
        Value::makeBool(S_ISLNK(st.st_mode)));
    vm.setHostObjectField(objRef, "isCharDevice",
        Value::makeBool(S_ISCHR(st.st_mode)));
    vm.setHostObjectField(objRef, "isBlockDevice",
        Value::makeBool(S_ISBLK(st.st_mode)));
    vm.setHostObjectField(objRef, "isFifo",
        Value::makeBool(S_ISFIFO(st.st_mode)));
    vm.setHostObjectField(objRef, "isSocket",
        Value::makeBool(S_ISSOCK(st.st_mode)));
    vm.setHostObjectField(objRef, "permissions",
        Value::makeInt(static_cast<int64_t>(st.st_mode & 07777)));

    std::error_code ec;
    auto lwt = fs::last_write_time(path, ec);
    if (!ec) {
        auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
            std::chrono::file_clock::to_sys(lwt));
        vm.setHostObjectField(objRef, "birthDate",
            Value::makeInt(static_cast<int64_t>(sctp.time_since_epoch().count())));
    } else {
        vm.setHostObjectField(objRef, "birthDate", Value::makeInt(static_cast<int64_t>(st.st_ctime)));
    }

    return Value::makeObjectId(objRef.id);
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
    return fnmatch(pattern.c_str(), name.c_str(), FNM_PATHNAME | FNM_PERIOD) == 0;
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
    auto &vm = api.vm;
    ObjectRef ref{v.asObjectId(), true};
    if (!vm.hasHostObjectField(ref, "__handle_ptr")) return nullptr;
    auto ptrVal = vm.getHostObjectField(ref, "__handle_ptr");
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
            std::string path = valueToString(args[0], api);
            return Value::makeBool(fs::exists(path));
        });

    // fs.isDir
    api.registerFunction(
        "fs.isDir", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeBool(false);
            std::string path = valueToString(args[0], api);
            std::error_code ec;
            return Value::makeBool(fs::is_directory(path, ec));
        });

    // fs.isFile
    api.registerFunction(
        "fs.isFile", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeBool(false);
            std::string path = valueToString(args[0], api);
            std::error_code ec;
            return Value::makeBool(fs::is_regular_file(path, ec));
        });

    // fs.isSymlink
    api.registerFunction(
        "fs.isSymlink", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeBool(false);
            std::string path = valueToString(args[0], api);
            std::error_code ec;
            return Value::makeBool(fs::is_symlink(path, ec));
        });

    // fs.size
    api.registerFunction(
        "fs.size", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeInt(-1);
            std::string path = valueToString(args[0], api);
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
            std::string path = valueToString(args[0], api);
            std::ifstream file(path);
            if (!file.is_open())
                return Value::makeNull();
            std::stringstream ss;
            ss << file.rdbuf();
            std::string content = ss.str();
            return makeStringId(content, api);
        });

    // fs.readDir
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

    // fs.readLines
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

    // fs.write
    api.registerFunction(
        "fs.write", [&api](const std::vector<Value> &args) {
            if (args.size() < 2)
                return Value::makeBool(false);
            std::string path = valueToString(args[0], api);
            std::string content = valueToString(args[1], api);
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
            std::string path = valueToString(args[0], api);
            std::string content = valueToString(args[1], api);
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
            std::string path = valueToString(args[0], api);
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
            std::string path = valueToString(args[0], api);
            std::error_code ec;
            return Value::makeBool(fs::create_directory(path, ec));
        });

    // fs.mkdirAll
    api.registerFunction(
        "fs.mkdirAll", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeBool(false);
            std::string path = valueToString(args[0], api);
            std::error_code ec;
            fs::create_directories(path, ec);
            return Value::makeBool(!ec);
        });

    // fs.delete (legacy name)
    api.registerFunction(
        "fs.delete", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeBool(false);
            std::string path = valueToString(args[0], api);
            std::error_code ec;
            return Value::makeBool(fs::remove(path, ec));
        });

    // fs.rm (same as delete, more conventional)
    api.registerFunction(
        "fs.rm", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeBool(false);
            std::string path = valueToString(args[0], api);
            std::error_code ec;
            return Value::makeBool(fs::remove(path, ec));
        });

    // fs.copy
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

    // fs.copyDir (recursive directory copy)
    api.registerFunction(
        "fs.copyDir", [&api](const std::vector<Value> &args) {
            if (args.size() < 2)
                return Value::makeBool(false);
            std::string src = valueToString(args[0], api);
            std::string dst = valueToString(args[1], api);
            std::error_code ec;
            fs::copy(src, dst, fs::copy_options::recursive, ec);
            return Value::makeBool(!ec);
        });

    // fs.move
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

    // fs.rename (same as move, explicit name)
    api.registerFunction(
        "fs.rename", [&api](const std::vector<Value> &args) {
            if (args.size() < 2)
                return Value::makeBool(false);
            std::string src = valueToString(args[0], api);
            std::string dst = valueToString(args[1], api);
            std::error_code ec;
            fs::rename(src, dst, ec);
            return Value::makeBool(!ec);
        });

    // fs.rmdir (directory removal, recursive if second arg is true)
    api.registerFunction(
        "fs.rmdir", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeBool(false);
            std::string path = valueToString(args[0], api);
            std::error_code ec;
            bool recursive = args.size() > 1 && api.vm.toBoolPublic(args[1]);
            if (recursive) {
                auto count = fs::remove_all(path, ec);
                return Value::makeBool(!ec);
            }
            return Value::makeBool(fs::remove(path, ec));
        });

    // fs.stat (detailed file metadata)
    api.registerFunction(
        "fs.stat", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeNull();
            std::string path = valueToString(args[0], api);
            return createStatObject(path, api);
        });

    // fs.symlink (create symbolic link)
    api.registerFunction(
        "fs.symlink", [&api](const std::vector<Value> &args) {
            if (args.size() < 2)
                return Value::makeBool(false);
            std::string target = valueToString(args[0], api);
            std::string link = valueToString(args[1], api);
            std::error_code ec;
            fs::create_symlink(target, link, ec);
            return Value::makeBool(!ec);
        });

    // fs.readlink (read symbolic link target)
    api.registerFunction(
        "fs.readlink", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeNull();
            std::string path = valueToString(args[0], api);
            std::error_code ec;
            auto target = fs::read_symlink(path, ec);
            if (ec)
                return Value::makeNull();
            return makeStringId(target.string(), api);
        });

    // fs.chmod
    api.registerFunction(
        "fs.chmod", [&api](const std::vector<Value> &args) {
            if (args.size() < 2)
                return Value::makeBool(false);
            std::string path = valueToString(args[0], api);
            int mode = 0644;
            if (args[1].isInt()) {
                mode = static_cast<int>(args[1].asInt());
            } else if (args[1].isDouble()) {
                mode = static_cast<int>(args[1].asDouble());
            }
            int ret = ::chmod(path.c_str(), static_cast<mode_t>(mode));
            return Value::makeBool(ret == 0);
        });

    // fs.walk (recursive directory walk, returns flat array of paths)
    api.registerFunction(
        "fs.walk", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeNull();
            std::string path = valueToString(args[0], api);
            std::error_code ec;
            if (!fs::is_directory(path, ec))
                return Value::makeNull();
            std::vector<fs::path> results;
            walkDir(path, results, api);
            auto arrRef = api.vm.createHostArray();
            for (const auto &p : results) {
                api.vm.pushHostArrayValue(arrRef, makeStringId(p.string(), api));
            }
            return Value::makeArrayId(arrRef.id);
        });

    // fs.traverse (same as walk but returns FileObjects instead of strings)
    api.registerFunction(
        "fs.traverse", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeNull();
            std::string path = valueToString(args[0], api);
            std::error_code ec;
            if (!fs::is_directory(path, ec))
                return Value::makeNull();
            std::vector<fs::path> results;
            walkDir(path, results, api);
            auto arrRef = api.vm.createHostArray();
            for (const auto &p : results) {
                auto fileObj = createFileObject(p, api);
                api.vm.pushHostArrayValue(arrRef, fileObj);
            }
            return Value::makeArrayId(arrRef.id);
        });

    // fs.glob (pattern matching with * and **)
    api.registerFunction(
        "fs.glob", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeNull();
            std::string pattern = valueToString(args[0], api);
            std::string dir = ".";
            if (args.size() > 1) {
                dir = valueToString(args[1], api);
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
            auto arrRef = api.vm.createHostArray();
            for (const auto &p : results) {
                api.vm.pushHostArrayValue(arrRef, makeStringId(p.string(), api));
            }
            return Value::makeArrayId(arrRef.id);
        });

    // fs.watch (inotify-based directory watcher)
    api.registerFunction(
        "fs.watch", [&api](const std::vector<Value> &args) {
            if (args.size() < 2)
                return Value::makeNull();
            std::string path = valueToString(args[0], api);
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
            auto &vm = api.vm;
            vm.registerHostFunction("fs._watchClose_" + std::to_string(inotifyFd),
                [&api, inotifyFd](const std::vector<Value> &) {
                    close(inotifyFd);
                    return Value::makeBool(true);
                });
            return watchObj;
        });

    // fs._watchClose (close watch handle)
    api.registerFunction(
        "fs._watchClose", [&api](const std::vector<Value> &args) {
            if (args.empty() || !args[0].isObjectId())
                return Value::makeBool(false);
            auto fdVal = api.vm.getHostObjectField(
                ObjectRef{args[0].asObjectId(), true}, "fd");
            if (!fdVal.isInt() && !fdVal.isDouble())
                return Value::makeBool(false);
            int fd = fdVal.isInt() ? static_cast<int>(fdVal.asInt())
                                   : static_cast<int>(fdVal.asDouble());
            close(fd);
            return Value::makeBool(true);
        });

    // fs.watchTree (recursive directory watcher)
    api.registerFunction(
        "fs.watchTree", [&api](const std::vector<Value> &args) {
            if (args.size() < 2)
                return Value::makeNull();
            std::string path = valueToString(args[0], api);
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
        });

    // fs.open (file handle: r, w, w+, a)
    api.registerFunction(
        "fs.open", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeNull();
            std::string path = valueToString(args[0], api);
            std::string mode = "r";
            if (args.size() > 1) {
                mode = valueToString(args[1], api);
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
        });

    // File handle: read(n)
    api.registerFunction(
        "fs._handleRead", [&api](const std::vector<Value> &args) {
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
            return makeStringId(std::string(buf.data(), bytesRead), api);
        });

    // File handle: write(data)
    api.registerFunction(
        "fs._handleWrite", [&api](const std::vector<Value> &args) {
            if (args.size() < 2 || !args[0].isObjectId())
                return Value::makeBool(false);
            auto *handle = getHandle(args[0], api);
            if (!handle || !handle->open)
                return Value::makeBool(false);
            std::string data = valueToString(args[1], api);
            ssize_t written = ::write(handle->fd, data.c_str(), data.size());
            return Value::makeBool(written >= 0);
        });

    // File handle: prepend(data)
    api.registerFunction(
        "fs._handlePrepend", [&api](const std::vector<Value> &args) {
            if (args.size() < 2 || !args[0].isObjectId())
                return Value::makeBool(false);
            auto *handle = getHandle(args[0], api);
            if (!handle || !handle->open)
                return Value::makeBool(false);
            std::string data = valueToString(args[1], api);
            std::string existing = valueToString(
                api.vm.getHostObjectField(
                    ObjectRef{args[0].asObjectId(), true}, "path"), api);
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
            if (args.size() < 2 || !args[0].isObjectId())
                return Value::makeBool(false);
            auto *handle = getHandle(args[0], api);
            if (!handle || !handle->open)
                return Value::makeBool(false);
            std::string data = valueToString(args[1], api);
            ::lseek(handle->fd, 0, SEEK_END);
            ssize_t written = ::write(handle->fd, data.c_str(), data.size());
            return Value::makeBool(written >= 0);
        });

    // File handle: seek(position)
    api.registerFunction(
        "fs._handleSeek", [&api](const std::vector<Value> &args) {
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
        });

    // File handle: clear()
    api.registerFunction(
        "fs._handleClear", [&api](const std::vector<Value> &args) {
            if (args.empty() || !args[0].isObjectId())
                return Value::makeBool(false);
            auto *handle = getHandle(args[0], api);
            if (!handle || !handle->open)
                return Value::makeBool(false);
            if (::ftruncate(handle->fd, 0) != 0)
                return Value::makeBool(false);
            ::lseek(handle->fd, 0, SEEK_SET);
            return Value::makeBool(true);
        });

    // File handle: flush()
    api.registerFunction(
        "fs._handleFlush", [&api](const std::vector<Value> &args) {
            if (args.empty() || !args[0].isObjectId())
                return Value::makeBool(false);
            auto *handle = getHandle(args[0], api);
            if (!handle || !handle->open)
                return Value::makeBool(false);
            return Value::makeBool(::fsync(handle->fd) == 0);
        });

    // File handle: close()
    api.registerFunction(
        "fs._handleClose", [&api](const std::vector<Value> &args) {
            if (args.empty() || !args[0].isObjectId())
                return Value::makeBool(false);
            auto *handle = getHandle(args[0], api);
            if (!handle || !handle->open)
                return Value::makeBool(false);
            ::close(handle->fd);
            handle->open = false;
            getFileHandles().erase(handle);
            delete handle;
            auto &vm = api.vm;
            vm.setHostObjectField(ObjectRef{args[0].asObjectId(), true},
                "__handle_ptr", Value::makeInt(0));
            return Value::makeBool(true);
        });

    // File handle: remove() (close and delete file)
    api.registerFunction(
        "fs._handleRemove", [&api](const std::vector<Value> &args) {
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
        });

    // fs.atomicWrite (write to temp then rename)
    api.registerFunction(
        "fs.atomicWrite", [&api](const std::vector<Value> &args) {
            if (args.size() < 2)
                return Value::makeBool(false);
            std::string path = valueToString(args[0], api);
            std::string content = valueToString(args[1], api);
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
            std::string tmpl = "/tmp/havel_XXXXXX";
            if (!args.empty()) {
                tmpl = valueToString(args[0], api);
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
        });

    // fs.lock (advisory file lock via flock)
    api.registerFunction(
        "fs.lock", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeBool(false);
            std::string path = valueToString(args[0], api);
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
        });

    // fs.tryLock (non-blocking advisory file lock)
    api.registerFunction(
        "fs.tryLock", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeBool(false);
            std::string path = valueToString(args[0], api);
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
        });

    // fs.isLocked
    api.registerFunction(
        "fs.isLocked", [&api](const std::vector<Value> &args) {
            if (args.empty())
                return Value::makeBool(false);
            std::string path = valueToString(args[0], api);
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
