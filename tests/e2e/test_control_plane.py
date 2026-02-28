"""Control plane command tests for DPDK application.

This module tests the control plane Unix socket interface including:
- Status command and response format
- Get threads command and thread information structure
- Invalid command handling
- Malformed JSON handling
- Command response timing and performance
"""

import pytest
import json
import time
import socket
from typing import Dict, Any


class TestControlPlane:
    """Test control plane command interface."""

    def test_status_command(self, control_client):
        """
        Test status command returns expected format.

        Validates:
        - Status command succeeds
        - Response has correct structure
        - Response contains expected fields

        Requirements: 5.1, 5.2, 5.3, 5.4, 5.8
        """
        response = control_client.status()

        assert 'status' in response, "Response should have 'status' field"
        assert response['status'] == 'success', "Status should be 'success'"

        assert 'result' in response, "Response should have 'result' field"

        result = response['result']
        assert isinstance(result, dict), "Result should be a dictionary"
        assert 'main_lcore' in result, "Result should contain 'main_lcore' field"
        assert 'num_pmd_threads' in result, "Result should contain 'num_pmd_threads' field"
        assert isinstance(result['main_lcore'], int), "main_lcore should be an integer"
        assert isinstance(result['num_pmd_threads'], int), "num_pmd_threads should be an integer"

    def test_get_threads_command(self, control_client):
        """
        Test get_threads command returns thread information structure.

        Validates:
        - get_threads command succeeds
        - Response contains thread array
        - Each thread has lcore_id field

        Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.8
        """
        response = control_client.get_threads()

        assert 'status' in response, "Response should have 'status' field"
        assert response['status'] == 'success', "Status should be 'success'"

        assert 'result' in response, "Response should have 'result' field"

        result = response['result']
        assert 'threads' in result, "Result should contain 'threads' field"

        threads = result['threads']
        assert isinstance(threads, list), "Threads should be a list"
        assert len(threads) > 0, "Should have at least one PMD thread"

        for thread in threads:
            assert isinstance(thread, dict), "Each thread should be a dictionary"
            assert 'lcore_id' in thread, "Thread should have 'lcore_id' field"
            assert isinstance(thread['lcore_id'], int), "lcore_id should be an integer"
            assert thread['lcore_id'] > 0, "PMD thread lcore_id should be > 0"

    def test_invalid_command(self, control_client):
        """
        Test that invalid commands are rejected with error response.

        Validates:
        - Invalid command returns error status
        - Error response contains error field
        - System remains stable after invalid command

        Requirements: 5.6, 5.8
        """
        response = control_client.send_command("invalid_command_xyz")

        assert 'status' in response, "Response should have 'status' field"
        assert response['status'] == 'error', "Status should be 'error' for invalid command"

        assert 'error' in response, "Error response should have 'error' field"
        assert isinstance(response['error'], str), "Error message should be a string"
        assert len(response['error']) > 0, "Error message should not be empty"

        # Verify system is still responsive after invalid command
        status_response = control_client.status()
        assert status_response['status'] == 'success', "System should still respond to valid commands"

    def test_malformed_json(self, control_client):
        """
        Test handling of malformed JSON input.

        Validates:
        - Malformed JSON is detected
        - Error response is returned
        - Connection remains usable after error

        Requirements: 5.6, 5.8
        """
        try:
            control_client.sock.sendall(b"not valid json at all\n")

            response_data = b""
            while True:
                chunk = control_client.sock.recv(4096)
                if not chunk:
                    pytest.fail("Connection closed unexpectedly")

                response_data += chunk

                if b'\n' in response_data:
                    break

            response_str = response_data.decode('utf-8').strip()
            response = json.loads(response_str)

            assert 'status' in response, "Response should have 'status' field"
            assert response['status'] == 'error', "Status should be 'error' for malformed JSON"

        except Exception as e:
            pytest.fail(f"Failed to handle malformed JSON: {e}")

        # Verify connection is still usable
        status_response = control_client.status()
        assert status_response['status'] == 'success', "Connection should still work after malformed JSON"

    def test_command_response_timing(self, control_client):
        """
        Test command response timing meets performance requirements.

        Validates:
        - Commands respond within reasonable time
        - Average response time is acceptable
        - No significant outliers in response time

        Requirements: 5.7, 5.8
        """
        num_iterations = 10
        response_times = []

        for _ in range(num_iterations):
            start_time = time.time()
            response = control_client.status()
            end_time = time.time()

            assert response['status'] == 'success', "Command should succeed"

            response_time = end_time - start_time
            response_times.append(response_time)

        avg_response_time = sum(response_times) / len(response_times)
        max_response_time = max(response_times)
        min_response_time = min(response_times)

        assert avg_response_time < 0.1, \
            f"Average response time {avg_response_time:.3f}s exceeds 100ms threshold"

        assert max_response_time < 0.5, \
            f"Maximum response time {max_response_time:.3f}s exceeds 500ms threshold"

        assert min_response_time < 0.05, \
            f"Minimum response time {min_response_time:.3f}s exceeds 50ms threshold"
