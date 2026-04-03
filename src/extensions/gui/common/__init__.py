"""
Common GUI Utilities Extension for Havel WM

Provides shared components used by other GUI extensions.

This module contains base widgets, hotkey capture, and shared utilities
that are used across multiple eextensions.
"""

from .BaseWidget import BaseWidget
from .HotkeyCapture import HotkeyCapture
from .Theme import Theme

__all__ = ['BaseWidget', 'HotkeyCapture', 'Theme', 'GUIManager', 'GUILauncher', 'ScriptRunner', 'SettingsWindow']

# Import optional components if they exist
try:
    from .GUIManager import GUIManager
    from .GUILauncher import GUILauncher
    from .ScriptRunner import ScriptRunner
    from .SettingsWindow import SettingsWindow
    __all__.extend(['GUIManager', 'GUILauncher', 'ScriptRunner', 'SettingsWindow'])
except ImportError:
    pass

# Utility functions
def get_theme():
    """Get the current theme."""
    return Theme.getCurrent()

def set_theme(theme_name):
    """Set the current theme."""
    return Theme.setTheme(theme_name)

def capture_hotkey(callback):
    """Capture a hotkey combination.
    
    Args:
        callback: Function to call with captured hotkey string
    """
    capture = HotkeyCapture()
    capture.capture(callback)
    return capture
