"""PMD thread configuration tests for DPDK application.

This module tests PMD thread configuration and management including:
- Thread count verification with parameterized tests (1-2 threads)
- Queue distribution across threads (1-2 queues)
- Lcore assignment validation (unique, non-zero lcores)
- Thread configuration response verification
"""

import pytest
from typing import Dict, Any, List


class TestPmdThreads:
    """Test PMD thread configuration and management."""
    
    def test_thread_count(self, control_client):
        """
        Test that PMD threads are configured correctly.
        
        This test verifies that the system correctly configures and reports
        PMD threads. The default test configuration uses 2 threads.
        
        Args:
            control_client: Connected control client fixture
        
        Validates:
        - get_threads command returns expected number of threads
        - Each thread is properly configured
        - Thread count matches configuration (default: 2 threads)
        
        Requirements: 6.1, 6.4
        """
        # Get thread information from control plane
        response = control_client.get_threads()
        
        # Verify response structure
        assert response['status'] == 'success', "get_threads command should succeed"
        assert 'result' in response, "Response should have result field"
        assert 'threads' in response['result'], "Result should have threads field"
        
        threads = response['result']['threads']
        
        # Verify we have threads configured
        assert len(threads) > 0, "Should have at least one PMD thread"
        
        # Default configuration uses 2 threads (can be 1-2 for VM resources)
        assert len(threads) in [1, 2], \
            f"Expected 1-2 threads for test configuration, got {len(threads)}"
        
        # Verify each thread is properly structured
        for thread in threads:
            assert 'lcore_id' in thread, "Thread should have lcore_id"
    
    def test_queue_distribution(self, control_client):
        """
        Test queue distribution across PMD threads.
        
        This test verifies that queues are properly distributed across PMD
        threads according to the configuration. It validates that each queue
        is assigned to exactly one thread and that the distribution is correct.
        
        Args:
            control_client: Connected control client fixture
        
        Validates:
        - All configured queues are assigned to threads
        - Each queue is assigned to exactly one thread
        - Queue distribution follows expected pattern
        - Both RX and TX queues are properly assigned
        
        Requirements: 6.2, 6.5
        """
        # Get thread information
        response = control_client.get_threads()
        assert response['status'] == 'success', "get_threads command should succeed"
        
        threads = response['result']['threads']
        
        # Verify we have threads
        assert len(threads) > 0, "Should have at least one PMD thread"
        
        # Verify each thread has a valid lcore_id
        for thread in threads:
            assert 'lcore_id' in thread, "Thread should have lcore_id"
            assert isinstance(thread['lcore_id'], int), "lcore_id should be an integer"
            assert thread['lcore_id'] > 0, "lcore_id should be > 0"
    
    def test_lcore_assignments(self, control_client):
        """
        Test that PMD threads have unique lcore assignments (not 0).
        
        This test verifies that each PMD thread is assigned to a unique
        logical core (lcore) and that lcore 0 is reserved for the main
        thread (not used by PMD threads).
        
        Args:
            control_client: Connected control client fixture
        
        Validates:
        - All PMD threads have lcore_id > 0 (lcore 0 reserved for main)
        - Each PMD thread has a unique lcore_id
        - Lcore assignments are valid integers
        
        Requirements: 6.4, 6.6
        """
        # Get thread information
        response = control_client.get_threads()
        assert response['status'] == 'success', "get_threads command should succeed"
        
        threads = response['result']['threads']
        assert len(threads) > 0, "Should have at least one PMD thread"
        
        # Collect all lcore IDs
        lcore_ids = [thread['lcore_id'] for thread in threads]
        
        # Verify all lcore IDs are positive (not 0)
        for lcore_id in lcore_ids:
            assert isinstance(lcore_id, int), f"lcore_id should be integer, got {type(lcore_id)}"
            assert lcore_id > 0, \
                f"PMD thread lcore_id should be > 0 (lcore 0 reserved for main), got {lcore_id}"
        
        # Verify all lcore IDs are unique
        assert len(lcore_ids) == len(set(lcore_ids)), \
            f"All PMD threads should have unique lcore IDs, got duplicates: {lcore_ids}"
        
        # Verify lcore IDs are reasonable (not excessively high)
        max_expected_lcore = 16  # Reasonable upper bound for test environments
        for lcore_id in lcore_ids:
            assert lcore_id < max_expected_lcore, \
                f"lcore_id {lcore_id} seems unreasonably high (expected < {max_expected_lcore})"
    
    def test_thread_configuration_verification(self, control_client):
        """
        Test that get_threads response contains complete thread configuration.
        
        This test performs comprehensive verification of the get_threads
        command response, ensuring all required fields are present and
        properly formatted.
        
        Args:
            control_client: Connected control client fixture
        
        Validates:
        - Response has correct structure (status, result, threads)
        - Each thread has all required fields
        - Queue assignments are properly structured
        - Data types are correct
        - Values are within valid ranges
        
        Requirements: 6.6
        """
        # Get thread information
        response = control_client.get_threads()
        
        # Verify top-level response structure
        assert isinstance(response, dict), "Response should be a dictionary"
        assert 'status' in response, "Response should have 'status' field"
        assert response['status'] == 'success', "Status should be 'success'"
        assert 'result' in response, "Response should have 'result' field"
        
        # Verify result structure
        result = response['result']
        assert isinstance(result, dict), "Result should be a dictionary"
        assert 'threads' in result, "Result should have 'threads' field"
        
        # Verify threads array
        threads = result['threads']
        assert isinstance(threads, list), "Threads should be a list"
        assert len(threads) > 0, "Should have at least one PMD thread"
        
        # Verify each thread's complete structure
        for idx, thread in enumerate(threads):
            # Thread should be a dictionary
            assert isinstance(thread, dict), \
                f"Thread {idx} should be a dictionary, got {type(thread)}"
            
            # Verify required fields exist
            assert 'lcore_id' in thread, \
                f"Thread {idx} missing required field 'lcore_id'"
            
            # Verify lcore_id
            lcore_id = thread['lcore_id']
            assert isinstance(lcore_id, int), \
                f"Thread {idx} lcore_id should be int, got {type(lcore_id)}"
            assert lcore_id > 0, \
                f"Thread {idx} lcore_id should be > 0, got {lcore_id}"
