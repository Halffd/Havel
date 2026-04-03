"""
File Automator Extension for Havel WM

Provides file automation rules and batch processing.

Example:
    from havel.extensions.gui.file_automator import FileAutomatorExtension
    
    automator = FileAutomatorExtension()
    automator.add_rule("*.txt", "move", "/backup/")
    automator.process_directory("/downloads/")
"""

from .FileAutomator import FileAutomator

__all__ = ['FileAutomator', 'FileAutomatorExtension']

class FileAutomatorExtension:
    """Scriptable interface for file automation."""
    
    def __init__(self):
        self._automator = FileAutomator()
    
    def add_rule(self, pattern, action, target=None):
        """Add a file automation rule.
        
        Args:
            pattern: File pattern (e.g., "*.txt")
            action: Action to perform ("move", "copy", "delete", "rename")
            target: Target path for move/copy/rename
        """
        return self._automator.addRule(pattern, action, target)
    
    def remove_rule(self, pattern):
        """Remove a file automation rule."""
        return self._automator.removeRule(pattern)
    
    def process_file(self, filepath):
        """Process a single file through automation rules."""
        return self._automator.processFile(filepath)
    
    def process_directory(self, directory, recursive=False):
        """Process all files in a directory."""
        return self._automator.processDirectory(directory, recursive)
    
    def get_rules(self):
        """Get all configured rules."""
        return self._automator.getRules()
    
    def enable_rule(self, pattern):
        """Enable a rule."""
        return self._automator.enableRule(pattern)
    
    def disable_rule(self, pattern):
        """Disable a rule."""
        return self._automator.disableRule(pattern)

def create_file_automator():
    """Create and return a FileAutomatorExtension instance."""
    return FileAutomatorExtension()
