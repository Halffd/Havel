"""
Automation Suite Extension for Havel WM

Provides automation tools including file watching, hotkey management,
and condition-based automation rules.

Example:
    from havel.extensions.gui.automation_suite import AutomationExtension
    
    auto = AutomationExtension()
    auto.create_file_watcher("/path/to/watch")
    auto.add_hotkey("ctrl+alt+t", lambda: print("Hotkey pressed!"))
"""

from .AutomationSuite import AutomationSuite
from .ConditionEditorDialog import ConditionEditorDialog

__all__ = ['AutomationSuite', 'ConditionEditorDialog', 'AutomationExtension']

class AutomationExtension:
    """Scriptable interface for automation functionality."""
    
    def __init__(self):
        self._suite = AutomationSuite()
    
    def create_file_watcher(self, path, callback=None):
        """Create a file watcher for the given path."""
        return self._suite.createFileWatcher(path, callback)
    
    def add_hotkey(self, key_combo, callback):
        """Register a global hotkey."""
        return self._suite.registerHotkey(key_combo, callback)
    
    def remove_hotkey(self, key_combo):
        """Unregister a global hotkey."""
        return self._suite.unregisterHotkey(key_combo)
    
    def create_condition_rule(self, condition, action):
        """Create a condition-based automation rule."""
        return self._suite.createConditionRule(condition, action)
    
    def show_condition_editor(self, parent=None):
        """Show the condition editor dialog."""
        dialog = ConditionEditorDialog(self._suite, parent)
        dialog.exec_()
        return dialog

def create_automation_suite():
    """Create and return an AutomationExtension instance."""
    return AutomationExtension()
