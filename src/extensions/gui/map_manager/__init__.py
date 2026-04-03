"""
Map Manager Extension for Havel WM

Provides window/workspace mapping and keyboard shortcut configuration.

Example:
    from havel.extensions.gui.map_manager import MapManagerExtension
    
    maps = MapManagerExtension()
    maps.load_config("/path/to/mappings.json")
    maps.show_editor()
"""

from .MapManagerWindow import MapManagerWindow
from .MappingEditorDialog import MappingEditorDialog

__all__ = ['MapManagerWindow', 'MappingEditorDialog', 'MapManagerExtension']

class MapManagerExtension:
    """Scriptable interface for map management."""
    
    def __init__(self):
        self._manager = MapManagerWindow()
    
    def load_config(self, filepath):
        """Load key mappings from a configuration file."""
        return self._manager.loadConfig(filepath)
    
    def save_config(self, filepath=None):
        """Save key mappings to a configuration file."""
        return self._manager.saveConfig(filepath)
    
    def add_mapping(self, key_combo, action, window_class=None):
        """Add a new key mapping.
        
        Args:
            key_combo: Key combination (e.g., "Alt+F1")
            action: Action to perform (command, script, or function)
            window_class: Optional window class to restrict mapping
        """
        return self._manager.addMapping(key_combo, action, window_class)
    
    def remove_mapping(self, key_combo, window_class=None):
        """Remove a key mapping."""
        return self._manager.removeMapping(key_combo, window_class)
    
    def show_editor(self, parent=None):
        """Show the mapping editor dialog."""
        dialog = MappingEditorDialog(self._manager, parent)
        dialog.exec_()
        return dialog
    
    def get_mappings(self):
        """Get all configured mappings."""
        return self._manager.getMappings()
    
    def get_window_mappings(self, window_class):
        """Get mappings specific to a window class."""
        return self._manager.getWindowMappings(window_class)

def create_map_manager():
    """Create and return a MapManagerExtension instance."""
    return MapManagerExtension()
