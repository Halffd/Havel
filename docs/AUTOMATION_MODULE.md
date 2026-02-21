# Automation Module Documentation

## Overview ✅

The Havel Automation module provides comprehensive automation capabilities including auto-clickers, key pressers, auto-runners, and complex chained tasks. All automation classes are now integrated as built-in functions accessible via the `automation` module.

## Available Classes

### 🖱️ AutoClicker
Automates mouse clicking with configurable buttons and intervals.

**Functions:**
```havel
// Start an auto-clicker
automation.startAutoClicker(button, intervalMs)

// Stop an auto-clicker  
automation.stopAutoClicker(taskName)

// Quick toggle (start/stop)
automation.autoClick(button, intervalMs)
```

**Parameters:**
- `button` - "left", "right", "middle" (default: "left")
- `intervalMs` - Click interval in milliseconds (default: 100)

**Example:**
```havel
// Start left-clicking every 200ms
let task = automation.startAutoClicker("left", 200);

// Stop after 5 seconds
setTimeout(() => {
    automation.stopAutoClicker(task);
}, 5000);
```

### ⌨️ AutoKeyPresser
Automates keyboard key pressing with configurable keys and intervals.

**Functions:**
```havel
// Start an auto-key-presser
automation.startAutoKeyPresser(key, intervalMs)

// Stop an auto-key-presser
automation.stopAutoKeyPresser(taskName)

// Quick toggle (start/stop)
automation.autoPress(key, intervalMs)
```

**Parameters:**
- `key` - Key name (e.g., "space", "w", "shift", "enter")
- `intervalMs` - Press interval in milliseconds (default: 100)

**Example:**
```havel
// Auto-press space every 500ms
let task = automation.startAutoKeyPresser("space", 500);

// Toggle to stop
automation.toggleTask(task);
```

### 🏃 AutoRunner
Automates directional movement (typically for games).

**Functions:**
```havel
// Start an auto-runner
automation.startAutoRunner(direction, intervalMs)

// Stop an auto-runner
automation.stopAutoRunner(taskName)

// Quick toggle (start/stop)
automation.autoRun(direction, intervalMs)
```

**Parameters:**
- `direction` - Movement direction (e.g., "w", "a", "s", "d")
- `intervalMs` - Movement interval in milliseconds (default: 50)

**Example:**
```havel
// Auto-run forward every 50ms
let task = automation.autoRun("w", 50);

// Change direction
automation.stopAutoRunner(task);
let newTask = automation.autoRun("s", 50);
```

### 🔗 ChainedTask
Creates complex automation sequences with multiple actions and timing.

**Functions:**
```havel
// Create a chained task
automation.createChainedTask(name, actions, loop)

// Start/stop chained tasks
automation.startChainedTask(taskName)
automation.stopChainedTask(taskName)
```

**Parameters:**
- `name` - Unique task name
- `actions` - Array of [action, delay] pairs
- `loop` - Whether to loop the sequence (default: false)

**Action Types:**
- `"click"` - Left mouse click
- `"rightClick"` - Right mouse click
- `"key:keyname"` - Press and release a key
- `"wait:ms"` - Wait for milliseconds

**Example:**
```havel
// Create a complex automation sequence
let actions = [
    ["key:w", 100],      // Press W for 100ms
    ["click", 0],        // Click immediately
    ["wait:500", 0],     // Wait 500ms
    ["rightClick", 0],   // Right click
    ["key:space", 0]     // Press space
];

let task = automation.createChainedTask("mySequence", actions, true);
automation.startChainedTask(task);
```

## Task Management

### 📋 Task Information
```havel
// Get task information
let info = automation.getTask(taskName);
print("Name:", info.name);
print("Running:", info.running);

// Check if task exists
let exists = automation.hasTask(taskName);
```

### 🔄 Task Control
```havel
// Toggle task (start/stop)
automation.toggleTask(taskName);

// Remove task completely
automation.removeTask(taskName);

// Stop all running tasks
automation.stopAllTasks();
```

## Advanced Usage

### 🎮 Gaming Automation
```havel
// Auto-sprint in games
setMode("gaming");

// Create sprint automation
let sprintTask = automation.autoPress("shift", 1000);

// Movement automation
let moveTask = automation.autoRun("w", 50);

// Combat sequence
let combatActions = [
    ["key:1", 0],        // Use skill 1
    ["wait:2000", 0],    // Wait 2 seconds
    ["key:2", 0],        // Use skill 2
    ["wait:1500", 0],    // Wait 1.5 seconds
    ["click", 0]         // Attack
];

let combatTask = automation.createChainedTask("combat", combatActions, false);
automation.startChainedTask(combatTask);
```

### 🖱️ Mouse Automation
```havel
// Rapid clicking for crafting
let craftTask = automation.autoClick("left", 50);

// Double-click automation
let doubleClickActions = [
    ["click", 0],
    ["wait:50", 0],
    ["click", 0]
];

let doubleClickTask = automation.createChainedTask("doubleClick", doubleClickActions, true);
automation.startChainedTask(doubleClickTask);
```

### ⌨️ Keyboard Automation
```havel
// Auto-typing
let typeActions = [
    ["key:h", 100],
    ["key:e", 100],
    ["key:l", 100],
    ["key:l", 100],
    ["key:o", 100]
];

let typeTask = automation.createChainedTask("typeHello", typeActions, false);
automation.startChainedTask(typeTask);

// Key combinations
let comboActions = [
    ["key:ctrl", 0],
    ["key:c", 0],
    ["wait:100", 0],
    ["key:ctrl", 0],
    ["key:v", 0]
];

let copyPasteTask = automation.createChainedTask("copyPaste", comboActions, false);
automation.startChainedTask(copyPasteTask);
```

## Integration with Other Modules

### 🎯 Mode-Based Automation
```havel
// Different automation for different modes
createKeyTap("F1", () => {
    setMode("afk");
    let afkTask = automation.autoPress("w", 1000);
}, "mode != \"afk\"");

createKeyTap("F2", () => {
    setMode("combat");
    automation.stopAllTasks();
    let combatTask = automation.autoClick("left", 200);
}, "mode != \"combat\"");
```

### ⏰ Timer Integration
```havel
// Timed automation
setTimeout(() => {
    let burstTask = automation.autoClick("left", 10);
    setTimeout(() => {
        automation.toggleTask(burstTask); // Stop burst
    }, 1000);
}, 5000);

// Interval automation
setInterval(() => {
    let quickTask = automation.autoPress("space", 50);
    setTimeout(() => {
        automation.removeTask(quickTask);
    }, 200);
}, 5000);
```

### 🎪 Event Monitor Integration
```havel
// Automation based on events
onKeyDown((keyCode, keyName) => {
    if (keyName == "F3") {
        // Start automation sequence
        let actions = [
            ["key:w", 2000],
            ["key:a", 1000],
            ["key:s", 1000],
            ["key:d", 1000]
        ];
        let task = automation.createChainedTask("square", actions, false);
        automation.startChainedTask(task);
    }
});
```

## Best Practices

### ✅ Do's
- Use descriptive task names for easy management
- Clean up tasks when done with `removeTask()`
- Use `stopAllTasks()` in mode switches
- Test automation sequences with longer delays first
- Use `toggleTask()` for start/stop functionality

### ❌ Don'ts
- Don't create too many tasks simultaneously
- Don't use very short intervals (< 10ms) unless necessary
- Don't forget to stop tasks that might interfere with other activities
- Don't rely on exact timing - use small buffer delays

## Error Handling

The automation module includes robust error handling:

```havel
// Safe task creation
let task = automation.startAutoClicker("left", 100);
if (automation.hasTask(task)) {
    print("Task started successfully");
} else {
    print("Failed to start task");
}

// Safe task operations
try {
    automation.startChainedTask("nonexistent");
} catch (e) {
    print("Error:", e);
}
```

## Performance Considerations

- **Task Overhead:** Each task uses minimal resources
- **Thread Safety:** All operations are thread-safe
- **Cleanup:** Tasks are automatically cleaned up when removed
- **Memory:** Task memory is freed when tasks are stopped

## Troubleshooting

### Common Issues

**Task not starting:**
- Check if AutomationManager is available
- Verify task name doesn't conflict with existing tasks
- Ensure parameters are valid

**Task not stopping:**
- Use `stopAllTasks()` as emergency stop
- Check if task name is correct
- Try `removeTask()` for force cleanup

**ChainedTask not working:**
- Verify action syntax is correct
- Check array format: [[action, delay], ...]
- Ensure delays are reasonable (not 0 for key presses)

The automation module provides powerful, flexible automation capabilities for any use case! 🚀
