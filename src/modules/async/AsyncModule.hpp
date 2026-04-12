/*
 * AsyncModule.hpp - REMOVED
 *
 * This module is no longer used.
 * Reason: Dead code from interpreter removal (bytecode VM doesn't use this)
 *
 * AsyncModule was a module-level async implementation that required the interpreter.
 * With bytecode compilation, async operations are now handled by:
 * - Scheduler (cooperative task execution)
 * - Fiber (per-task execution context)
 * - EventQueue (callback bridge for async operations)
 *
 * The declaration (registerAsyncModule) has been removed from HostModules.cpp.
 * This file can be deleted as it serves no purpose.
 */

#pragma once

// Empty stub - module not used

