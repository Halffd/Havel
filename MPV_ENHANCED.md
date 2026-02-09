# Enhanced MPV Module Documentation

## Overview ✅

The MPV module has been significantly enhanced with process discovery, multiple instance support, and robust socket management.

## New Features

### 🔍 **Process Discovery**

#### `mpv.findInstances()`
Finds all running MPV instances and their socket paths.

**Returns:** Array of MPV instance objects with properties:
- `pid` - Process ID
- `socketPath` - Unix socket path
- `command` - Full command line
- `isActive` - Whether this is the active instance

**Example:**
```havel
let instances = mpv.findInstances();
for (let instance in instances) {
    print("PID:", instance.pid, "Socket:", instance.socketPath);
}
```

#### `mpv.isRunning()`
Check if any MPV instances are running.

**Returns:** Boolean

**Example:**
```havel
if (mpv.isRunning()) {
    print("MPV is running");
}
```

### 🔧 **Socket Management**

#### `mpv.changeSocket(socketPath)`
Change the MPV socket path dynamically.

**Parameters:**
- `socketPath` - New socket path to use

**Example:**
```havel
mpv.changeSocket("/tmp/mpvsocket-12345");
```

#### `mpv.setActiveInstance(pid)`
Set active MPV instance by process ID.

**Parameters:**
- `pid` - Process ID of MPV instance

**Returns:** Boolean success

**Example:**
```havel
let success = mpv.setActiveInstance("12345");
```

#### `mpv.setActiveInstanceBySocket(socketPath)`
Set active MPV instance by socket path.

**Parameters:**
- `socketPath` - Socket path of MPV instance

**Returns:** Boolean success

**Example:**
```havel
let success = mpv.setActiveInstanceBySocket("/tmp/mpvsocket-12345");
```

#### `mpv.getActiveInstance()`
Get the currently active MPV instance.

**Returns:** MPV instance object or null

**Example:**
```havel
let active = mpv.getActiveInstance();
if (active != null) {
    print("Active PID:", active.pid);
}
```

### 🎮 **Multiple Instance Control**

#### `mpv.controlMultiple(pids, commands)`
Control multiple MPV instances simultaneously.

**Parameters:**
- `pids` - Array of process IDs
- `commands` - Array of commands to send

**Example:**
```havel
let pids = ["12345", "12346"];
let commands = ["cycle pause", "stop"];
mpv.controlMultiple(pids, commands);
```

### 🎵 **Enhanced Media Controls**

#### `mpv.playPause()`
Enhanced play/pause with fallback to playerctl.

**Features:**
- Primary: MPV socket control
- Fallback: playerctl command
- Automatic socket detection

## Process Discovery Methods

The enhanced MPV module uses multiple methods to find MPV instances:

### 1. Socket Scanning
- Scans `/tmp` for MPV socket files
- Identifies socket patterns like `mpvsocket-*`, `mpvsocket.*`, `mpvsocket_*`

### 2. Process Enumeration
- Uses `pgrep -f mpv` to find MPV processes
- Cross-references with socket discoveries

### 3. /proc Analysis
- Reads `/proc/[pid]/cmdline` for process identification
- Maps PIDs to socket paths
- Most reliable method for running instances

## Socket Path Detection

The module searches for MPV sockets in these locations:
1. `/tmp/mpvsocket-[pid]`
2. `/tmp/mpvsocket.[pid]`
3. `/tmp/mpvsocket_[pid]`
4. `/run/user/[uid]/mpv/socket`
5. `/tmp/mpvsocket` (default)

## Error Handling

### Robust Fallbacks
- **Socket not found:** Falls back to playerctl commands
- **Connection failed:** Automatic reconnection attempts
- **Instance not found:** Returns null/false gracefully

### Thread Safety
- All socket operations are thread-safe
- Multiple instance switching is atomic
- Process discovery is non-blocking

## Usage Examples

### Basic Usage
```havel
// Check if MPV is running
if (mpv.isRunning()) {
    // Find all instances
    let instances = mpv.findInstances();
    
    if (instances.length > 0) {
        // Use first instance
        mpv.setActiveInstance(instances[0].pid);
        mpv.playPause();
    }
}
```

### Advanced Multi-Instance Control
```havel
// Control multiple MPV windows
let instances = mpv.findInstances();
if (instances.length >= 2) {
    let pids = [];
    for (let i = 0; i < 2; i++) {
        pids.push(instances[i].pid);
    }
    
    // Pause first two instances
    mpv.controlMultiple(pids, ["cycle pause"]);
    
    // Stop all instances
    setTimeout(() => {
        mpv.controlMultiple(pids, ["stop"]);
    }, 2000);
}
```

### Dynamic Socket Switching
```havel
// Switch between different MPV instances
let instances = mpv.findInstances();
for (let instance in instances) {
    print("Switching to PID:", instance.pid);
    mpv.setActiveInstance(instance.pid);
    mpv.playPause();
    sleep(1000); // Wait 1 second
}
```

## Integration with createKeyTap

The enhanced MPV module works perfectly with createKeyTap:

```havel
// Create media control hotkeys that work with any MPV instance
createKeyTap("space", () => {
    mpv.playPause(); // Will find active instance or use fallback
});

createKeyTap("q", () => {
    mpv.stop(); // Enhanced with fallback
});

// Advanced: Control specific instances
createKeyTap("1", () => {
    let instances = mpv.findInstances();
    if (instances.length > 0) {
        mpv.setActiveInstance(instances[0].pid);
        mpv.playPause();
    }
}, "mode == \"gaming\"");
```

## Benefits

### ✅ **Reliability**
- Multiple discovery methods ensure instance detection
- Automatic fallback to playerctl when MPV unavailable
- Robust error handling and recovery

### ✅ **Flexibility**
- Support for multiple MPV instances
- Dynamic socket switching
- Programmatic instance control

### ✅ **Performance**
- Efficient process enumeration
- Cached instance information
- Minimal system overhead

The enhanced MPV module provides comprehensive control over MPV instances with robust fallback mechanisms and multi-instance support!
