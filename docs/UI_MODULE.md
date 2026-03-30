# Havel UI Module Documentation

## Overview

The Havel UI Module provides a scripting-friendly interface for creating native desktop applications using Qt widgets. It follows a minimal, disposable UI philosophy with no complex lifecycle management.

## Design Principles

1. **No Lifecycle**: Create, use, and dispose UI immediately
2. **Events = Plain Callbacks**: No signals, no observers - just functions
3. **Simple Layouts**: Row/col/grid only - no CSS complexity
4. **Disposable UI**: `show() → sleep() → close()` feels normal
5. **Instant Creation**: No setup rituals - just create and go

## Two API Styles

### 1. Imperative API

Build UI programmatically with method chaining:

```havel
win = ui.window("My App", {width: 800, height: 600})
btn = ui.btn("Click me!").onClick(() => print("Hello!"))
win.add(btn).show()
```

### 2. Declarative Syntax

Define UI structure in a block:

```havel
ui {
  window "My App" {
    column {
      text "Hello, World!"
      button "Click Me" {
        onClick => print("Clicked!")
      }
    }
  }
}
```

## Window Elements

### window(title, options)

Creates a main application window.

```havel
win = ui.window("My Application", {
  width: 800,
  height: 600,
  resizable: true
})
```

**Methods:**
- `add(child)` - Add child element
- `show()` - Display window
- `hide()` - Hide window
- `close()` - Close and dispose
- `addMenu(menu)` - Add menu bar

## Display Elements

### text(content)

Static text label.

```havel
ui.text("Hello, World!")
```

### label(content)

Selectable text label.

```havel
ui.label("Status: Ready")
```

### image(path)

Display an image.

```havel
ui.image("/path/to/image.png")
```

### divider()

Horizontal line separator.

```havel
ui.divider()
```

### spacer(size)

Flexible spacing element.

```havel
ui.spacer(20)  // 20 pixels
```

## Input Elements

### btn(label)

Clickable button.

```havel
btn = ui.btn("Submit")
btn.onClick(() => print("Clicked!"))
```

**Methods:**
- `onClick(callback)` - Set click handler
- `text(newText)` - Change label

### input(placeholder)

Single-line text input.

```havel
input = ui.input("Enter your name...")
name = input.value()      // Getter
input.value("John")       // Setter
```

### textarea(placeholder)

Multi-line text input.

```havel
textarea = ui.textarea("Enter description...")
text = textarea.value()
```

### checkbox(label, checked)

Checkbox with label.

```havel
cb = ui.checkbox("Enable feature", true)
cb.onChange((checked) => print(checked))
```

### slider(min, max, value)

Numeric slider control.

```havel
slider = ui.slider(0, 100, 50)
slider.onChange((value) => print("Volume: " + value))
```

### dropdown(options)

Selection dropdown.

```havel
dd = ui.dropdown(["Option 1", "Option 2", "Option 3"])
dd.onChange((selected) => print(selected))
```

### progress(value, max)

Progress bar.

```havel
ui.progress(65, 100)  // 65% complete
```

### spinner()

Loading indicator.

```havel
ui.spinner()
```

## Layout Containers

### row()

Horizontal layout container.

```havel
row = ui.row()
row.add(ui.btn("Left"))
row.add(ui.spacer())  // Push right
row.add(ui.btn("Right"))
```

### col()

Vertical layout container.

```havel
col = ui.col()
col.add(ui.text("Item 1"))
col.add(ui.text("Item 2"))
```

### grid(cols)

Grid layout with specified columns.

```havel
grid = ui.grid(3)  // 3 columns
grid.add(ui.btn("1"))
grid.add(ui.btn("2"))
grid.add(ui.btn("3"))
grid.add(ui.btn("4"))  // Wraps to next row
```

### scroll()

Scrollable container.

```havel
scroll = ui.scroll()
content = ui.col()
for i in range(1, 100) {
  content.add(ui.label("Item " + i))
}
scroll.add(content)
```

## Menu System

### menu(title)

Create a menu.

```havel
fileMenu = ui.menu("File")
fileMenu.add(ui.menuItem("New", "Ctrl+N"))
fileMenu.add(ui.menuItem("Open", "Ctrl+O"))
fileMenu.add(ui.menuSeparator())
fileMenu.add(ui.menuItem("Exit"))
```

### menuItem(label, shortcut)

Menu item with optional keyboard shortcut.

```havel
item = ui.menuItem("Save", "Ctrl+S")
item.onClick(() => print("Saving..."))
```

### menuSeparator()

Menu divider line.

```havel
ui.menuSeparator()
```

## Dialogs

### alert(message)

Show information dialog.

```havel
ui.alert("Operation completed successfully!")
```

### confirm(message)

Show yes/no dialog, returns boolean.

```havel
if ui.confirm("Delete this file?") {
  print("Deleting...")
}
```

### filePicker(title)

File selection dialog, returns path string.

```havel
path = ui.filePicker("Select a file")
if path != "" {
  print("Selected: " + path)
}
```

### dirPicker(title)

Directory selection dialog.

```havel
dir = ui.dirPicker("Choose directory")
```

### notify(message, type)

Show notification toast.

```havel
ui.notify("Task complete!", "success")  // Types: info, success, warning, error
```

## Styling Methods

All elements support these styling methods:

```havel
element
  .pad(10)                    // Padding in pixels
  .bg("#ffffff")             // Background color
  .fg("#000000")             // Foreground/text color
  .font("Arial", 14)          // Font family and size
  .width(200)                 // Fixed width
  .height(50)                 // Fixed height
  .align("center")            // Text alignment
  .alignRight()               // Shortcut for align("right")
  .bold(true)                 // Bold text
```

## Complete Example

```havel
// Create window
win = ui.window("My Application", {width: 600, height: 400})

// Add menu
fileMenu = ui.menu("File")
fileMenu.add(ui.menuItem("Exit").onClick(() => win.close()))
win.addMenu(fileMenu)

// Create form
form = ui.col()

form.add(ui.text("User Registration").bold(true).font("", 20))
form.add(ui.divider())

form.add(ui.label("Username:"))
username = ui.input("Enter username...")
form.add(username)

form.add(ui.label("Email:"))
email = ui.input("Enter email...")
form.add(email)

form.add(ui.checkbox("Subscribe to newsletter"))

btnRow = ui.row()
submitBtn = ui.btn("Submit").bg("#28a745").fg("white")
submitBtn.onClick(() => {
  user = username.value()
  ui.notify("Welcome, " + user + "!", "success")
})

resetBtn = ui.btn("Reset")
resetBtn.onClick(() => {
  username.value("")
  email.value("")
})

btnRow.add(submitBtn)
btnRow.add(resetBtn)
form.add(btnRow)

// Add form to window and show
win.add(form)
win.show()

// Keep window open
while win.isVisible() {
  ui.pumpEvents(100)
}
```

## Declarative Syntax Reference

The `ui { }` block desugars to imperative API calls:

| Declarative | Imperative |
|-------------|------------|
| `window "Title" { }` | `ui.window("Title")` |
| `button "Label"` | `ui.btn("Label")` |
| `onClick => { }` | `.onClick(() => { })` |
| `pad(10)` | `.pad(10)` |
| `bg("#fff")` | `.bg("#fff")` |

## Best Practices

1. **Use declarative syntax** for static layouts
2. **Use imperative API** for dynamic UIs
3. **Always handle window close** in your event loop
4. **Chain styling calls** for readability
5. **Use spacers** for flexible layouts
6. **Dispose of windows** when done to free resources

## Examples

See the `examples/` directory:
- `ui_example.hv` - Basic usage
- `ui_declarative.hv` - Declarative syntax demo
- `ui_complete_demo.hv` - All features showcase
