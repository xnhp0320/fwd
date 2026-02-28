"""DPDK process lifecycle management for testing."""

import fcntl
import os
import selectors
import subprocess
import time
import signal
from pathlib import Path
from typing import Optional, List
import threading


class DpdkProcess:
    """Manages DPDK application process lifecycle."""

    def __init__(
        self,
        binary_path: str,
        config_path: str,
        startup_timeout: int = 30,
        shutdown_timeout: int = 10
    ):
        self.binary_path = binary_path
        self.config_path = config_path
        self.startup_timeout = startup_timeout
        self.shutdown_timeout = shutdown_timeout
        self.process: Optional[subprocess.Popen] = None
        self.stdout_lines: List[str] = []
        self.stderr_lines: List[str] = []
        self.error_message: str = ""
        self._output_thread: Optional[threading.Thread] = None
        self._stop_output_capture = False

    @staticmethod
    def _set_nonblocking(fd):
        """Set a file descriptor to non-blocking mode."""
        flags = fcntl.fcntl(fd, fcntl.F_GETFL)
        fcntl.fcntl(fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)

    def start(self) -> bool:
        """Launch the DPDK process. Returns True on success."""
        try:
            self.process = subprocess.Popen(
                [self.binary_path, '-i', self.config_path],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

            # Make both pipes non-blocking
            self._set_nonblocking(self.process.stdout.fileno())
            self._set_nonblocking(self.process.stderr.fileno())

            # Start selector-based output capture thread
            self._stop_output_capture = False
            self._output_thread = threading.Thread(
                target=self._capture_output,
                daemon=True
            )
            self._output_thread.start()

            return True

        except FileNotFoundError:
            self.error_message = f"Binary not found: {self.binary_path}"
            return False
        except PermissionError:
            self.error_message = f"Permission denied: {self.binary_path}"
            return False
        except Exception as e:
            self.error_message = f"Failed to launch process: {e}"
            return False

    def _capture_output(self):
        """Capture stdout and stderr using selectors + non-blocking I/O."""
        if not self.process or not self.process.stdout or not self.process.stderr:
            return

        sel = selectors.DefaultSelector()
        sel.register(self.process.stdout, selectors.EVENT_READ, 'stdout')
        sel.register(self.process.stderr, selectors.EVENT_READ, 'stderr')

        stdout_buf = b''
        stderr_buf = b''

        open_streams = 2
        while open_streams > 0 and not self._stop_output_capture:
            # Short timeout so we can check _stop_output_capture periodically
            events = sel.select(timeout=0.1)
            for key, _ in events:
                try:
                    data = key.fileobj.read(4096)
                except (OSError, ValueError):
                    data = b''

                if not data:
                    # EOF on this stream
                    sel.unregister(key.fileobj)
                    open_streams -= 1
                    continue

                if key.data == 'stdout':
                    stdout_buf += data
                    while b'\n' in stdout_buf:
                        line, stdout_buf = stdout_buf.split(b'\n', 1)
                        self.stdout_lines.append(line.decode('utf-8', errors='replace'))
                else:
                    stderr_buf += data
                    while b'\n' in stderr_buf:
                        line, stderr_buf = stderr_buf.split(b'\n', 1)
                        self.stderr_lines.append(line.decode('utf-8', errors='replace'))

        # Flush remaining partial lines
        if stdout_buf:
            self.stdout_lines.append(stdout_buf.decode('utf-8', errors='replace'))
        if stderr_buf:
            self.stderr_lines.append(stderr_buf.decode('utf-8', errors='replace'))

        sel.close()

    def wait_for_ready(self) -> bool:
        """
        Wait for DPDK process to complete initialization.
        Looks for "Control plane ready" message in output.
        """
        start_time = time.time()

        while time.time() - start_time < self.startup_timeout:
            if not self.is_running():
                self.error_message = "Process terminated during initialization"
                return False

            stdout = self.get_stdout()
            if "Control plane ready" in stdout:
                return True

            if "EAL: Error" in stdout or "FATAL" in stdout:
                self.error_message = "Initialization error detected in output"
                return False

            time.sleep(0.1)

        self.error_message = f"Initialization timeout after {self.startup_timeout}s"
        return False

    def is_running(self) -> bool:
        """Check if process is currently running."""
        return self.process is not None and self.process.poll() is None

    def terminate(self, graceful: bool = True) -> bool:
        """Terminate the DPDK process."""
        if not self.process:
            return True

        if not self.is_running():
            return True

        try:
            if graceful:
                self.process.send_signal(signal.SIGINT)
                try:
                    self.process.wait(timeout=self.shutdown_timeout)
                    return True
                except subprocess.TimeoutExpired:
                    pass

            self.process.kill()
            self.process.wait(timeout=5)
            return True

        except Exception as e:
            self.error_message = f"Failed to terminate process: {e}"
            return False
        finally:
            # Stop capture after process terminates so pipes close
            # and the selector loop exits naturally.
            self._stop_output_capture = True
            if self._output_thread:
                self._output_thread.join(timeout=2)

    def get_stdout(self) -> str:
        """Get captured stdout output."""
        return '\n'.join(self.stdout_lines)

    def get_stderr(self) -> str:
        """Get captured stderr output."""
        return '\n'.join(self.stderr_lines)

    def get_exit_code(self) -> Optional[int]:
        """Get process exit code (None if still running)."""
        if self.process is None:
            return None
        return self.process.poll()
