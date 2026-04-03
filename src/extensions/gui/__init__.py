"""
Havel WM GUI Extensions

This package provides independent, scriptable GUI extensions for the Havel Window Manager.
Each extension can be imported and used individually in scripts.

Available Extensions:
    - alt_tab: Alt+Tab window switcher with thumbnails
    - automation_suite: Automation tools (file watching, hotkeys, conditions)
    - brightness_panel: Screen brightness control
    - clipboard_manager: Clipboard history and management
    - file_automator: File automation rules and batch processing
    - map_manager: Window/workspace key mapping configuration
    - screenshot_manager: Screenshot capture with region selection
    - system_monitor: System resource monitoring (CPU, memory, disk, network)
    - text_chunker: Text splitting and processing utilities
    - common: Shared widgets and utilities

Example Usage:
    # Import specific extensions
    from havel.extensions.gui import alt_tab, clipboard_manager
    
    # Create and use extensions
    switcher = alt_tab.AltTabExtension()
    clipboard = clipboard_manager.ClipboardExtension()
    
    # Or use the convenience functions
    from havel.extensions.gui.alt_tab import create_alt_tab
    from havel.extensions.gui.clipboard_manager import create_clipboard_manager
    
    switcher = create_alt_tab()
    clipboard = create_clipboard_manager()
"""

__version__ = "1.0.0"
__all__ = [
    'alt_tab',
    'automation_suite',
    'brightness_panel',
    'clipboard_manager',
    'file_automator',
    'map_manager',
    'screenshot_manager',
    'system_monitor',
    'text_chunker',
    'common',
]

# Convenience imports
from . import alt_tab
from . import automation_suite
from . import brightness_panel
from . import clipboard_manager
from . import file_automator
from . import map_manager
from . import screenshot_manager
from . import system_monitor
from . import text_chunker
from . import common
