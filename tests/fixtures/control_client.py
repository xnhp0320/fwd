"""Control client for DPDK control plane Unix socket."""

import socket
import json
import time
from typing import Dict, Any, Optional
from pathlib import Path


class ControlClient:
    """Client for DPDK control plane Unix socket."""
    
    def __init__(self, socket_path: str = "/tmp/dpdk_control.sock"):
        """
        Initialize control client.
        
        Args:
            socket_path: Path to Unix domain socket
        """
        self.socket_path = socket_path
        self.sock: Optional[socket.socket] = None
    
    def connect(self, timeout: float = 5.0, retry_count: int = 10, retry_delay: float = 0.5) -> bool:
        """
        Connect to the control plane socket with retry logic.
        
        Args:
            timeout: Socket operation timeout in seconds
            retry_count: Number of connection attempts
            retry_delay: Delay between retry attempts in seconds
        
        Returns:
            True if connected successfully
        """
        for attempt in range(retry_count):
            try:
                # Create Unix domain socket
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.sock.settimeout(timeout)
                
                # Attempt connection
                self.sock.connect(self.socket_path)
                return True
                
            except FileNotFoundError:
                # Socket file doesn't exist yet
                if attempt < retry_count - 1:
                    time.sleep(retry_delay)
                    if self.sock:
                        self.sock.close()
                        self.sock = None
                else:
                    if self.sock:
                        self.sock.close()
                        self.sock = None
                    return False
                    
            except ConnectionRefusedError:
                # Server not ready yet
                if attempt < retry_count - 1:
                    time.sleep(retry_delay)
                    if self.sock:
                        self.sock.close()
                        self.sock = None
                else:
                    if self.sock:
                        self.sock.close()
                        self.sock = None
                    return False
                    
            except Exception as e:
                # Other connection errors
                if self.sock:
                    self.sock.close()
                    self.sock = None
                return False
        
        return False
    
    def send_command(self, command: str, **kwargs) -> Dict[str, Any]:
        """
        Send a command to the control plane.
        
        Args:
            command: Command name (e.g., "status", "get_threads")
            **kwargs: Additional command parameters
        
        Returns:
            Parsed JSON response
        
        Raises:
            ConnectionError: If not connected
            ValueError: If response is invalid JSON
            TimeoutError: If response not received within timeout
        """
        if self.sock is None:
            raise ConnectionError("Not connected to control socket")
        
        # Build command JSON
        cmd_dict = {"command": command}
        if kwargs:
            cmd_dict.update(kwargs)
        
        # Serialize to JSON with newline delimiter
        cmd_json = json.dumps(cmd_dict) + "\n"
        
        try:
            # Send command
            self.sock.sendall(cmd_json.encode('utf-8'))
            
            # Receive response
            response_data = b""
            while True:
                chunk = self.sock.recv(4096)
                if not chunk:
                    # Server closed connection — use whatever we have
                    if response_data:
                        break
                    raise ConnectionError("Connection closed by server")
                
                response_data += chunk
                
                # Check if we have a complete JSON response (ends with newline)
                if b'\n' in response_data:
                    break
            
            # Parse JSON response
            response_str = response_data.decode('utf-8').strip()
            response = json.loads(response_str)
            
            return response
            
        except socket.timeout:
            raise TimeoutError(f"Command '{command}' timed out")
        except json.JSONDecodeError as e:
            raise ValueError(f"Invalid JSON response: {e}")
        except Exception as e:
            raise ConnectionError(f"Failed to send command: {e}")
    
    def status(self) -> Dict[str, Any]:
        """
        Send status command.
        
        Returns:
            Status response dictionary
        """
        return self.send_command("status")
    
    def get_threads(self) -> Dict[str, Any]:
        """
        Send get_threads command.
        
        Returns:
            Thread information response dictionary
        """
        return self.send_command("get_threads")
    
    def shutdown(self) -> Dict[str, Any]:
        """
        Send shutdown command.

        Returns:
            Shutdown response dictionary.
            If the server closes the connection before responding
            (expected during shutdown), returns a synthetic success response.
        """
        try:
            return self.send_command("shutdown")
        except ConnectionError:
            # Server may close the connection before we read the response.
            # This is expected behavior for a shutdown command.
            return {"status": "success", "result": {"message": "Shutdown initiated"}}
    
    def close(self) -> None:
        """Close the socket connection."""
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass  # Ignore errors during close
            finally:
                self.sock = None
    
    def __enter__(self):
        """Context manager entry."""
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.close()
        return False  # Don't suppress exceptions
