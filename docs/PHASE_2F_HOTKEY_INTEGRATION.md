// Phase 2F Integration Guide
// Hotkey Condition & Mode Integration with Reactive Watchers
// 
// This file documents how to integrate ConditionalHotkeyManager and ModeManager
// with the reactive watcher system (WatcherRegistry) once ExecutionEngine is available.

/*
=============================================================================
PHASE 2F: REACTIVE HOTKEY CONDITION INTEGRATION
=============================================================================

Components Modified:
1. ConditionalHotkey struct - Added watcher_id field
2. ConditionalHotkeyManager - Added setExecutionEngine() method
3. ModeDefinition struct - Added watcher_id field
4. ModeManager - Added setExecutionEngine() method

Current Status: Phase 2 Reactive Watchers Infrastructure (✅ Complete)
Pending: Phase 3 ExecutionEngine Integration

=============================================================================
HOW IT WILL WORK
=============================================================================

1. HOTKEY CONDITION REGISTRATION
   
   When a ConditionalHotkey is added with a string condition:
   
   a) Evaluate condition once to get initial result
   b) Extract variables used in condition (dependency tracking)
   c) Create watcher in WatcherRegistry:
      - condition_func_id: Bytecode for condition evaluation
      - condition_ip: Instruction pointer
      - dependencies: Set of variable names used
      - fiber: Hotkey action fiber (or nullptr for polling fallback)
      - initial_result: First evaluation result
   d) Store returned watcher_id in ConditionalHotkey.watcher_id

   Then when any variable changes:
   
   e) VAR_CHANGED event pushes to EventQueue
   f) onVariableChanged() handler evaluates condition via watcher
   g) False→true edge: Grab hotkey, execute trueAction
   h) True→false edge: Ungrab hotkey, execute falseAction

2. MODE CONDITION REGISTRATION
   
   Similar to hotkeys:
   
   a) Register mode condition as watcher
   b) On false→true edge: Call onEnter, triggerEnter callbacks
   c) On true→false edge: Call onExit, triggerExit callbacks
   d) Store watcher_id in ModeDefinition.watcher_id

3. DEPENDENCY TRACKING
   
   During condition evaluation:
   - DependencyTrackerScope captures all global variable reads
   - Only re-evaluate when watched variables change
   - No polling needed (pure event-driven)

4. WHEN BLOCKS
   
   When blocks become first-class reactive constructs:
   
   fn main() {
     let enabled = true;
     let counter = 0;
     
     // Creates watcher that fires on enabled going false→true
     when enabled {
       println("System enabled!");
       counter = counter + 1;
     }
     
     // Later, changing enabled triggers watcher immediately
     enabled = false;
     enabled = true;  // ← Watcher fires, statements execute synchronously
   }

=============================================================================
INTEGRATION POINTS
=============================================================================

In HotkeyManager:
   - After creating ConditionalHotkeyManager
   - After creating ExecutionEngine (Phase 3)
   - Call: conditionalManager.setExecutionEngine(executionEngine)
   - Call: modeManager->setExecutionEngine(executionEngine)

In ConditionalHotkeyManager::AddConditionalHotkey():
   Phase 2F:
   - Check if executionEngine_ is available
   - If yes: Register condition as watcher
   - If no: Fall back to polling via UpdateAllConditionalHotkeys()

In ModeManager::defineMode():
   Phase 2F:
   - Check if executionEngine_ is available
   - If yes: Register mode condition as watcher
   - If no: Fall back to polling via update()

=============================================================================
BACKWARD COMPATIBILITY
=============================================================================

Systems continue working even without ExecutionEngine:
- ConditionalHotkeyManager.UpdateAllConditionalHotkeys() still works
- ModeManager.update() still works
- Just uses polling instead of event-driven watchers
- Performance degrades but functionality unchanged

When ExecutionEngine becomes available:
- New hotkeys automatically use reactive system
- Old hotkeys continue with polling
- No breaking changes

=============================================================================
BENEFITS OF PHASE 2F INTEGRATION
=============================================================================

1. REACTIVE: Hotkey conditions fire immediately when variables change
2. EFFICIENT: Only conditions with changed variables are re-evaluated
3. COMPOSITIONAL: Conditions can be complex, dependencies extracted automatically
4. PREDICTABLE: Edge-triggered (false→true) prevents loops and wasted updates
5. MODES: Mode transitions become fully event-driven and composable

Example:
   mode gaming priority 10 {
       condition = window.any(exe == "steam.exe")  // Depends on: (window)
       enter { brightness(50); volume(80) }
   }
   
   // When any window changes: VAR_CHANGED("window")
   // Gaming mode condition re-evaluated automatically
   // If becomes true: enter handler runs immediately
   // No polling needed!

=============================================================================
TESTING STRATEGY
=============================================================================

Once ExecutionEngine is integrated:

1. Unit tests:
   - ConditionalHotkey.watcher_id stored correctly
   - ModeDefinition.watcher_id stored correctly
   - WatcherRegistry.registerWatcher() called on hotkey add

2. Integration tests:
   - Variable change → VAR_CHANGED event
   - Condition re-evaluated via watcher
   - False→true edge triggers hotkey action
   - Mode transitions work with watcher firing

3. Havel script tests:
   - When blocks work as expected
   - Hotkey conditions react to variables
   - Mode conditions trigger callbacks

See test_when_basic.hv for syntax example.

=============================================================================
*/
