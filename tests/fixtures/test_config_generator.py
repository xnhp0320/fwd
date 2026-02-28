"""Unit tests for TestConfigGenerator."""

import json
import tempfile
import os
from config_generator import TestConfigGenerator


def test_generate_config_basic():
    """Test basic configuration generation."""
    config = TestConfigGenerator.generate_config(
        num_ports=2,
        num_threads=2,
        num_queues=2
    )
    
    # Verify structure
    assert "core_mask" in config
    assert "memory_channels" in config
    assert "additional_params" in config
    assert "ports" in config
    assert "pmd_threads" in config
    
    # Verify core mask
    assert config["core_mask"] == "0x7"  # 3 cores (0, 1, 2)
    
    # Verify additional params include required flags
    params_dict = {param[0]: param[1] for param in config["additional_params"]}
    assert "--no-huge" in params_dict
    assert "--no-pci" in params_dict
    
    # Verify vdev specifications
    vdev_params = [p for p in config["additional_params"] if p[0] == "--vdev"]
    assert len(vdev_params) == 2
    assert any("net_tap0,iface=dtap0" in p[1] for p in vdev_params)
    assert any("net_tap1,iface=dtap1" in p[1] for p in vdev_params)
    
    # Verify ports
    assert len(config["ports"]) == 2
    for port in config["ports"]:
        assert port["num_rx_queues"] == 2
        assert port["num_tx_queues"] == 2
    
    # Verify PMD threads
    assert len(config["pmd_threads"]) == 2
    assert config["pmd_threads"][0]["lcore_id"] == 1
    assert config["pmd_threads"][1]["lcore_id"] == 2


def test_generate_core_mask():
    """Test core mask generation."""
    assert TestConfigGenerator.generate_core_mask(1) == "0x3"  # 2 cores
    assert TestConfigGenerator.generate_core_mask(2) == "0x7"  # 3 cores


def test_distribute_queues():
    """Test queue distribution across threads."""
    threads = TestConfigGenerator.distribute_queues(
        num_ports=2,
        num_queues=2,
        num_threads=2
    )
    
    assert len(threads) == 2
    
    # Verify lcore assignments
    assert threads[0].lcore_id == 1
    assert threads[1].lcore_id == 2
    
    # Verify queue distribution (round-robin)
    # Thread 0: port0/queue0, port1/queue0
    # Thread 1: port0/queue1, port1/queue1
    assert len(threads[0].rx_queues) == 2
    assert len(threads[1].rx_queues) == 2
    
    # Collect all assigned queues
    all_queues = []
    for thread in threads:
        for queue in thread.rx_queues:
            all_queues.append((queue["port_id"], queue["queue_id"]))
    
    # Verify all queues are assigned exactly once
    expected_queues = [(0, 0), (0, 1), (1, 0), (1, 1)]
    assert sorted(all_queues) == sorted(expected_queues)


def test_write_config():
    """Test configuration writing to file."""
    config = TestConfigGenerator.generate_config(num_ports=1, num_threads=1, num_queues=1)
    
    with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.json') as f:
        temp_path = f.name
    
    try:
        TestConfigGenerator.write_config(config, temp_path)
        
        # Read back and verify
        with open(temp_path, 'r') as f:
            loaded_config = json.load(f)
        
        assert loaded_config == config
    finally:
        os.unlink(temp_path)


def test_single_port_single_thread():
    """Test minimal configuration (1 port, 1 thread, 1 queue)."""
    config = TestConfigGenerator.generate_config(
        num_ports=1,
        num_threads=1,
        num_queues=1
    )
    
    assert config["core_mask"] == "0x3"  # 2 cores
    assert len(config["ports"]) == 1
    assert len(config["pmd_threads"]) == 1
    assert len(config["pmd_threads"][0]["rx_queues"]) == 1


def test_parameter_validation():
    """Test parameter validation."""
    try:
        TestConfigGenerator.generate_config(num_ports=3)
        assert False, "Should raise ValueError for num_ports > 2"
    except ValueError as e:
        assert "num_ports" in str(e)
    
    try:
        TestConfigGenerator.generate_config(num_threads=3)
        assert False, "Should raise ValueError for num_threads > 2"
    except ValueError as e:
        assert "num_threads" in str(e)
    
    try:
        TestConfigGenerator.generate_config(num_queues=3)
        assert False, "Should raise ValueError for num_queues > 2"
    except ValueError as e:
        assert "num_queues" in str(e)


if __name__ == "__main__":
    test_generate_config_basic()
    test_generate_core_mask()
    test_distribute_queues()
    test_write_config()
    test_single_port_single_thread()
    test_parameter_validation()
    print("All tests passed!")
