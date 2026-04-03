"""
Clipboard Manager Extension for Havel WM

Provides clipboard history management with search and persistent storage.

Example:
    from havel.extensions.gui.clipboard_manager import ClipboardExtension
    
    clipboard = ClipboardExtension()
    clipboard.show_history()
    clipboard.search("keyword")
    item = clipboard.get_history_item(0)
"""

from .ClipboardManager import ClipboardManager

__all__ = ['ClipboardManager', 'ClipboardExtension']

class ClipboardExtension:
    """Scriptable interface for clipboard management."""
    
    def __init__(self):
        self._manager = None
    
    def create(self, parent=None):
        """Create the clipboard manager."""
        self._manager = ClipboardManager(parent)
        return self._manager
    
    def show(self):
        """Show the clipboard history window."""
        if self._manager:
            self._manager.showHistory()
        return self
    
    def hide(self):
        """Hide the clipboard manager."""
        if self._manager:
            self._manager.hide()
        return self
    
    def get_history(self, limit=10):
        """Get clipboard history items."""
        if self._manager:
            return self._manager.getHistory(limit)
        return []
    
    def get_history_item(self, index):
        """Get a specific history item by index."""
        if self._manager:
            return self._manager.getHistoryItem(index)
        return None
    
    def search(self, query):
        """Search clipboard history."""
        if self._manager:
            return self._manager.searchHistory(query)
        return []
    
    def clear_history(self):
        """Clear clipboard history."""
        if self._manager:
            self._manager.clearHistory()
        return self
    
    def copy_to_clipboard(self, text):
        """Copy text to clipboard."""
        if self._manager:
            self._manager.copyToClipboard(text)
        return self

def create_clipboard_manager(parent=None):
    """Create and return a ClipboardManager instance."""
    return ClipboardExtension().create(parent)
