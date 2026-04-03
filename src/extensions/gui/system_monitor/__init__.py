"""
System Monitor Extension for Havel WM

Provides system resource monitoring with CPU, memory, disk, and network stats.

Example:
    from havel.extensions.gui.system_monitor import SystemMonitorExtension
    
    monitor = SystemMonitorExtension()
    monitor.show()
    stats = monitor.get_cpu_stats()
    memory = monitor.get_memory_stats()
"""

from .SystemMonitor import SystemMonitor

__all__ = ['SystemMonitor', 'SystemMonitorExtension']

class SystemMonitorExtension:
    """Scriptable interface for system monitoring."""
    
    def __init__(self):
        self._monitor = None
    
    def create(self, parent=None):
        """Create the system monitor window."""
        self._monitor = SystemMonitor(parent)
        return self._monitor
    
    def show(self):
        """Show the system monitor window."""
        if self._monitor:
            self._monitor.show()
        return self
    
    def hide(self):
        """Hide the system monitor."""
        if self._monitor:
            self._monitor.hide()
        return self
    
    def get_cpu_stats(self):
        """Get current CPU statistics."""
        if self._monitor:
            return self._monitor.getCpuStats()
        return {}
    
    def get_memory_stats(self):
        """Get current memory statistics."""
        if self._monitor:
            return self._monitor.getMemoryStats()
        return {}
    
    def get_disk_stats(self):
        """Get current disk statistics."""
        if self._monitor:
            return self._monitor.getDiskStats()
        return {}
    
    def get_network_stats(self):
        """Get current network statistics."""
        if self._monitor:
            return self._monitor.getNetworkStats()
        return {}
    
    def get_process_list(self, limit=20):
        """Get list of top processes by resource usage.
        
        Args:
            limit: Maximum number of processes to return
        """
        if self._monitor:
            return self._monitor.getProcessList(limit)
        return []
    
    def set_alert_threshold(self, resource, threshold):
        """Set alert threshold for a resource.
        
        Args:
            resource: Resource type ("cpu", "memory", "disk")
            threshold: Threshold percentage (0-100)
        """
        if self._monitor:
            self._monitor.setAlertThreshold(resource, threshold)
        return self

def create_system_monitor(parent=None):
    """Create and return a SystemMonitor instance."""
    return SystemMonitorExtension().create(parent)
