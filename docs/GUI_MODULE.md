# GUI Module

The GUI module provides dialog and window management functions for Havel.

## Initialization

GUI functions require a QApplication to be running. Use `gui.initialize()` to set up the GUI at runtime.

### `gui.initialize()`

Initialize the GUI system and create QApplication if not already running.

```havel
// Initialize GUI (required before using other gui.* functions)
gui.initialize()

// Now you can use GUI dialogs
let result = gui.input("Enter your name")
gui.notify("Hello", "Welcome, " + result)
```

**Returns:** `true` if successful

### `gui.reload()`

Reload GUI components. Useful after configuration changes.

```havel
gui.initialize()
// ... make changes ...
gui.reload()
```

**Returns:** `true` if successful

**Error:** Returns error if GUI not initialized

### `gui.isInitialized()`

Check if GUI system is initialized.

```havel
if !gui.isInitialized() {
    gui.initialize()
}
```

**Returns:** `true` if QApplication exists

## Dialog Functions

### `gui.input(title, [prompt], [default])`

Show text input dialog.

```havel
let name = gui.input("Enter Name", "What is your name?", "John")
```

### `gui.password(title, [prompt])`

Show password input dialog (masked).

```havel
let pass = gui.password("Security", "Enter password:")
```

### `gui.confirm(title, message)`

Show confirmation dialog.

```havel
if gui.confirm("Exit", "Are you sure you want to exit?") {
    app.quit()
}
```

### `gui.notify(title, message)`

Show notification/toast.

```havel
gui.notify("Download Complete", "Your file has been downloaded")
```

### `gui.showMenu(title, options)`

Show selection menu.

```havel
let choice = gui.showMenu("Select Action", ["Open", "Save", "Cancel"])
```

### `gui.fileDialog([title], [directory], [filter])`

Show file open dialog.

```havel
let file = gui.fileDialog("Open File", "/home/user", "*.txt")
```

### `gui.directoryDialog([title], [directory])`

Show directory selection dialog.

```havel
let dir = gui.directoryDialog("Select Folder", "/home/user")
```

### `gui.colorPicker([title], [defaultColor])`

Show color picker dialog.

```havel
let color = gui.colorPicker("Choose Color", "#FF0000")
```

## Window Effects

### `gui.setTransparency(windowId, opacity)`

Set window transparency by window ID.

```havel
let win = window.active()
gui.setTransparency(win.id, 0.8)  // 80% opaque
```

### `gui.setWindowTransparency(windowId, opacity)`

Alias for `setTransparency`.

### `gui.setTransparencyByTitle(title, opacity)`

Set window transparency by title.

```havel
gui.setTransparencyByTitle("Firefox", 0.9)
```

## Complete Example

```havel
// Initialize GUI
if !gui.isInitialized() {
    gui.initialize()
}

// Show menu
let choice = gui.showMenu("Main Menu", [
    "Take Screenshot",
    "Show Info",
    "Exit"
])

if choice == "Take Screenshot" {
    screenshot.full()
    gui.notify("Done", "Screenshot captured")
} else if choice == "Show Info" {
    let info = gui.input("System Info", "Enter system to query:")
    gui.notify("Info", "Querying " + info)
} else {
    gui.notify("Bye", "Goodbye!")
    app.quit()
}
```

## Notes

- `gui.initialize()` creates QApplication - call once at startup
- GUI functions block execution until dialog closed
- Returns empty string/null if user cancels dialog
- Opacity values: 0.0 (transparent) to 1.0 (opaque)
