# GTK and ImGui Extensions for Havel

This document describes the GTK4 and Dear ImGui extensions for the Havel scripting language.

## Overview

Havel supports two powerful UI framework extensions with **dynamic loading**:

1. **GTK4 Extension** - Native GTK4 widget toolkit for traditional GUI applications
2. **ImGui Extension** - Dear ImGui immediate-mode GUI for real-time applications and tools

### Key Feature: Dynamic Loading

Both extensions use **runtime dynamic loading** (dlopen/dlsym) to load their dependencies:

- **No compile-time dependencies** - Extensions build without requiring GTK4/ImGui installed
- **Lazy loading** - Libraries are only loaded when you actually call extension functions
- **Graceful degradation** - If libraries aren't available, functions return errors instead of crashing
- **Smaller distribution** - Only the extension .so files are needed

## Installation

### Build Requirements

The extensions build with just the core Havel dependencies. No GTK4 or ImGui libraries needed at build time!

```bash
./build.sh 1 build
# or
make
```

### Runtime Requirements

For the extensions to work at runtime, you need the respective libraries installed:

#### GTK4 Extension Runtime

**Ubuntu/Debian:**
```bash
sudo apt install libgtk-4-dev libgtk-4-1
```

**Fedora/RHEL:**
```bash
sudo dnf install gtk4 gtk4-devel
```

**Arch Linux:**
```bash
sudo pacman -S gtk4
```

#### ImGui Extension Runtime

**Ubuntu/Debian:**
```bash
sudo apt install libglfw3 libglfw3-dev libgl1-mesa-glx libgl1-mesa-dev
```

**Fedora/RHEL:**
```bash
sudo dnf install glfw mesa-libGL mesa-libGLU
```

**Arch Linux:**
```bash
sudo pacman -S glfw-x11 mesa glu
```

## Extension Loading

Extensions are loaded dynamically using the `import` statement:

```havel
import gtk from "extension:gtk"
import imgui from "extension:imgui"
```

The extension `.so` files should be in:
- `/usr/lib/havel/extensions/`
- `/usr/local/lib/havel/extensions/`
- `~/.havel/extensions/`
- Or specify with `HAVEL_EXTENSION_DIR` environment variable

## GTK4 Extension API

### Core Functions

```havel
import gtk from "extension:gtk"

// Initialize GTK (libraries loaded here on first use)
if (!gtk.init()) {
    print("Failed to load GTK4")
    return
}

gtk.run()      // Run main loop (blocking)
gtk.quit()     // Quit main loop
```

### Windows

```havel
// Create window
let win = gtk.windowNew("My Application")
gtk.windowShow(win)
gtk.windowSetTitle(win, "New Title")
gtk.windowSetSize(win, 800, 600)
gtk.windowSetPosition(win, 100, 100)

// Window properties
gtk.windowSetResizable(win, true)
gtk.windowSetModal(win, true)
gtk.windowMaximize(win)
gtk.windowMinimize(win)

// Get size
let size = gtk.windowGetSize(win)  // {width, height}

// Set child widget
gtk.windowSetChild(win, childWidget)

// Close/destroy
gtk.windowClose(win)
gtk.windowHide(win)
gtk.widgetDestroy(win)
```

### Labels

```havel
// Create label
let label = gtk.labelNew("Hello World")

// Set/get text
gtk.labelSetText(label, "New text")
let text = gtk.labelGetText(label)

// Markup (Pango formatting)
gtk.labelSetMarkup(label, "<b>Bold</b> <i>Italic</i>")

// Properties
gtk.labelSetSelectable(label, true)
gtk.labelSetJustify(label, 2)  // 0=left, 1=right, 2=center, 3=fill
```

### Buttons

```havel
// Create buttons
let btn1 = gtk.buttonNew()
let btn2 = gtk.buttonNewWithLabel("Click Me")

// Set label/icon
gtk.buttonSetLabel(btn, "New Label")
gtk.buttonSetIconName(btn, "gtk-ok")

// Trigger click programmatically
gtk.buttonClicked(btn)
```

### Toggle Buttons & Check Buttons

```havel
// Toggle button
let toggle = gtk.toggleButtonNew()
gtk.toggleButtonSetActive(toggle, true)
let isActive = gtk.toggleButtonGetActive(toggle)

// Check button
let check = gtk.checkButtonNew()
let checkWithLabel = gtk.checkButtonNewWithLabel("Option")
```

### Switches

```havel
let sw = gtk.switchNew()
gtk.switchSetActive(sw, true)
let active = gtk.switchGetActive(sw)
```

### Text Entry

```havel
// Create entry
let entry = gtk.entryNew()

// Set/get text
gtk.entrySetText(entry, "Initial text")
let text = gtk.entryGetText(entry)

// Placeholder text
gtk.entrySetPlaceholderText(entry, "Type here...")

// Properties
gtk.entrySetEditable(entry, false)
gtk.entrySetVisibility(entry, false)  // For passwords
gtk.entrySetMaxLength(entry, 100)

// Specialized entries
let searchEntry = gtk.searchEntryNew()
let passwordEntry = gtk.passwordEntryNew()
```

### Layout Containers

#### Box (Horizontal/Vertical)

```havel
// Create box
let vbox = gtk.boxNew("vertical", 5)   // orientation, spacing
let hbox = gtk.boxNew("horizontal", 10)

// Add widgets
gtk.boxAppend(box, widget)
gtk.boxPrepend(box, widget)

// Properties
gtk.boxSetSpacing(box, 10)
gtk.boxSetHomogeneous(box, true)
```

#### Grid

```havel
let grid = gtk.gridNew()

// Attach widget at column, row with colspan, rowspan
gtk.gridAttach(grid, widget, 0, 0, 1, 1)

// Spacing
gtk.gridSetRowSpacing(grid, 5)
gtk.gridSetColumnSpacing(grid, 10)
```

#### Frame

```havel
let frame = gtk.frameNew("Frame Title")
gtk.frameSetLabel(frame, "New Title")
gtk.frameSetChild(frame, childWidget)
```

#### Scrolled Window

```havel
let scrolled = gtk.scrolledWindowNew()
gtk.scrolledWindowSetChild(scrolled, textView)

// Set scrollbar policy
gtk.scrolledWindowSetPolicy(scrolled, "automatic", "automatic")
// Options: "automatic", "always", "never", "external"
```

### Text View

```havel
let textView = gtk.textViewNew()
let buffer = gtk.textViewGetBuffer(textView)

// Set/get text
gtk.textBufferSetText(buffer, "Content")
let text = gtk.textBufferGetText(buffer)

// Properties
gtk.textViewSetEditable(textView, false)
gtk.textViewSetWrapMode(textView, 1)  // Word wrap
```

### Images

```havel
let image = gtk.imageNew()
let imageFromIcon = gtk.imageNewFromIconName("gtk-ok")
gtk.imageSetFromIconName(image, "gtk-cancel")
```

### Progress Bar

```havel
let progress = gtk.progressBarNew()
gtk.progressBarSetFraction(progress, 0.5)  // 0.0 to 1.0
gtk.progressBarSetText(progress, "Loading...")
gtk.progressBarPulse(progress)  // Activity mode
```

### Spinner (Loading Indicator)

```havel
let spinner = gtk.spinnerNew()
gtk.spinnerStart(spinner)
gtk.spinnerStop(spinner)
```

### Combo Box (Dropdown)

```havel
let combo = gtk.comboBoxTextNew()
gtk.comboBoxTextAppend(combo, "id1", "Option 1")
gtk.comboBoxTextAppend(combo, "id2", "Option 2")
gtk.comboBoxTextSetActiveId(combo, "id1")
let activeId = gtk.comboBoxTextGetActiveId(combo)
```

### Notebook (Tabs)

```havel
let notebook = gtk.notebookNew()

// Add pages
let pageNum = gtk.notebookAppendPage(notebook, childWidget, tabLabel)
gtk.notebookSetCurrentPage(notebook, 0)
```

### Stack (Stacked Views)

```havel
let stack = gtk.stackNew()
gtk.stackAddTitled(stack, child1, "name1", "Tab 1")
gtk.stackAddTitled(stack, child2, "name2", "Tab 2")
gtk.stackSetVisibleChildName(stack, "name1")
```

### Expander (Collapsible Section)

```havel
let expander = gtk.expanderNew("Options")
gtk.expanderSetExpanded(expander, true)
```

### Header Bar

```havel
let headerbar = gtk.headerBarNew()
gtk.headerBarSetTitle(headerbar, "Title")
gtk.headerBarPackStart(headerbar, widget)
gtk.headerBarPackEnd(headerbar, widget)
```

### Menu Bar

```havel
let menubar = gtk.menuBarNew()
let menuItem = gtk.menuItemNewWithLabel("File")
gtk.menuBarAppend(menubar, menuItem)
```

### Popover

```havel
let popover = gtk.popoverNew()
gtk.popoverPopup(popover)
```

### Dialogs

```havel
// Basic dialog
let dialog = gtk.dialogNew()
gtk.dialogAddButton(dialog, "OK", -3)  // -3 = GTK_RESPONSE_OK
let response = gtk.dialogRun(dialog)

// Message dialog
let msgDialog = gtk.messageDialogNew(parent, 2, 1)  // type=INFO, buttons=OK
gtk.messageDialogSetMarkup(msgDialog, "<b>Warning!</b>")
```

### Widget Base Functions

```havel
// Visibility and state
gtk.widgetSetVisible(widget, true)
gtk.widgetSetSensitive(widget, false)
gtk.widgetSetTooltipText(widget, "Tooltip")

// Styling
gtk.widgetAddCssClass(widget, "my-class")

// Sizing
gtk.widgetSetSizeRequest(widget, 100, 50)

// Focus
gtk.widgetGrabFocus(widget)
```

### Separator

```havel
let hSeparator = gtk.separatorNew("horizontal")
let vSeparator = gtk.separatorNew("vertical")
```

### Complete GTK Example

```havel
import gtk from "extension:gtk"

// Initialize - GTK4 libraries loaded here
if (!gtk.init()) {
    print("Failed to load GTK4. Install libgtk-4-1")
    return
}

// Create main window
let win = gtk.windowNew("My App")
gtk.windowSetSize(win, 400, 300)

// Create layout
let vbox = gtk.boxNew("vertical", 10)

// Add label
let label = gtk.labelNew("Welcome to Havel + GTK4!")
gtk.boxAppend(vbox, label)

// Add button
let button = gtk.buttonNewWithLabel("Click Me")
gtk.boxAppend(vbox, button)

// Add entry
let entry = gtk.entryNew()
gtk.entrySetPlaceholderText(entry, "Type something...")
gtk.boxAppend(vbox, entry)

// Add progress bar
let progress = gtk.progressBarNew()
gtk.progressBarSetFraction(progress, 0.5)
gtk.boxAppend(vbox, progress)

// Set content
gtk.windowSetChild(win, vbox)
gtk.windowShow(win)

// Run main loop
gtk.run()
```

## ImGui Extension API

See the separate ImGui documentation for the complete API reference.

## Widget Reference Table

| Category | Widgets |
|----------|---------|
| **Basic** | Label, Button, ToggleButton, CheckButton, Switch |
| **Text Input** | Entry, SearchEntry, PasswordEntry, TextView |
| **Layout** | Box, Grid, Fixed, Frame, Separator |
| **Containers** | ScrolledWindow, Viewport, Expander, Revealer |
| **Multi-page** | Notebook, Stack |
| **Selection** | ComboBoxText, ListBox, FlowBox |
| **Display** | Image, ProgressBar, LevelBar, Scale, SpinButton |
| **Menus** | MenuBar, MenuItem, Popover |
| **Dialogs** | Dialog, MessageDialog, AboutDialog |
| **Bars** | HeaderBar, ActionBar |
| **Loading** | Spinner |

## Troubleshooting

### Extension Not Found

```bash
ls build-release/src/extensions/gtk/gtk_extension.so
sudo cp build-release/src/extensions/gtk/gtk_extension.so /usr/lib/havel/extensions/
```

### GTK4 Not Loading at Runtime

```bash
pkg-config --modversion gtk4
ldconfig -p | grep gtk-4
sudo apt install libgtk-4-1
```

### Debug Loading

The extension prints diagnostic messages:

```
[GTK] GTK4 libraries loaded dynamically
```

If loading fails:

```
[GTK] Failed to load GTK4 libraries
```

## License

MIT License - same as Havel project.
