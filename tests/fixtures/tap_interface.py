"""TAP interface management for DPDK testing.

This module provides utilities for verifying and managing TAP network interfaces
created by DPDK's net_tap virtual PMD driver during testing.
"""

import subprocess
import time
from typing import List, Optional, Dict, Any
import re


class TapInterfaceManager:
    """Manages TAP network interfaces for testing."""
    
    @staticmethod
    def interface_exists(interface_name: str) -> bool:
        """
        Check if a TAP interface exists.
        
        Args:
            interface_name: Interface name (e.g., "dtap0")
        
        Returns:
            True if interface exists
        """
        try:
            result = subprocess.run(
                ['ip', 'link', 'show', interface_name],
                capture_output=True,
                text=True,
                timeout=5
            )
            return result.returncode == 0
        except (subprocess.TimeoutExpired, FileNotFoundError):
            return False
    
    @staticmethod
    def wait_for_interface(
        interface_name: str,
        timeout: float = 10.0
    ) -> bool:
        """
        Wait for a TAP interface to be created.
        
        Args:
            interface_name: Interface name to wait for
            timeout: Maximum seconds to wait
        
        Returns:
            True if interface appeared within timeout
        """
        start_time = time.time()
        while time.time() - start_time < timeout:
            if TapInterfaceManager.interface_exists(interface_name):
                return True
            time.sleep(0.1)
        return False
    
    @staticmethod
    def get_interface_info(interface_name: str) -> Optional[Dict[str, Any]]:
        """
        Get information about a TAP interface.
        
        Args:
            interface_name: Interface name
        
        Returns:
            Dictionary with interface info (state, mtu, etc.) or None
        """
        try:
            result = subprocess.run(
                ['ip', 'link', 'show', interface_name],
                capture_output=True,
                text=True,
                timeout=5
            )
            
            if result.returncode != 0:
                return None
            
            output = result.stdout
            info = {}
            
            # Parse state (UP/DOWN)
            if 'state UP' in output:
                info['state'] = 'UP'
            elif 'state DOWN' in output:
                info['state'] = 'DOWN'
            else:
                info['state'] = 'UNKNOWN'
            
            # Parse MTU
            mtu_match = re.search(r'mtu (\d+)', output)
            if mtu_match:
                info['mtu'] = int(mtu_match.group(1))
            
            # Parse MAC address
            mac_match = re.search(r'link/ether ([0-9a-f:]+)', output)
            if mac_match:
                info['mac'] = mac_match.group(1)
            
            # Parse interface index
            index_match = re.search(r'^(\d+):', output, re.MULTILINE)
            if index_match:
                info['index'] = int(index_match.group(1))
            
            return info
            
        except (subprocess.TimeoutExpired, FileNotFoundError):
            return None
    
    @staticmethod
    def set_interface_up(interface_name: str) -> bool:
        """
        Bring a TAP interface up.
        
        Args:
            interface_name: Interface name
        
        Returns:
            True if successful
        """
        try:
            result = subprocess.run(
                ['ip', 'link', 'set', interface_name, 'up'],
                capture_output=True,
                text=True,
                timeout=5
            )
            return result.returncode == 0
        except (subprocess.TimeoutExpired, FileNotFoundError):
            return False
    
    @staticmethod
    def verify_interfaces(interface_names: List[str]) -> bool:
        """
        Verify that all specified interfaces exist.
        
        Args:
            interface_names: List of interface names to verify
        
        Returns:
            True if all interfaces exist
        """
        return all(
            TapInterfaceManager.interface_exists(name)
            for name in interface_names
        )
