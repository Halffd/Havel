"""
Alt+Tab Window Switcher Extension for Havel WM

Provides an Alt+Tab style window switcher with thumbnail previews.
Can be imported and used from scripts.

Example:
    from havel.extensions.gui.alt_tab import AltTabExtension
    
    alt_tab = AltTabExtension()
    alt_tab.show()
    alt_tab.select_next()
"""

from .AltTab import AltTabWindow

__all__ = ['AltTabWindow', 'AltTabExtension']

class AltTabExtension:
    """Scriptable interface for Alt+Tab functionality."""
    
    def __init__(self):
        self._window = None
    
    def create(self, parent=None):
        """Create the Alt+Tab window."""
        self._window = AltTabWindow(parent)
        return self._window
    
    def show(self):
        """Show the Alt+Tab window."""
        if self._window:
            self._window.show()
        return self
    
    def hide(self):
        """Hide the Alt+Tab window."""
        if self._window:
            self._window.hide()
        return self
    
    def select_next(self):
        """Select next window."""
        if self._window:
            self._window.selectNext()
        return self
    
    def select_previous(self):
        """Select previous window."""
        if self._window:
            self._window.selectPrevious()
        return self
    
    def activate_selected(self):
        """Activate the currently selected window."""
        if self._window:
            self._window.activateSelected()
        return self

# Convenience function
def create_alt_tab(parent=None):
    """Create and return an AltTabExtension instance."""
    return AltTabExtension().create(parent)
