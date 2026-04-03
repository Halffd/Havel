"""
Screenshot Manager Extension for Havel WM

Provides screenshot capture with region selection and annotation.

Example:
    from havel.extensions.gui.screenshot_manager import ScreenshotExtension
    
    ss = ScreenshotExtension()
    ss.capture_fullscreen()
    ss.capture_region(x, y, w, h)
    ss.capture_window("window_id")
"""

from .ScreenshotManager import ScreenshotManager
from .ScreenRegionSelector import ScreenRegionSelector

__all__ = ['ScreenshotManager', 'ScreenRegionSelector', 'ScreenshotExtension']

class ScreenshotExtension:
    """Scriptable interface for screenshot management."""
    
    def __init__(self):
        self._manager = ScreenshotManager()
    
    def capture_fullscreen(self, save_path=None):
        """Capture the entire screen.
        
        Args:
            save_path: Optional path to save the screenshot
        """
        return self._manager.captureFullscreen(save_path)
    
    def capture_region(self, x, y, width, height, save_path=None):
        """Capture a specific screen region.
        
        Args:
            x, y: Top-left coordinates
            width, height: Dimensions of region
            save_path: Optional path to save the screenshot
        """
        return self._manager.captureRegion(x, y, width, height, save_path)
    
    def capture_window(self, window_id, save_path=None):
        """Capture a specific window.
        
        Args:
            window_id: Window identifier
            save_path: Optional path to save the screenshot
        """
        return self._manager.captureWindow(window_id, save_path)
    
    def show_region_selector(self):
        """Show the region selection tool."""
        selector = ScreenRegionSelector()
        selector.show()
        return selector
    
    def copy_to_clipboard(self, image):
        """Copy screenshot to clipboard."""
        return self._manager.copyToClipboard(image)
    
    def annotate(self, image):
        """Open annotation tool for the screenshot."""
        return self._manager.annotate(image)
    
    def get_screenshot_history(self):
        """Get list of recent screenshots."""
        return self._manager.getHistory()

def create_screenshot_manager():
    """Create and return a ScreenshotExtension instance."""
    return ScreenshotExtension()
