"""
Brightness Panel Extension for Havel WM

Provides screen brightness control with slider interface.

Example:
    from havel.extensions.gui.brightness_panel import BrightnessExtension
    
    panel = BrightnessExtension()
    panel.show()
    panel.set_brightness(75)
    current = panel.get_brightness()
"""

from .BrightnessPanel import BrightnessPanel

__all__ = ['BrightnessPanel', 'BrightnessExtension']

class BrightnessExtension:
    """Scriptable interface for brightness control."""
    
    def __init__(self):
        self._panel = None
    
    def create(self, parent=None):
        """Create the brightness panel."""
        self._panel = BrightnessPanel(parent)
        return self._panel
    
    def show(self):
        """Show the brightness panel."""
        if self._panel:
            self._panel.show()
        return self
    
    def hide(self):
        """Hide the brightness panel."""
        if self._panel:
            self._panel.hide()
        return self
    
    def set_brightness(self, level):
        """Set brightness level (0-100)."""
        if self._panel:
            self._panel.setBrightness(level)
        return self
    
    def get_brightness(self):
        """Get current brightness level."""
        if self._panel:
            return self._panel.getBrightness()
        return 0
    
    def increase(self, amount=5):
        """Increase brightness by amount."""
        if self._panel:
            self._panel.increaseBrightness(amount)
        return self
    
    def decrease(self, amount=5):
        """Decrease brightness by amount."""
        if self._panel:
            self._panel.decreaseBrightness(amount)
        return self

def create_brightness_panel(parent=None):
    """Create and return a BrightnessPanel instance."""
    return BrightnessExtension().create(parent)
