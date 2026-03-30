# Qt Extension for Havel

This document describes the Qt6 extension for the Havel scripting language.

## Overview

The **Qt Extension** provides native Qt6/Qt5 widget toolkit capabilities for building traditional desktop GUI applications with the Havel scripting language.

### Key Feature: Dynamic Loading

The Qt extension uses **runtime dynamic loading** (dlopen/dlsym) to load Qt libraries:

- **No compile-time dependencies** - Builds without Qt installed
- **Lazy loading** - Qt libraries loaded only when `qt.init()` is called
- **Qt6 and Qt5 compatible** - Automatically tries Qt6 first, falls back to Qt5
- **Graceful errors** - Returns error values if Qt not available

## Installation

### Build Requirements

The extension builds with just the core Havel dependencies. No Qt libraries needed at build time!

```bash
./build.sh 1 build
# or
make
```

### Runtime Requirements

For the extension to work at runtime, you need Qt installed:

**Ubuntu/Debian:**
```bash
# Qt6
sudo apt install libqt6core6 libqt6gui6 libqt6widgets6

# Or Qt5
sudo apt install libqt5core5a libqt5gui5 libqt5widgets5
```

**Fedora/RHEL:**
```bash
# Qt6
sudo dnf install qt6-qtbase qt6-qtbase-gui qt6-qtwidgets

# Or Qt5
sudo dnf install qt5-qtbase qt5-qtbase-gui qt5-qtwidgets
```

**Arch Linux:**
```bash
sudo pacman -S qt6-base
# or
sudo pacman -S qt5-base
```

## Extension Loading

```havel
import qt from "extension:qt"
```

The extension `.so` file should be in:
- `/usr/lib/havel/extensions/`
- `/usr/local/lib/havel/extensions/`
- `~/.havel/extensions/`

## Qt Extension API

### Core Functions

```havel
import qt from "extension:qt"

// Initialize Qt (libraries loaded here on first use)
if (!qt.init()) {
    print("Failed to load Qt")
    return
}

// Application settings
qt.setApplicationName("My App")
qt.setOrganizationName("MyOrg")

// Run event loop (blocking)
qt.exec()

// Quit event loop
qt.quit()

// Process pending events
qt.processEvents()
```

### Main Window

```havel
// Create main window
let win = qt.mainWindowNew()
qt.widgetSetWindowTitle(win, "My Application")
qt.widgetResize(win, 800, 600)
qt.widgetMove(win, 100, 100)

// Create central widget and set it
let central = qt.mainWindowNew()
qt.mainWindowSetCentralWidget(win, central)

// Show window
qt.widgetShow(win)
qt.widgetHide(win)
qt.widgetClose(win)
```

### Widget Base Functions

```havel
// Visibility and state
qt.widgetSetVisible(widget, true)
qt.widgetSetEnabled(widget, false)
qt.widgetSetFocus(widget)
qt.widgetUpdate(widget)

// Window properties
qt.widgetSetWindowTitle(widget, "Title")
qt.widgetSetStyleSheet(widget, "background: red;")
qt.widgetSetToolTip(widget, "Helpful tip")

// Geometry
qt.widgetResize(widget, 200, 100)
qt.widgetMove(widget, x, y)
qt.widgetSetGeometry(widget, x, y, w, h)
```

### QLabel

```havel
// Create label
let label = qt.labelNew("Hello World")

// Set text
qt.labelSetText(label, "New text")

// Alignment (use Qt constants)
qt.labelSetAlignment(label, 0x0084)  // AlignCenter
qt.labelSetAlignment(label, 0x0001)  // AlignLeft
qt.labelSetAlignment(label, 0x0002)  // AlignRight

// Word wrap
qt.labelSetWordWrap(label, true)
```

### QPushButton

```havel
// Create button
let btn = qt.pushButtonNew("Click Me")

// Set text
qt.pushButtonSetText(btn, "New Text")

// Checkable button (toggle)
qt.pushButtonSetCheckable(btn, true)
qt.pushButtonSetChecked(btn, true)
let checked = qt.pushButtonIsChecked(btn)

// Trigger click programmatically
qt.pushButtonClick(btn)

// Flat button (no border)
qt.pushButtonSetFlat(btn, true)
```

### QLineEdit

```havel
// Create line edit
let edit = qt.lineEditNew("Initial text")

// Get/set text
qt.lineEditSetText(edit, "New text")
let text = qt.lineEditText(edit)

// Placeholder text
qt.lineEditSetPlaceholderText(edit, "Type here...")

// Read-only
qt.lineEditSetReadOnly(edit, true)

// Clear
qt.lineEditClear(edit)

// Echo mode (for passwords)
// 0 = Normal, 1 = NoEcho, 2 = Password
qt.lineEditSetEchoMode(edit, 2)
```

### QTextEdit

```havel
// Create text edit
let textEdit = qt.textEditNew("")

// Plain text
qt.textEditSetPlainText(edit, "Content")
let text = qt.textEditToPlainText(edit)

// HTML
qt.textEditSetHtml(edit, "<b>Bold</b> <i>Italic</i>")
let html = qt.textEditToHtml(edit)

// Append text
qt.textEditAppend(edit, "More text")

// Clear
qt.textEditClear(edit)

// Read-only
qt.textEditSetReadOnly(edit, true)

// Placeholder
qt.textEditSetPlaceholderText(edit, "Enter text...")

// Clipboard operations
qt.textEditCopy(edit)
qt.textEditCut(edit)
qt.textEditPaste(edit)
```

### QComboBox

```havel
// Create combo box
let combo = qt.comboBoxNew()

// Add items
qt.comboBoxAddItem(combo, "Option 1")
qt.comboBoxAddItem(combo, "Option 2")
qt.comboBoxInsertItem(combo, 0, "First")

// Remove/clear
qt.comboBoxRemoveItem(combo, 0)
qt.comboBoxClear(combo)

// Get/set selection
qt.comboBoxSetCurrentIndex(combo, 0)
let index = qt.comboBoxCurrentIndex(combo)
let text = qt.comboBoxCurrentText(combo)
let itemText = qt.comboBoxItemText(combo, index)

// Count
let count = qt.comboBoxCount(combo)

// Editable
qt.comboBoxSetEditable(combo, true)

// Max visible items
qt.comboBoxSetMaxVisibleItems(combo, 10)
```

### QSpinBox

```havel
// Create spin box
let spin = qt.spinBoxNew()

// Range
qt.spinBoxSetRange(spin, 0, 100)
qt.spinBoxSetMinimum(spin, 0)
qt.spinBoxSetMaximum(spin, 100)

// Value
qt.spinBoxSetValue(spin, 50)
let value = qt.spinBoxValue(spin)

// Step
qt.spinBoxSetSingleStep(spin, 5)

// Prefix/Suffix
qt.spinBoxSetPrefix(spin, "$")
qt.spinBoxSetSuffix(spin, " kg")

// Wrapping
qt.spinBoxSetWrapping(spin, true)
```

### QDoubleSpinBox

```havel
// Create double spin box
let dspin = qt.doubleSpinBoxNew()

// Range
qt.doubleSpinBoxSetRange(dspin, 0.0, 1.0)

// Value
qt.doubleSpinBoxSetValue(dspin, 0.5)
let value = qt.doubleSpinBoxValue(dspin)

// Decimals
qt.doubleSpinBoxSetDecimals(dspin, 3)
```

### QSlider

```havel
// Create slider (0 = horizontal, 1 = vertical)
let slider = qt.sliderNew(1)  // Vertical

// Orientation
qt.sliderSetOrientation(slider, 1)

// Range
qt.sliderSetRange(slider, 0, 100)
qt.sliderSetMinimum(slider, 0)
qt.sliderSetMaximum(slider, 100)

// Value
qt.sliderSetValue(slider, 50)
let value = qt.sliderValue(slider)

// Step
qt.sliderSetSingleStep(slider, 5)

// Tick marks
qt.sliderSetTickPosition(slider, 2)  // TicksRight

// Inverted
qt.sliderSetInvertedAppearance(slider, true)
```

### QScrollBar

```havel
// Create scroll bar (0 = horizontal, 1 = vertical)
let scrollbar = qt.scrollBarNew(1)

// Range and value
qt.scrollBarSetRange(scrollbar, 0, 100)
qt.scrollBarSetValue(scrollbar, 50)
let value = qt.scrollBarValue(scrollbar)
```

### QDial

```havel
// Create dial
let dial = qt.dialNew()

// Range and value
qt.dialSetRange(dial, 0, 100)
qt.dialSetValue(dial, 50)
let value = qt.dialValue(dial)

// Wrapping
qt.dialSetWrapping(dial, true)

// Notches
qt.dialSetNotchesVisible(dial, true)
```

### QProgressBar

```havel
// Create progress bar
let progress = qt.progressBarNew()

// Range
qt.progressBarSetRange(progress, 0, 100)

// Value
qt.progressBarSetValue(progress, 50)
let value = qt.progressBarValue(progress)

// Format
qt.progressBarSetFormat(progress, "%p%")

// Orientation (0 = horizontal, 1 = vertical)
qt.progressBarSetOrientation(progress, 0)

// Text visibility
qt.progressBarSetTextVisible(progress, true)

// Reset
qt.progressBarReset(progress)
```

### QFrame

```havel
// Create frame
let frame = qt.frameNew()

// Shape (0=NoFrame, 1=Box, 2=Panel, 6=StyledPanel)
qt.frameSetFrameShape(frame, 2)

// Shadow (0=Plain, 1=Raised, 2=Sunken)
qt.frameSetFrameShadow(frame, 2)

// Line width
qt.frameSetLineWidth(frame, 2)

// Combined style
qt.frameSetFrameStyle(frame, 0x0031)  // Box | Sunken
```

### Layouts

```havel
// QVBoxLayout
let vBox = qt.vBoxLayoutNew()
qt.layoutAddWidget(vBox, widget)
qt.layoutAddStretch(vBox, 1)  // stretch factor
qt.layoutAddSpacing(vBox, 10)

// QHBoxLayout
let hBox = qt.hBoxLayoutNew()
qt.layoutAddWidget(hBox, widget)
qt.layoutAddStretch(hBox, 1)

// QGridLayout
let grid = qt.gridLayoutNew()
qt.gridLayoutAddWidget(grid, widget, row, col, rowSpan, colSpan)
qt.gridLayoutSetRowSpacing(grid, row, 5)
qt.gridLayoutSetColumnSpacing(grid, col, 10)
qt.gridLayoutSetRowStretch(grid, row, 1)
qt.gridLayoutSetColumnStretch(grid, col, 1)

// Set layout on widget
qt.widgetSetLayout(container, layout)
```

### QTabWidget

```havel
// Create tab widget
let tabs = qt.tabWidgetNew()

// Add tabs
let idx1 = qt.tabWidgetAddTab(tabs, widget1, "Tab 1")
let idx2 = qt.tabWidgetInsertTab(tabs, 0, widget2, "Tab 2")

// Remove tab
qt.tabWidgetRemoveTab(tabs, idx)

// Current tab
qt.tabWidgetSetCurrentIndex(tabs, 0)
let idx = qt.tabWidgetCurrentIndex(tabs)
let widget = qt.tabWidgetCurrentWidget(tabs)
qt.tabWidgetSetCurrentWidget(tabs, widget)

// Count
let count = qt.tabWidgetCount(tabs)

// Tab text
qt.tabWidgetSetTabText(tabs, idx, "New Title")
let text = qt.tabWidgetTabText(tabs, idx)

// Tab enabled
qt.tabWidgetSetTabEnabled(tabs, idx, false)
let enabled = qt.tabWidgetIsTabEnabled(tabs, idx)

// Properties
qt.tabWidgetSetTabsClosable(tabs, true)
qt.tabWidgetSetMovable(tabs, true)
qt.tabWidgetSetTabPosition(tabs, 1)  // TabAtTop

// Clear all
qt.tabWidgetClear(tabs)
```

### QStackedWidget

```havel
// Create stacked widget
let stacked = qt.stackedWidgetNew()

// Add widgets
let idx = qt.stackedWidgetAddWidget(stacked, widget)
qt.stackedWidgetInsertWidget(stacked, 0, widget)

// Current widget
qt.stackedWidgetSetCurrentIndex(stacked, 0)
let idx = qt.stackedWidgetCurrentIndex(stacked)
let widget = qt.stackedWidgetCurrentWidget(stacked)
qt.stackedWidgetSetCurrentWidget(stacked, widget)

// Count and access
let count = qt.stackedWidgetCount(stacked)
let w = qt.stackedWidgetWidget(stacked, idx)
let idx = qt.stackedWidgetIndexOf(stacked, widget)

// Remove
qt.stackedWidgetRemoveWidget(stacked, widget)
```

### QScrollArea

```havel
// Create scroll area
let scrollArea = qt.scrollAreaNew()

// Set widget
qt.scrollAreaSetWidget(scrollArea, widget)
let widget = qt.scrollAreaWidget(scrollArea)

// Resizable
qt.scrollAreaSetWidgetResizable(scrollArea, true)

// Scroll bar policies (0=AsNeeded, 1=AlwaysOff, 2=AlwaysOn)
qt.scrollAreaSetHScrollBarPolicy(scrollArea, 0)
qt.scrollAreaSetVScrollBarPolicy(scrollArea, 0)
```

### QSplitter

```havel
// Create splitter (0 = horizontal, 1 = vertical)
let splitter = qt.splitterNew(0)

// Add widgets
qt.splitterAddWidget(splitter, widget1)
qt.splitterAddWidget(splitter, widget2)

// Orientation
qt.splitterSetOrientation(splitter, 0)

// Handle width
qt.splitterSetHandleWidth(splitter, 5)
```

### Menu Bar

```havel
// Create menu bar
let menuBar = qt.menuBarNew()

// Add menu
let menu = qt.menuBarAddMenu(menuBar, "File")

// Add action
let action = qt.menuBarAddAction(menuBar, "Exit")
```

### QMenu

```havel
// Create menu
let menu = qt.menuNew("File")

// Add action
let action = qt.menuAddAction(menu, "Save")

// Add separator
qt.menuAddSeparator(menu)

// Clear
qt.menuClear(menu)
```

### QStatusBar

```havel
// Create status bar
let statusBar = qt.statusBarNew()

// Show message
qt.statusBarShowMessage(statusBar, "Ready", 0)  // 0 = permanent

// Clear message
qt.statusBarClearMessage(statusBar)

// Add widget
qt.statusBarAddWidget(statusBar, widget, 0)
qt.statusBarAddPermanentWidget(statusBar, widget, 0)
```

### QMessageBox (Static Dialogs)

```havel
// About dialog
qt.messageBoxAbout(parent, "My App", "Version 1.0\n\nCopyright 2024")

// Information dialog
let result = qt.messageBoxInformation(parent, "Info", "Operation completed", 0x00000400, 0x00000400)

// Question dialog
let result = qt.messageBoxQuestion(parent, "Confirm", "Are you sure?", 0x00002000 | 0x00004000, 0x00004000)
// Buttons: Qt_Yes = 0x00002000, Qt_No = 0x00004000, Qt_Ok = 0x00000400, Qt_Cancel = 0x00400000

// Warning dialog
let result = qt.messageBoxWarning(parent, "Warning", "Low disk space", 0x00000400, 0x00000400)
```

### Complete Qt Example

```havel
import qt from "extension:qt"

// Initialize Qt
if (!qt.init()) {
    print("Failed to load Qt")
    return
}

qt.setApplicationName("Havel Qt Demo")
qt.setOrganizationName("Havel")

// Create main window
let win = qt.mainWindowNew()
qt.widgetSetWindowTitle(win, "Havel + Qt")
qt.widgetResize(win, 500, 400)

// Create layout
let layout = qt.vBoxLayoutNew()

// Label
let label = qt.labelNew("Welcome to Havel + Qt!")
qt.labelSetAlignment(label, 0x0084)  // Center
qt.layoutAddWidget(layout, label)

// Line edit
let edit = qt.lineEditNew("")
qt.lineEditSetPlaceholderText(edit, "Enter your name...")
qt.layoutAddWidget(layout, edit)

// Button
let btn = qt.pushButtonNew("Greet")
qt.layoutAddWidget(layout, btn)

// Add stretch
qt.layoutAddStretch(layout)

// Create container and set layout
let container = qt.mainWindowNew()
qt.widgetSetLayout(container, layout)
qt.mainWindowSetCentralWidget(win, container)

// Show window
qt.widgetShow(win)

// Run event loop
qt.exec()
```

## Qt Constants

```havel
// Alignment
Qt_AlignLeft      = 0x0001
Qt_AlignRight     = 0x0002
Qt_AlignHCenter   = 0x0004
Qt_AlignTop       = 0x0020
Qt_AlignBottom    = 0x0040
Qt_AlignVCenter   = 0x0080
Qt_AlignCenter    = 0x0084

// Orientation
Qt_Horizontal     = 0
Qt_Vertical       = 1

// Frame shapes
Qt_NoFrame        = 0
Qt_Box            = 1
Qt_Panel          = 2
Qt_StyledPanel    = 6

// Focus policy
Qt_NoFocus        = 0
Qt_TabFocus       = 0x1
Qt_ClickFocus     = 0x2
Qt_StrongFocus    = 0x3

// Cursor shapes
Qt_ArrowCursor    = 0
Qt_IBeamCursor    = 4
Qt_WaitCursor     = 3
Qt_CrossCursor    = 2

// Window flags
Qt_Window         = 0x00000001
Qt_Dialog         = 0x00000002
Qt_Popup          = 0x00000010
Qt_Tool           = 0x00000020
Qt_FramelessWindowHint = 0x08000000
Qt_WindowStaysOnTopHint = 0x00000800

// Modality
Qt_NonModal       = 0
Qt_WindowModal    = 1
Qt_ApplicationModal = 2

// Message box buttons
Qt_Ok             = 0x00000400
Qt_Cancel         = 0x00400000
Qt_Yes            = 0x00002000
Qt_No             = 0x00004000

// Message box icons
Qt_NoIcon         = 0
Qt_Info           = 1
Qt_Warning        = 2
Qt_Critical       = 3
Qt_Question       = 4

// Scroll bar policy
Qt_ScrollBarAsNeeded  = 0
Qt_ScrollBarAlwaysOff = 1
Qt_ScrollBarAlwaysOn  = 2

// Tab position
Qt_NoTabBar     = 0
Qt_TabAtTop     = 1
Qt_TabAtBottom  = 2
Qt_TabAtLeft    = 3
Qt_TabAtRight   = 4

// Echo mode
Qt_NormalEcho   = 0
Qt_NoEcho       = 1
Qt_PasswordEcho = 2
```

## Troubleshooting

### Extension Not Found

```bash
ls build-release/src/extensions/qt/qt_extension.so
sudo cp build-release/src/extensions/qt/qt_extension.so /usr/lib/havel/extensions/
```

### Qt Not Loading at Runtime

```bash
# Check Qt6
ldconfig -p | grep Qt6Widgets

# Check Qt5
ldconfig -p | grep Qt5Widgets

# Install if missing
sudo apt install libqt6widgets6
# or
sudo apt install libqt5widgets5
```

### Debug Loading

The extension prints diagnostic messages:

```
[Qt] Qt libraries loaded dynamically
```

If loading fails:

```
[Qt] Failed to load Qt Widgets
```

## Comparison: Qt vs GTK

| Feature | Qt | GTK |
|---------|-----|-----|
| **Paradigm** | Retained mode | Retained mode |
| **Loading** | Dynamic (dlopen) | Dynamic (dlopen) |
| **Best for** | Cross-platform apps | Linux-native apps |
| **Runtime deps** | Qt6/Qt5 libraries | GTK4 libraries |
| **Styling** | QSS (Qt Style Sheets) | CSS |
| **Designer** | Qt Designer | Glade |

## License

MIT License - same as Havel project.
