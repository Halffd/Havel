🗺️ Havel Language Roadmap
✅ Done
	* Core VM + bytecode compiler
	* Closures + upvalue capture
	* Generators / coroutines (yield)
	* Tail call optimization
	* GC heap
	* Basic stdlib (print, fs, env, time)
	* LLVM JIT (partial)
	* String types (single, double, triple, f-strings)
	* for loops, if/else, try/catch
	* INCLOCAL/DECLOCAL opcodes
	* Jump threading optimization

----

🔧 Phase 1 — Stability (fix what's broken)
	* [ ] Generator + for loop upvalue bug (Test 5)
	* [ ] Deep nested coroutine state (Test 8)
	* [ ] LLVM JIT — remaining opcode coverage
	* [ ] Negative indexing access arr[-1] (last element)
	* [ ] Octal literals 0o644, Hexadecimal literals 0xF0, Binary literals 0b00001001
	* [ ] type() doesn't recognize coroutines

----

📦 Phase 2 — Stdlib Expansion
fs module
    * [ ] fs.exists(path)           // Check if file/directory exists
fs.isFile(path)           // Check if path is a file
fs.isDir(path)            // Check if path is a directory
fs.isSymlink(path)        // Check if path is a symbolic link
fs.size(path)             // Get file size in bytes
fs.read(path)             // Read entire file as string
fs.write(path, content)   // Write string to file (overwrites)
fs.append(path, content)  // Append string to file
fs.rm(path)               // Remove file
fs.copy(src, dest)        // Copy file
fs.move(src, dest)        // Move/rename file
fs.rename(old, new)       // Rename file (similar to move)
Directory Operations
javascript
fs.mkdir(path)            // Create directory
fs.rmdir(path, recursive) // Remove directory (recursive option)
fs.readDir(path)          // List directory contents (returns array of file info objects)
fs.copyDir(src, dest)     // Recursive directory copy
File Info Objects (from readDir)
javascript
// Each entry has:
entry.name        // File name
entry.path        // Full path
entry.size        // Size in bytes
entry.extension   // File extension
entry.permissions // Unix permissions (if available)
entry.modDate     // Modified date
entry.accessDate  // Access date
entry.birthDate   // Creation date
File Handles (Streaming)
javascript
fs.open(path, mode)       // Open file handle
// Modes: "r" (read), "w" (write), "w+" (read/write), "a" (append)
// Handle methods:
handle.read(n)            // Read n bytes (or all if no n)
handle.write(data)        // Write data
handle.prepend(data)      // Write at beginning
handle.append(data)       // Write at end
handle.seek(position)     // Move read/write position
handle.clear()            // Clear file content
handle.flush()            // Flush buffers to disk
handle.close()            // Close handle
handle.remove()           // Close and delete file
handle.name               // File name property
handle.path               // File path property
Advanced Operations
javascript
fs.walk(path, callback)           // Recursive directory iteration
fs.traverse(path)                 // Returns iterator for recursive walk
fs.glob(pattern)                  // Pattern matching (supports *, **, {brace}, [class])
fs.watch(path, callback)          // Watch directory for changes
fs.watchTree(path, callback)      // Recursive directory watching
fs.chmod(path, mode)              // Change permissions (Unix only)
fs.stat(path)                     // Get file metadata
fs.tempFile()                     // Create temporary file
fs.atomicWrite(path, content)     // Atomic write with temp file + rename
fs.lock(path)                     // Acquire file lock
fs.tryLock(path)                  // Try non-blocking lock
fs.isLocked(path)                 // Check if file is locked
	* [ ] fs.walk / fs.traverse
	* [ ] fs.glob
	* [ ] fs.watch / fs.watchTree
	* [ ] fs.copyDir
	* [ ] fs.symlink / fs.readlink
	* [ ] fs.chmod / fs.stat
	* [ ] fs.atomicWrite
	* [ ] fs.tempFile
	* [ ] fs.lock / fs.tryLock
	* [ ] fs.createReadStream / fs.createWriteStream
	* [ ] File handle object (fs.open → .read(), .write(), .seek(), .clear(), .close())
Path Helpers
javascript
path.basename(path)               // Get file name from path
path.dirname(path)                // Get directory from path
path.ext(path)                    // Get file extension
path.join(...parts)               // Join path segments
path.sep                          // Path separator ('/' or '\')
path.isAbs(path)                  // Check if path is absolute
path.normalize(path)              // Normalize path (remove ./, ../)
path.resolve(...paths)            // Resolve to absolute path
path.relative(from, to)           // Get relative path
Global Shortcuts (Convenience)
javascript
read(path)        // Same as fs.read(path)
write(path, data) // Same as fs.write(path, data)
append(path, data)// Same as fs.append(path, data)
rm(path)          // Same as fs.rm(path)
Environment & System
javascript
env.get(name)     // Get environment variable
env.set(name, value) // Set environment variable
env.has(name)     // Check if environment variable exists
env.unset(name)   // Delete environment variable
env.all()         // Get all environment variables

sys.platform      // "linux", "macos", "windows", "bsd", "unknown"
sys.hostname()    // System hostname
sys.cpus()        // CPU info
sys.memory()      // Memory info
sys.uptime()      // System uptime
Time Helpers
javascript
time.date()       // Current date as string
time.now()        // Current timestamp (milliseconds)
time.unix()       // Current Unix timestamp (seconds)
time.sleep(ms)    // Sleep for milliseconds

parse module

	* [ ] parse.json / write.json
	* [ ] parse.yaml
	* [ ] parse.toml
	* [ ] parse.csv
	* [ ] parse.xml
	* [ ] parse.ini
	* [ ] parse.env
	* [ ] parse.url
	* [ ] parse.query
	* [ ] parse.auto (format detection)
	* [ ] parse.validate (schema validation)
	* [ ] Streaming parsers (parse.json.stream)

crypto module

	* [ ] crypto.hash (sha256, sha512, md5)
	* [ ] crypto.hashFile (streaming)
	* [ ] crypto.hmac
	* [ ] crypto.encrypt / crypto.decrypt (AES-GCM)
	* [ ] crypto.generateKey / crypto.generateKeyPair
	* [ ] crypto.sign / crypto.verify
	* [ ] crypto.pbkdf2
	* [ ] crypto.randomBytes / crypto.randomUUID
	* [ ] crypto.loadCertificate
	* [ ] crypto.seal (TPM/keychain)

encode module

	* [ ] encode.base64 / decode.base64
	* [ ] encode.hex / decode.hex
	* [ ] encode.utf8
	* [ ] String methods: .toBase64(), .fromBase64()

zip module

	* [ ] zip.create / zip.extract
	* [ ] zip.list
	* [ ] zip.open (virtual fs handle)
	* [ ] Encrypted zip
	* [ ] Formats: zip, tar, tar.gz, 7z

----

🚀 Phase 3 — Language Features
	* [ ] Pipe operator |

"hello" | toUpper | print
data | compress("gzip") | encrypt(key) | write("out.bin")

	* [ ] sys.platform builtin (replace env.get("OSTYPE"))
	* [ ] Path type — rich path object vs plain strings

let p = Path("/tmp/foo/bar.txt")
p.parent    // /tmp/foo
p.stem      // bar
p.ext       // .txt
p / "baz"   // /tmp/foo/bar.txt/baz  (/ operator)

	* [ ] Classes with inheritance (extends)
	* [ ] Interfaces / traits
	* [ ] Pattern matching (match expression)

match value {
    0       => "zero"
    1..10   => "small"
    string  => "it's a string: " + value
    _       => "other"
}

	* [ ] Destructuring

let [a, b, ...rest] = arr
let { name, age } = obj

	* [ ] Optional chaining ?.
	* [ ] Spread operator ...
	* [ ] async/await (built on coroutines)
	* [ ] Operator overloading
	* [ ] Generics / type parameters
	* [ ] defer statement

----

⚡ Phase 4 — Performance
	* [ ] Full LLVM JIT coverage (all opcodes)
	* [ ] Inline caches for method dispatch
	* [ ] Escape analysis (stack-allocate short-lived objects)
	* [ ] Profile-guided optimization
	* [ ] SIMD intrinsics
	* [ ] Parallel GC

----

🌐 Phase 5 — Ecosystem
	* [ ] Package manager (hpkg or haven)
	* [ ] Build system
	* [ ] LSP (language server for editor support)
	* [ ] Debugger (--debug step mode)
	* [ ] REPL improvements
	* [ ] Doc generator
	* [ ] Formatter (havel fmt)
	* [ ] Linter
	* [ ] WebAssembly target
	* [ ] C FFI / interop
	* [ ] Test runner (built-in test {} blocks)

test "fs.read works" {
    let content = fs.read("/tmp/test.txt")
    assert(content != null)
}


----

🎯 North Star
fast as C, clean as Python, safe as Rust, batteries as Go
pipe operator + generators + coroutines = best async story of any language

----

Havel Module Architecture
Binary (~10MB)
/usr/bin/havel
├── VM + GC
├── Core types (int, str, array, object, bool, null)
├── Builtins (print, input, type, len, range, assert)
├── Operators
├── Module loader
└── Error handling

Stdlib (~20-30MB on disk, loaded on demand)
/usr/lib/havel/
├── fs.hv           ← pure Havel
├── path.hv         ← pure Havel
├── time.hv         ← pure Havel
├── env.hv          ← pure Havel
├── math.hv         ← pure Havel
├── parse/
│   ├── json.hv     ← pure Havel
│   ├── yaml.hv     ← pure Havel
│   ├── toml.hv     ← pure Havel
│   └── csv.hv      ← pure Havel
├── crypto.so       ← C extension (perf critical)
├── zip.so          ← C extension
└── net.so          ← C extension

User packages
~/.havel/packages/
└── installed by hpkg

----

The use Story
three styles, all valid:

// explicit use (Go-style, best for large scripts)
use fs from "fs"
use { read, write } from "fs"

// auto-available globals (configured in havel.config.json)
{
    "autouses": ["fs", "path", "time"]
}

// dynamic use (for optional/conditional loading)
let crypto = use("crypto")

----

Compiled Bytecode Cache
this is the key perf win Python missed for years:

/usr/lib/havel/
├── fs.hv           ← source
├── __cache__/
│   └── fs.hbc      ← compiled bytecode, auto-generated
                       invalidated when .hv changes

~/.havel/packages/mylib/
├── mylib.hv
└── __cache__/
    └── mylib.hbc


startup sequence:
```

	1. check for .hbc cache
	2. if exists + newer than .hv → load .hbc directly (fast)
	3. if missing/stale → compile .hv → write .hbc → load


---

## Build Modes


bash
# scripting (no compile step, just run)
havel script.hv
release binary (bundles only used modules)
havel build script.hv -o myapp

→ tree shakes stdlib, embeds only used functions
→ result: single static binary, no runtime deps
minimal (tier 0 only, for embedded use)
havel build --minimal script.hv -o myapp   # ~1MB

dynamic (small binary + shared .so libs)
havel build --dynamic script.hv -o myapp   # ~300KB + /usr/lib/havel/*.so


---

## What Goes Where (decision table)

| module | tier | format | reason |
|--------|------|--------|--------|
| print, len, type | 0 | compiled in | can't run without |
| fs, path, time | 1 | .hv + .hbc | common, pure logic |
| math | 1 | .hv + .hbc | pure logic |
| parse/json | 1 | .hv + .hbc | pure logic |
| parse/yaml | 1 | .hv + .hbc | pure logic |
| crypto | 2 | .so | perf critical |
| zip | 2 | .so | perf critical |
| net/http | 2 | .so | needs libuv/OS APIs |
| image/audio | 3 | hpkg | too niche for stdlib |
| tensorflow | 3 | hpkg | obviously external |

---

## `hpkg` Package Manager


bash
hpkg install express        # installs to ~/.havel/packages/
hpkg install express --global  # installs to /usr/lib/havel/packages/
hpkg publish                # publish to registry
hpkg init                   # create havel.pkg.json

```json
// havel.pkg.json
{
    "name": "myapp",
    "version": "1.0.0",
    "dependencies": {
        "express": "^2.0.0",
        "sqlite": "^1.0.0"
    },
    "autouses": ["fs", "path", "time"],
    "features": ["crypto", "zip"]
}

----

the biggest win over Node is keeping the binary small — Node's 85MB is mostly V8 which you don't need since you have your own VM. shooting for 8-12MB binary + 25MB stdlib on disk seems very achievable

src/havel-lang/compiler/module/
src/havel-lang/runtime/ModuleLoader.hpp
src/modules/ModuleLoader.cpp
src/host/module/ModuleLoader.cpp

4 separate module loaders — that might be worth consolidating at some point

src/core/browser/BrowserModule.cpp
src/modules/browser/BrowserModule.cpp

duplicate filenames in different locations — same with a few others like MouseController.hpp appearing in both src/io/ and src/core/io/

stage 0: C++ compiler (current)
stage 1: write compiler.hv, compile it WITH stage 0
stage 2: compile compiler.hv WITH stage 1
stage 3: compiler can compile itself ← true self-hosting


	1. stability bugs         ← blocking everything
	2. use/module system   ← unlocks everything
	3. llvm separation        ← do while codebase is still manageable
	4. stdlib in .hv          ← big cleanup, removes 20+ C++ files
	5. hpkg packages          ← ecosystem
	6. self-hosted compiler   ← end game