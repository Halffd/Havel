/*
 * HelpModule.cpp
 * 
 * Help documentation module for Havel language.
 */
#include "HelpModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"

namespace havel::modules {

void registerHelpModule(Environment& env, HostContext& ctx) {
    (void)ctx;  // Help doesn't need host context
    
    env.Define("help", BuiltinFunction(

            if (args.empty()) {
              // Show general help
              help << "\n=== Havel Language Help ===\n\n";
              help << "Usage: help()          - Show this help\n";
              help << "       help(\"module\")  - Show help for specific "
                      "module\n\n";
              help << "Available modules:\n";
              help << "  - system      : System functions (print, sleep, "
                      "exit, "
                      "etc.)\n";
              help << "  - window      : Window management functions\n";
              help << "  - clipboard   : Clipboard operations\n";
              help << "  - text        : Text manipulation (upper, lower, "
                      "trim, "
                      "etc.)\n";
              help << "  - file        : File I/O operations\n";
              help << "  - array       : Array manipulation (map, filter, "
                      "reduce, "
                      "etc.)\n";
              help << "  - io          : Input/output control\n";
              help << "  - audio       : Audio control (volume, mute, "
                      "etc.)\n";
              help << "  - media       : Media playback control\n";
              help << "  - brightness  : Screen brightness control\n";
              help << "  - launcher    : Process launching (run, kill, "
                      "etc.)\n";
              help << "  - gui         : GUI dialogs and menus\n";
              help << "  - screenshot  : Screenshot capture with image data\n";
              help << "  - pixel       : Pixel-level screen operations\n";
              help << "  - image       : Image finding and matching\n";
              help << "  - ocr         : Optical character recognition\n";
              help << "  - timer       : Timer and scheduling functions\n";
              help << "  - automation  : Task automation\n";
              help << "  - physics     : Physics calculations\n";
              help << "  - process     : Process management\n";
              help << "  - http        : HTTP requests\n";
              help << "  - regex       : Regular expressions\n";
              help << "  - media       : Media playback\n";
              help << "  - mpvcontroller : MPV media player control\n";
              help << "  - filemanager : Advanced file operations\n";
              help << "  - altTab      : Window switcher\n";
              help << "  - mapmanager  : Key mapping management\n";
              help << "  - brightness  : Screen brightness control\n";
              help << "  - config      : Configuration access\n";
              help << "  - debug       : Debugging utilities\n";
              help << "  - approx      : Fuzzy comparison for floats\n\n";
              help << "Language features:\n";
              help << "  - const       : Immutable variable bindings\n";
              help << "  - trait/impl  : Interface-based polymorphism\n";
              help << "  - repeat      : Loop with count (supports variables)\n";
              help << "  - $ command   : Shell command execution\n";
              help << "  - `command`   : Shell command with output capture\n";
              help << "  - :duration   : Sleep statement (e.g., :100)\n";
              help << "  - struct methods : Methods on struct instances\n";
              help << "  - Type()      : Struct constructor sugar\n\n";
              help << "For detailed documentation, see Havel.md\n";
            } else {
              std::string module = ValueToString(args[0]);

              if (module == "system") {
                help << "\n=== System Module ===\n\n";
                help << "Constants:\n";
                help << "  true, false, null\n\n";
                help << "Functions:\n";
                help << "  print(...args)         - Print values to "
                        "stdout\n";
                help << "  println(...args)       - Print values with "
                        "newline\n";
                help << "  sleep(ms)              - Sleep for "
                        "milliseconds\n";
                help << "  exit([code])           - Exit program with "
                        "optional "
                        "code\n";
                help << "  type(value)            - Get type of value\n";
                help << "  len(array|string)      - Get length\n";
                help << "  range(start, end)      - Create array of "
                        "numbers\n";
                help << "  random([min, max])     - Generate random "
                        "number\n";
              } else if (module == "window") {
                help << "\n=== Window Module ===\n\n";
                help << "Functions:\n";
                help << "  window.getTitle()              - Get active "
                        "window "
                        "title\n";
                help << "  window.maximize()              - Maximize active "
                        "window\n";
                help << "  window.minimize()              - Minimize active "
                        "window\n";
                help << "  window.close()                 - Close active "
                        "window\n";
                help << "  window.center()                - Center active "
                        "window\n";
                help << "  window.focus()                 - Focus active "
                        "window\n";
                help << "  window.next()                  - Switch to next "
                        "window\n";
                help << "  window.previous()              - Switch to "
                        "previous "
                        "window\n";
                help << "  window.move(x, y)              - Move window to "
                        "position\n";
                help << "  window.resize(w, h)            - Resize window\n";
                help << "  window.moveResize(x,y,w,h)     - Move and "
                        "resize\n";
                help << "  window.alwaysOnTop(enable)     - Set always on "
                        "top\n";
                help << "  window.transparency(level)     - Set "
                        "transparency "
                        "(0-1)\n";
                help << "  window.toggleFullscreen()      - Toggle "
                        "fullscreen\n";
                help << "  window.snap(direction)         - Snap to screen "
                        "edge\n";
                help << "  window.moveToMonitor(index)    - Move to "
                        "monitor\n";
                help << "  window.moveToCorner(corner)    - Move to "
                        "corner\n";
                help << "  window.getClass()              - Get window "
                        "class\n";
                help << "  window.exists()                - Check if window "
                        "exists\n";
                help << "  window.isActive()              - Check if "
                        "window is "
                        "active\n";
              } else if (module == "clipboard") {
                help << "\n=== Clipboard Module ===\n\n";
                help << "Functions:\n";
                help << "  clipboard.get()        - Get clipboard text\n";
                help << "  clipboard.set(text)    - Set clipboard text\n";
                help << "  clipboard.clear()      - Clear clipboard\n";
              } else if (module == "text") {
                help << "\n=== Text Module ===\n\n";
                help << "Functions:\n";
                help << "  upper(text)            - Convert to uppercase\n";
                help << "  lower(text)            - Convert to lowercase\n";
                help << "  trim(text)             - Remove leading/trailing "
                        "whitespace\n";
                help << "  split(text, delimiter) - Split text into array\n";
                help << "  join(array, separator) - Join array into text\n";
                help << "  replace(text, old, new)- Replace text\n";
                help << "  contains(text, search) - Check if text contains "
                        "substring\n";
                help << "  startsWith(text, prefix) - Check if starts "
                        "with\n";
                help << "  endsWith(text, suffix)   - Check if ends with\n";
              } else if (module == "file") {
                help << "\n=== File Module ===\n\n";
                help << "Functions:\n";
                help << "  file.read(path)        - Read file contents\n";
                help << "  file.write(path, data) - Write to file\n";
                help << "  file.exists(path)      - Check if file exists\n";
              } else if (module == "array") {
                help << "\n=== Array Module ===\n\n";
                help << "Functions:\n";
                help << "  map(array, fn)         - Transform array "
                        "elements\n";
                help << "  filter(array, fn)      - Filter array elements\n";
                help << "  reduce(array, fn, init)- Reduce array to single "
                        "value\n";
                help << "  forEach(array, fn)     - Execute function for "
                        "each "
                        "element\n";
                help << "  push(array, value)     - Add element to end\n";
                help << "  pop(array)             - Remove and return last "
                        "element\n";
                help << "  shift(array)           - Remove and return first "
                        "element\n";
                help << "  unshift(array, value)  - Add element to "
                        "beginning\n";
                help << "  reverse(array)         - Reverse array\n";
                help << "  sort(array, [fn])      - Sort array\n";
              } else if (module == "io") {
                help << "\n=== IO Module ===\n\n";
                help << "Functions:\n";
                help << "  io.block()             - Block all input\n";
                help << "  io.unblock()           - Unblock input\n";
                help << "  send(keys)             - Send keystrokes\n";
                help << "  click([button])        - Simulate mouse click\n";
                help << "  mouseMove(x, y)        - Move mouse to "
                        "position\n";
              } else if (module == "audio") {
                help << "\n=== Audio Module ===\n\n";
                help << "Functions:\n";
                help << "  audio.getVolume()      - Get system volume "
                        "(0-100)\n";
                help << "  audio.setVolume(level) - Set system volume\n";
                help << "  audio.mute()           - Mute audio\n";
                help << "  audio.unmute()         - Unmute audio\n";
                help << "  audio.toggleMute()     - Toggle mute state\n";
              } else if (module == "media") {
                help << "\n=== Media Module ===\n\n";
                help << "Functions:\n";
                help << "  media.play()           - Play media\n";
                help << "  media.pause()          - Pause media\n";
                help << "  media.stop()           - Stop media\n";
                help << "  media.next()           - Next track\n";
                help << "  media.previous()       - Previous track\n";
              } else if (module == "brightness") {
                help << "\n=== Brightness Module ===\n\n";
                help << "Functions:\n";
                help << "  brightnessManager.getBrightness()    - Get "
                        "brightness "
                        "(0-100)\n";
                help << "  brightnessManager.setBrightness(val) - Set "
                        "brightness\n";
              } else if (module == "launcher") {
                help << "\n=== Launcher Module ===\n\n";
                help << "Functions:\n";
                help << "  run(command)           - Run command and return "
                        "result "
                        "object {success, exitCode, stdout, stderr, pid, "
                        "error, "
                        "executionTimeMs\n";
                help << "  runAsync(command)      - Run command "
                        "asynchronously\n";
                help << "  runDetached(command)   - Run command detached "
                        "from "
                        "parent\n";
                help << "  terminal(command)      - Run command in "
                        "terminal\n";
                help << "  kill(pid)              - Kill process by PID\n";
                help << "  killByName(name)       - Kill process by name\n";
              } else if (module == "gui") {
                help << "\n=== GUI Module ===\n\n";
                help << "Functions:\n";
                help << "  gui.menu(items)        - Show menu dialog\n";
                help << "  gui.notify(title, msg) - Show notification\n";
                help << "  gui.confirm(msg)       - Show confirmation "
                        "dialog\n";
                help << "  gui.input(prompt)      - Show input dialog\n";
                help << "  gui.fileDialog([title, dir, filter]) - Show file "
                        "picker\n";
                help << "  gui.directoryDialog([title, dir])    - Show "
                        "directory "
                        "picker\n";
              } else if (module == "debug") {
                help << "\n=== Debug Module ===\n\n";
                help << "Variables:\n";
                help << "  debug                  - Debug flag "
                        "(boolean)\n\n";
                help << "Functions:\n";
                help << "  assert(condition, msg) - Assert condition\n";
                help << "  trace(msg)             - Print trace message\n";
              } else if (module == "mpvcontroller") {
                help << "\n=== MPVController Module ===\n\n";
                help << "Functions:\n";
                help << "  mpvcontroller.volumeUp()                    - "
                        "Increase "
                        "volume\n";
                help << "  mpvcontroller.volumeDown()                  - "
                        "Decrease "
                        "volume\n";
                help << "  mpvcontroller.toggleMute()                  - "
                        "Toggle "
                        "mute\n";
                help << "  mpvcontroller.seekForward()                 - "
                        "Seek "
                        "forward\n";
                help << "  mpvcontroller.seekBackward()                - "
                        "Seek "
                        "backward\n";
                help << "  mpvcontroller.speedUp()                     - "
                        "Increase "
                        "playback speed\n";
                help << "  mpvcontroller.slowDown()                    - "
                        "Decrease "
                        "playback speed\n";
                help << "  mpvcontroller.toggleSubtitleVisibility()   - "
                        "Toggle "
                        "subtitles\n";
                help << "  mpvcontroller.setLoop(enabled)              - "
                        "Set loop "
                        "mode\n";
                help << "  mpvcontroller.sendRaw(command)             - "
                        "Send raw "
                        "MPV command\n";
              } else if (module == "textchunker") {
                help << "\n=== TextChunker Module ===\n\n";
                help << "Functions:\n";
                help << "  textchunker.chunk(text, maxSize)           - "
                        "Split "
                        "text "
                        "into chunks\n";
                help << "  textchunker.merge(chunks)                   - "
                        "Merge "
                        "chunks back\n";
              } else if (module == "ocr") {
                help << "\n=== OCR Module ===\n\n";
                help << "Functions:\n";
                help << "  ocr.capture()                               - "
                        "Capture "
                        "screen and extract text\n";
                help << "  ocr.captureRegion(x, y, width, height)      - "
                        "Capture "
                        "region and extract text\n";
                help << "  ocr.extractText(imagePath)                  - "
                        "Extract "
                        "text from image file\n";
              } else if (module == "alttab") {
                help << "\n=== AltTab Module ===\n\n";
                help << "Functions:\n";
                help << "  alttab.show()                               - "
                        "Show "
                        "alt-tab window switcher\n";
                help << "  alttab.next()                               - "
                        "Switch "
                        "to "
                        "next window\n";
                help << "  alttab.previous()                           - "
                        "Switch "
                        "to "
                        "previous window\n";
                help << "  alttab.hide()                               - "
                        "Hide "
                        "alt-tab switcher\n";
              } else if (module == "clipboardmanager") {
                help << "\n=== ClipboardManager Module ===\n\n";
                help << "Functions:\n";
                help << "  clipboardmanager.copy(text)                 - "
                        "Copy "
                        "text "
                        "to clipboard\n";
                help << "  clipboardmanager.paste()                    - "
                        "Paste "
                        "from clipboard\n";
                help << "  clipboardmanager.clear()                    - "
                        "Clear "
                        "clipboard\n";
                help << "  clipboardmanager.history()                  - "
                        "Get "
                        "clipboard history\n";
              } else if (module == "mapmanager") {
                help << "\n=== MapManager Module ===\n\n";
                help << "Functions:\n";
                help << "  mapmanager.load(mapFile)                    - "
                        "Load key "
                        "mapping file\n";
                help << "  mapmanager.save(mapFile)                    - "
                        "Save "
                        "current mappings\n";
                help << "  mapmanager.clear()                          - "
                        "Clear "
                        "all "
                        "mappings\n";
                help << "  mapmanager.list()                           - "
                        "List all "
                        "mappings\n";
                help << "  mapmanager.add(key, action)                 - "
                        "Add key "
                        "mapping\n";
                help << "  mapmanager.remove(key)                      - "
                        "Remove "
                        "key mapping\n";
              } else if (module == "filemanager") {
                help << "\n=== FileManager Module ===\n\n";
                help << "Functions:\n";
                help << "  filemanager.read(path)                      - "
                        "Read "
                        "file "
                        "content\n";
                help << "  filemanager.write(path, content)             - "
                        "Write "
                        "content to file\n";
                help << "  filemanager.append(path, content)            - "
                        "Append "
                        "content to file\n";
                help << "  filemanager.exists(path)                     - "
                        "Check "
                        "if "
                        "file exists\n";
                help << "  filemanager.delete(path)                    - "
                        "Delete "
                        "file\n";
                help << "  filemanager.copy(source, dest)              - "
                        "Copy "
                        "file\n";
                help << "  filemanager.move(source, dest)              - "
                        "Move "
                        "file\n";
                help << "  filemanager.size(path)                      - "
                        "Get file "
                        "size\n";
                help << "  filemanager.wordCount(path)                 - "
                        "Count "
                        "words in file\n";
                help << "  filemanager.lineCount(path)                 - "
                        "Count "
                        "lines in file\n";
                help << "  filemanager.getChecksum(path, algorithm)    - "
                        "Get file "
                        "checksum\n";
                help << "  filemanager.getMimeType(path)               - "
                        "Get MIME "
                        "type\n";
                help << "  filemanager.File(path)                      - "
                        "Create "
                        "File object\n\n";
                help << "Detector Functions:\n";
                help << "  detectDisplay()                             - "
                        "Detect "
                        "display configuration\n";
                help << "  detectMonitorConfig()                       - "
                        "Detect "
                        "monitor configuration\n";
                help << "  detectWindowManager()                       - "
                        "Detect "
                        "window manager\n";
                help << "  detectSystem()                              - "
                        "Detect "
                        "system information\n";
              } else if (module == "screenshot") {
                help << "\n=== Screenshot Module ===\n\n";
                help << "Functions (all return {path, data, width, height}):\n";
                help << "  screenshot.full([path])           - Full screen screenshot\n";
                help << "  screenshot.region(x,y,w,h)        - Region screenshot\n";
                help << "  screenshot.monitor()              - Current monitor screenshot\n";
                help << "\nReturns object with:\n";
                help << "  - path: File path to saved PNG\n";
                help << "  - data: Base64-encoded image data\n";
                help << "  - width, height: Image dimensions\n";
              } else if (module == "pixel") {
                help << "\n=== Pixel Module ===\n\n";
                help << "Functions:\n";
                help << "  pixel.get()                  - Get pixel color at cursor\n";
                help << "  pixel.get(x, y)              - Get pixel color at position\n";
                help << "  pixel.match(color)           - Check cursor pixel matches color\n";
                help << "  pixel.match(x, y, color, tol)- Check if pixel matches color\n";
                help << "  pixel.waitFor(x, y, color, timeout) - Wait for pixel color\n";
                help << "  pixel.setCacheEnabled(enabled, cacheTime) - Enable screenshot cache\n";
              } else if (module == "timer") {
                help << "\n=== Timer Module ===\n\n";
                help << "Functions:\n";
                help << "  timer.start(id, interval, callback) - Start repeating timer\n";
                help << "  timer.stop(id)                      - Stop timer\n";
                help << "  timer.once(delay, callback)         - One-shot timer\n";
              } else if (module == "approx") {
                help << "\n=== Approx Module ===\n\n";
                help << "Functions:\n";
                help << "  approx(a, b, epsilon)  - Fuzzy float comparison (relative tolerance)\n";
                help << "\nExample:\n";
                help << "  approx(0.1 + 0.2, 0.3)  => true\n";
              } else if (module == "type") {
                help << "\n=== Type Conversion ===\n\n";
                help << "Functions:\n";
                help << "  int(x)     - Convert to integer (truncates)\n";
                help << "  num(x)     - Convert to double\n";
                help << "  str(x)     - Convert to string\n";
                help << "  list(...)  - Create list from arguments or iterable\n";
                help << "  tuple(...) - Create tuple (fixed-size list)\n";
                help << "  set_(...)  - Create set (unique elements)\n";
              } else if (module == "implements") {
                help << "\n=== Traits ===\n\n";
                help << "Syntax:\n";
                help << "  trait Name { fn method() }\n";
                help << "  impl Name for Type { fn method() { ... } }\n";
                help << "\nFunctions:\n";
                help << "  implements(obj, traitName) - Check if type implements trait\n";
              } else if (module == "repeat") {
                help << "\n=== Repeat Statement ===\n\n";
                help << "Syntax:\n";
                help << "  repeat count { body }     - Repeat count times\n";
                help << "  repeat count statement    - Inline form\n";
                help << "\nCount can be literal, variable, or expression:\n";
                help << "  repeat 5 { ... }          - Literal\n";
                help << "  repeat n { ... }          - Variable\n";
                help << "  repeat 2 + 3 { ... }      - Expression\n";
              } else if (module == "shell") {
                help << "\n=== Shell Commands ===\n\n";
                help << "Syntax:\n";
                help << "  $ command          - Execute shell command (fire-and-forget)\n";
                help << "  `command`          - Execute and capture output\n";
                help << "\nBacktick returns object:\n";
                help << "  - stdout: Command output\n";
                help << "  - stderr: Error output\n";
                help << "  - exitCode: Exit code\n";
                help << "  - success: Boolean success flag\n";
                help << "  - error: Error message if any\n";
              } else if (module == "sleep") {
                help << "\n=== Sleep Statement ===\n\n";
                help << "Syntax:\n";
                help << "  :duration          - Sleep for duration\n";
                help << "\nDuration formats:\n";
                help << "  :100               - Milliseconds\n";
                help << "  :1s                - Seconds\n";
                help << "  :1m30s             - Minutes and seconds\n";
                help << "  :0:0:30.500        - HH:MM:SS.mmm format\n";
              } else if (module == "struct") {
                help << "\n=== Structs ===\n\n";
                help << "Syntax:\n";
                help << "  struct Name {\n";
                help << "    field1\n";
                help << "    field2\n";
                help << "    fn init(args) { this.field = args }\n";
                help << "    fn method() { ... }\n";
                help << "  }\n";
                help << "\nConstruction:\n";
                help << "  let obj = Name.new(args)  - Constructor\n";
                help << "  let obj = Name(args)      - Sugar (same as above)\n";
                help << "\nMethod calls:\n";
                help << "  obj.method()              - Instance method\n";
                help << "  obj.field                 - Field access\n";
              } else if (module == "const") {
                help << "\n=== Const ===\n\n";
                help << "Syntax:\n";
                help << "  const name = value      - Immutable binding\n";
                help << "\nConst prevents reassignment:\n";
                help << "  const x = 10\n";
                help << "  x = 20    // Error!\n";
                help << "\nNote: Object properties can still be modified:\n";
                help << "  const obj = {a: 1}\n";
                help << "  obj.a = 2   // OK\n";
                help << "  obj = {}    // Error!\n";
              } else if (module == "image") {
                help << "\n=== Image Module ===\n\n";
                help << "Functions:\n";
                help << "  image.find(path, [tolerance])  - Find image on screen\n";
                help << "  image.findAll(path)            - Find all occurrences\n";
                help << "  image.wait(path, timeout)      - Wait for image to appear\n";
              } else if (module == "ocr") {
                help << "\n=== OCR Module ===\n\n";
                help << "Functions:\n";
                help << "  ocr.read(image, [lang])  - Extract text from image\n";
                help << "  ocr.readRegion(x,y,w,h)  - OCR on screen region\n";
              } else if (module == "automation") {
                help << "\n=== Automation Module ===\n\n";
                help << "Functions:\n";
                help << "  automation.start(type, params)  - Start automation task\n";
                help << "  automation.stop(type)           - Stop automation task\n";
                help << "  automation.toggle(type)         - Toggle automation\n";
              } else if (module == "physics") {
                help << "\n=== Physics Module ===\n\n";
                help << "Functions:\n";
                help << "  physics.distance(x1,y1,x2,y2)  - Distance between points\n";
                help << "  physics.angle(x1,y1,x2,y2)     - Angle between points\n";
                help << "  physics.lerp(a,b,t)            - Linear interpolation\n";
              } else if (module == "process") {
                help << "\n=== Process Module ===\n\n";
                help << "Functions:\n";
                help << "  process.list()           - List running processes\n";
                help << "  process.byName(name)     - Find process by name\n";
                help << "  process.byPid(pid)       - Get process by PID\n";
                help << "  process.kill(pid)        - Kill process\n";
              } else if (module == "http") {
                help << "\n=== HTTP Module ===\n\n";
                help << "Functions:\n";
                help << "  http.get(url)            - GET request\n";
                help << "  http.post(url, data)     - POST request\n";
                help << "  http.put(url, data)      - PUT request\n";
                help << "  http.delete(url)         - DELETE request\n";
              } else if (module == "regex") {
                help << "\n=== Regex Module ===\n\n";
                help << "Functions:\n";
                help << "  regex.match(text, pattern)     - Match pattern\n";
                help << "  regex.replace(text, pat, repl) - Replace matches\n";
                help << "  regex.split(text, pattern)     - Split by pattern\n";
              } else if (module == "altTab") {
                help << "\n=== AltTab Module ===\n\n";
                help << "Functions:\n";
                help << "  altTab.show()     - Show window switcher\n";
                help << "  altTab.next()     - Next window\n";
                help << "  altTab.previous() - Previous window\n";
                help << "  altTab.hide()     - Hide switcher\n";
              } else if (module == "mapmanager") {
                help << "\n=== MapManager Module ===\n\n";
                help << "Functions:\n";
                help << "  mapmanager.load(file)     - Load key mappings\n";
                help << "  mapmanager.save(file)     - Save mappings\n";
                help << "  mapmanager.add(key, act)  - Add mapping\n";
                help << "  mapmanager.remove(key)    - Remove mapping\n";
                help << "  mapmanager.list()         - List all mappings\n";
              } else if (module == "config") {
                help << "\n=== Config Module ===\n\n";
                help << "Access configuration values:\n";
                help << "  config.get(key)    - Get config value\n";
                help << "  config.set(k, v)   - Set config value\n";
              } else {
                help << "\nUnknown module: " << module << "\n";
                help << "Use help() to see available modules.\n";
              }
            }

            std::cout << help.str();
            return HavelValue(nullptr);
          }));
}

void Interpreter::visitConditionalHotkey(const ast::ConditionalHotkey &node) {
  // Extract the hotkey string for use with HotkeyManager
  std::string hotkeyStr;
  if (!node.binding->hotkeys.empty()) {
    if (auto *hotkeyLit = dynamic_cast<const ast::HotkeyLiteral *>(
            node.binding->hotkeys[0].get())) {
      hotkeyStr = hotkeyLit->combination;
    }
  }

  if (hotkeyStr.empty()) {
    lastResult =
        HavelRuntimeError("Invalid hotkey in conditional hotkey binding");
    return;
  }

  if (hotkeyManager) {
    // Create a lambda that captures the condition expression and
    // re-evaluates it
    // Capture shared_from_this() to ensure Interpreter stays alive during execution
    auto self = shared_from_this();
    auto destroyedFlag = m_destroyed;
    auto conditionFunc = [self, destroyedFlag, condExpr = node.condition.get()]() -> bool {
      if (destroyedFlag->load()) return false;  // Interpreter destroyed, skip evaluation
      try {
        auto result = self->Evaluate(*condExpr);
        if (isError(result)) {
          // Log error but return false to prevent the hotkey from triggering
          return false;
        }
        return Interpreter::ValueToBool(unwrap(result));
      } catch (const std::exception& e) {
        // Catch any exceptions to prevent them escaping into Qt event loop
        std::cerr << "Conditional hotkey condition evaluation failed: " << e.what() << std::endl;
        return false;
      }
    };

    // Create the action callback
    auto actionFunc = [self, destroyedFlag, action = node.binding->action.get()]() {
      if (destroyedFlag->load()) return;  // Interpreter destroyed, skip action
      try {
        if (action) {
          // Lock interpreter mutex to protect environment and lastResult
          std::lock_guard<std::mutex> lock(self->interpreterMutex);
          auto result = self->Evaluate(*action);
          if (isError(result)) {
            std::cerr << "Conditional hotkey action evaluation failed: "
                      << std::get<HavelRuntimeError>(result).what() << std::endl;
          }
        }
      } catch (const std::exception& e) {
        // Catch any exceptions to prevent them escaping into Qt event loop
        std::cerr << "Conditional hotkey action threw exception: " << e.what() << std::endl;
      }
    };

    // Register as a contextual hotkey with the HotkeyManager using
    // function-based condition
    hotkeyManager->AddContextualHotkey(hotkeyStr, conditionFunc, actionFunc);
    lastResult = nullptr;
  } else {
    // Fallback: static evaluation if HotkeyManager is not available
    auto conditionResult = Evaluate(*node.condition);
    if (isError(conditionResult)) {
      lastResult = conditionResult;
      return;
    }

    bool conditionMet = ValueToBool(unwrap(conditionResult));

    if (conditionMet) {
      // If condition is true, register the hotkey binding normally
      visitHotkeyBinding(*node.binding);
    } else {
      // If condition is false, we don't register the hotkey, and we
      // don't evaluate the action Only register the hotkey if the
      // condition is initially true
      lastResult = nullptr;
    }
  }
}

void Interpreter::visitWhenBlock(const ast::WhenBlock &node) {
  // For each statement in the when block, wrap it with the shared
  // condition
  auto self = shared_from_this();
  for (const auto &stmt : node.statements) {
    // Check if it's a hotkey binding
    if (auto *hotkeyBinding =
            dynamic_cast<const ast::HotkeyBinding *>(stmt.get())) {
      // Extract hotkey string
      std::string hotkeyStr;
      if (!hotkeyBinding->hotkeys.empty()) {
        if (auto *hotkeyLit = dynamic_cast<const ast::HotkeyLiteral *>(
                hotkeyBinding->hotkeys[0].get())) {
          hotkeyStr = hotkeyLit->combination;
        }
      }

      if (hotkeyStr.empty()) {
        lastResult = HavelRuntimeError("Invalid hotkey in when block");
        return;
      }

      if (hotkeyManager) {
        // Create a lambda that captures the shared condition
        // Capture shared_from_this() to ensure Interpreter stays alive during execution
        auto destroyedFlag = m_destroyed;
        auto conditionFunc = [self, destroyedFlag, condExpr = node.condition.get()]() -> bool {
          if (destroyedFlag->load()) return false;  // Interpreter destroyed
          auto result = self->Evaluate(*condExpr);
          if (isError(result)) {
            // Log error but return false to prevent the hotkey from
            // triggering
            return false;
          }
          return Interpreter::ValueToBool(unwrap(result));
        };

        // Create the action callback
        auto actionFunc = [self, destroyedFlag, action = hotkeyBinding->action.get()]() {
          if (destroyedFlag->load()) return;  // Interpreter destroyed
          if (action) {
            // Lock interpreter mutex to protect environment and lastResult
            std::lock_guard<std::mutex> lock(self->interpreterMutex);
            auto result = self->Evaluate(*action);
            if (isError(result)) {
              std::cerr << "When block hotkey action failed: "
                        << std::get<HavelRuntimeError>(result).what()
                        << std::endl;
            }
          }
        };

        // Register as contextual hotkey with shared condition
        hotkeyManager->AddContextualHotkey(hotkeyStr, conditionFunc,
                                           actionFunc);
      }
    } else if (auto *conditionalHotkey =
                   dynamic_cast<const ast::ConditionalHotkey *>(stmt.get())) {
      // Handle nested conditional hotkeys inside when blocks
      // Combine the outer condition with the inner condition
      if (hotkeyManager) {
        // Extract hotkey string from the nested binding
        std::string hotkeyStr;
        if (!conditionalHotkey->binding->hotkeys.empty()) {
          if (auto *hotkeyLit = dynamic_cast<const ast::HotkeyLiteral *>(
                  conditionalHotkey->binding->hotkeys[0].get())) {
            hotkeyStr = hotkeyLit->combination;
          }
        }

        if (hotkeyStr.empty()) {
          lastResult = HavelRuntimeError(
              "Invalid hotkey in conditional hotkey within when block");
          return;
        }

        // Create a combined condition that requires both outer and
        // inner conditions
        // Capture shared_from_this() to ensure Interpreter stays alive during execution
        auto destroyedFlag = m_destroyed;
        auto combinedConditionFunc =
            [self, destroyedFlag, outerCond = node.condition.get(),
             innerCond = conditionalHotkey->condition.get()]() -> bool {
          if (destroyedFlag->load()) return false;  // Interpreter destroyed
          // Evaluate outer condition
          auto outerResult = self->Evaluate(*outerCond);
          if (isError(outerResult) || !Interpreter::ValueToBool(unwrap(outerResult))) {
            return false;
          }

          // Evaluate inner condition
          auto innerResult = self->Evaluate(*innerCond);
          if (isError(innerResult) || !Interpreter::ValueToBool(unwrap(innerResult))) {
            return false;
          }

          return true;
        };

        // Create the action callback from the inner binding
        auto actionFunc =
            [self, destroyedFlag, action = conditionalHotkey->binding->action.get()]() {
              if (destroyedFlag->load()) return;  // Interpreter destroyed
              if (action) {
                // Lock interpreter mutex to protect environment and lastResult
                std::lock_guard<std::mutex> lock(self->interpreterMutex);
                auto result = self->Evaluate(*action);
                if (isError(result)) {
                  std::cerr << "Nested conditional hotkey action failed: "
                            << std::get<HavelRuntimeError>(result).what()
                            << std::endl;
                }
              }
            };

        // Register with combined condition
        hotkeyManager->AddContextualHotkey(hotkeyStr, combinedConditionFunc,
                                           actionFunc);
      }
    } else if (auto *whenBlock =
                   dynamic_cast<const ast::WhenBlock *>(stmt.get())) {
      // Handle nested when blocks - inherit the condition by creating a
      // combined condition
      if (hotkeyManager) {
        // Create a combined condition that requires both the outer and
        // inner conditions
        auto destroyedFlag = m_destroyed;
        auto combinedConditionFunc =
            [self, destroyedFlag, outerCond = node.condition.get(),
             innerCond = whenBlock->condition.get()]() -> bool {
          if (destroyedFlag->load()) return false;  // Interpreter destroyed
          // Evaluate outer condition
          auto outerResult = self->Evaluate(*outerCond);
          if (isError(outerResult) || !Interpreter::ValueToBool(unwrap(outerResult))) {
            return false;
          }

          // Evaluate inner condition
          auto innerResult = self->Evaluate(*innerCond);
          if (isError(innerResult) || !Interpreter::ValueToBool(unwrap(innerResult))) {
            return false;
          }

          return true;
        };

        // For nested when blocks, we need to recursively process their
        // statements with the combined condition. For simplicity, we'll
        // just evaluate inner when blocks with the combined condition,
        // but a more complete implementation would recursively process
        // each statement inside the nested when block.
        for (const auto &innerStmt : whenBlock->statements) {
          // Process each statement in the nested when block with the
          // combined condition
          if (auto *innerHotkeyBinding =
                  dynamic_cast<const ast::HotkeyBinding *>(innerStmt.get())) {
            // Extract hotkey string from the nested statement
            std::string innerHotkeyStr;
            if (!innerHotkeyBinding->hotkeys.empty()) {
              if (auto *hotkeyLit = dynamic_cast<const ast::HotkeyLiteral *>(
                      innerHotkeyBinding->hotkeys[0].get())) {
                innerHotkeyStr = hotkeyLit->combination;
              }
            }

            if (innerHotkeyStr.empty()) {
              continue; // Skip invalid hotkeys
            }

            // Use the same action
            auto innerActionFunc =
                [self, action = innerHotkeyBinding->action.get()]() {
                  if (action) {
                    // Lock interpreter mutex to protect environment and lastResult
                    std::lock_guard<std::mutex> lock(self->interpreterMutex);
                    auto result = self->Evaluate(*action);
                    if (isError(result)) {
                      std::cerr << "Nested when block hotkey action failed: "
                                << std::get<HavelRuntimeError>(result).what()
                                << std::endl;
                    }
                  }
                };

            // Register with the combined condition (outer && inner)
            hotkeyManager->AddContextualHotkey(
                innerHotkeyStr, combinedConditionFunc, innerActionFunc);
          }
        }
      }
    } else {
      // Non-hotkey statements in when block - evaluate once statically
      auto result = Evaluate(*stmt);
      if (isError(result)) {
        lastResult = result;
        return;
      }
    }
  }

  lastResult = nullptr;
}

// ============================================================================
// Script Auto-Reload Implementation
// ============================================================================

void Interpreter::enableReload() {
  reloadEnabled.store(true);
  if (scriptPath.empty()) {
    warn("Cannot enable auto-reload: no script path set");
    return;
  }
  startReloadWatcher();
  info("Auto-reload enabled for script: {}", scriptPath);
}

void Interpreter::disableReload() {
  reloadEnabled.store(false);
  stopReloadWatcher();
  info("Auto-reload disabled");
}

void Interpreter::toggleReload() {
  if (reloadEnabled.load()) {
    disableReload();
  } else {
    enableReload();
  }
}

void Interpreter::startReloadWatcher() {
  if (reloadWatcherRunning.load() || scriptPath.empty()) {
    return;
  }
  
  reloadWatcherRunning.store(true);
  reloadWatcherThread = std::thread([this]() {
    namespace fs = std::filesystem;
    
    // Get initial modification time
    try {
      if (fs::exists(scriptPath)) {
        lastModifiedTime = fs::last_write_time(scriptPath);
      }
    } catch (...) {
      // File doesn't exist yet or can't be accessed
    }
    
    while (reloadWatcherRunning.load() && reloadEnabled.load()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      
      try {
        if (fs::exists(scriptPath)) {
          auto currentTime = fs::last_write_time(scriptPath);
          if (currentTime > lastModifiedTime) {
            lastModifiedTime = currentTime;
            info("Script file changed, triggering reload...");
            triggerReload();
          }
        }
      } catch (...) {
        // File access error, continue watching
      }
    }
  });
}

void Interpreter::stopReloadWatcher() {
  reloadWatcherRunning.store(false);
  if (reloadWatcherThread.joinable()) {
    reloadWatcherThread.join();
  }
}

void Interpreter::triggerReload() {
  std::lock_guard<std::mutex> lock(reloadMutex);
  
  if (!reloadEnabled.load() || scriptPath.empty()) {
    return;
  }

  // Clear all hotkeys before reload
  if (hotkeyManager) {
    info("Clearing hotkeys before reload...");
    hotkeyManager->clearAllHotkeys();
  }

  // Execute on reload handler
  executeOnReload();

  // Re-execute the script
  try {
    std::ifstream file(scriptPath);
    if (file) {
      std::stringstream buffer;
      buffer << file.rdbuf();
      std::string code = buffer.str();
      Execute(code);
      info("Script reloaded successfully");
    }
  } catch (const std::exception& e) {
    error("Failed to reload script: {}", e.what());
  }
}

    ));
}

} // namespace havel::modules
