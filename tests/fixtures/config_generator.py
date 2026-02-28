"""Test configuration generator for DPDK e2e tests.

This module provides utilities to generate dpdk.json configuration files
for testing with net_tap virtual PMD driver.
"""

from dataclasses import dataclass, asdict
from typing import List, Dict, Any
import json


@dataclass
class QueueAssignment:
    """Represents a (port, queue) assignment."""
    port_id: int
    queue_id: int


@dataclass
class PmdThreadConfig:
    """Configuration for a single PMD thread."""
    lcore_id: int
    rx_queues: List[Dict[str, int]]
    tx_queues: List[Dict[str, int]]


@dataclass
class PortConfig:
    """Configuration for a DPDK port."""
    port_id: int
    num_rx_queues: int
    num_tx_queues: int
    num_descriptors: int
    mbuf_pool_size: int
    mbuf_size: int


class TestConfigGenerator:
    """Generates DPDK configuration files for testing with net_tap virtual PMD."""
    
    @staticmethod
    def generate_config(
        num_ports: int = 2,
        num_threads: int = 2,
        num_queues: int = 2,
        use_hugepages: bool = False
    ) -> Dict[str, Any]:
        """
        Generate a test configuration with net_tap virtual PMD.
        
        Args:
            num_ports: Number of virtual ports (1-2)
            num_threads: Number of PMD threads (1-2)
            num_queues: Number of queues per port (1-2)
            use_hugepages: Whether to use hugepages (default: False)
        
        Returns:
            Dictionary representing dpdk.json configuration
        
        Raises:
            ValueError: If parameters are out of valid range
        """
        # Validate parameters (limited for VM resource constraints)
        if not 1 <= num_ports <= 2:
            raise ValueError("num_ports must be 1-2 (VM resource constraint)")
        if not 1 <= num_threads <= 2:
            raise ValueError("num_threads must be 1-2 (VM resource constraint)")
        if not 1 <= num_queues <= 2:
            raise ValueError("num_queues must be 1-2 (VM resource constraint)")
        
        # Generate core mask (main lcore + worker lcores)
        core_mask = TestConfigGenerator.generate_core_mask(num_threads)
        
        # Generate additional EAL parameters
        additional_params = []
        if not use_hugepages:
            additional_params.append(["--no-huge", ""])
        additional_params.append(["--no-pci", ""])
        # Use file-prefix to avoid /var/run/dpdk permission issues
        additional_params.append(["--file-prefix", "dpdk_test"])
        
        # Add vdev for each port with net_tap
        for port_id in range(num_ports):
            vdev_spec = f"net_tap{port_id},iface=dtap{port_id}"
            additional_params.append(["--vdev", vdev_spec])
        
        # Generate port configurations
        ports = []
        for port_id in range(num_ports):
            port = PortConfig(
                port_id=port_id,
                num_rx_queues=num_queues,
                num_tx_queues=num_queues,
                num_descriptors=512,  # Small for testing
                mbuf_pool_size=4096,  # Sufficient for test workloads
                mbuf_size=2048        # Standard Ethernet
            )
            ports.append(asdict(port))
        
        # Distribute queues across threads
        pmd_threads = TestConfigGenerator.distribute_queues(
            num_ports, num_queues, num_threads
        )
        
        # Build configuration
        config = {
            "core_mask": core_mask,
            "memory_channels": 4,
            "additional_params": additional_params,
            "ports": ports,
            "pmd_threads": [asdict(t) for t in pmd_threads]
        }
        
        return config
    
    @staticmethod
    def generate_core_mask(num_threads: int) -> str:
        """
        Generate core mask for given number of threads.
        Reserves lcore 0 for main thread.
        
        Args:
            num_threads: Number of PMD worker threads
        
        Returns:
            Hexadecimal core mask string (e.g., "0x7" for 3 cores)
        
        Example:
            >>> TestConfigGenerator.generate_core_mask(1)
            '0x3'  # lcores 0, 1
            >>> TestConfigGenerator.generate_core_mask(2)
            '0x7'  # lcores 0, 1, 2
        """
        # Main lcore (0) + worker lcores (1..num_threads)
        total_cores = num_threads + 1
        mask = (1 << total_cores) - 1
        return f"0x{mask:x}"
    
    @staticmethod
    def distribute_queues(
        num_ports: int,
        num_queues: int,
        num_threads: int
    ) -> List[PmdThreadConfig]:
        """
        Distribute queues evenly across PMD threads using round-robin.
        
        Strategy:
        - Round-robin assignment of (port, queue) pairs to threads
        - Each thread gets approximately equal number of queues
        
        Args:
            num_ports: Number of ports
            num_queues: Number of queues per port
            num_threads: Number of PMD threads
        
        Returns:
            List of PMD thread configurations
        
        Example:
            With 2 ports, 2 queues, 2 threads:
            - Thread 1 (lcore 1): port0/queue0, port1/queue0
            - Thread 2 (lcore 2): port0/queue1, port1/queue1
        """
        # Create thread configs
        threads = []
        for i in range(num_threads):
            lcore_id = i + 1  # Skip lcore 0 (main)
            threads.append(PmdThreadConfig(
                lcore_id=lcore_id,
                rx_queues=[],
                tx_queues=[]
            ))
        
        # Distribute queues round-robin
        thread_idx = 0
        for port_id in range(num_ports):
            for queue_id in range(num_queues):
                assignment = {"port_id": port_id, "queue_id": queue_id}
                threads[thread_idx].rx_queues.append(assignment)
                threads[thread_idx].tx_queues.append(assignment)
                thread_idx = (thread_idx + 1) % num_threads
        
        return threads
    
    @staticmethod
    def write_config(config: Dict[str, Any], path: str) -> None:
        """
        Write configuration to JSON file.
        
        Args:
            config: Configuration dictionary
            path: Output file path
        
        Example:
            >>> config = TestConfigGenerator.generate_config()
            >>> TestConfigGenerator.write_config(config, "test_config.json")
        """
        with open(path, 'w') as f:
            json.dump(config, f, indent=2)
