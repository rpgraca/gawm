import socket
import subprocess
import time
import os
import numpy as np

class Gawm:
    def __init__(self, port=None, exe="gawm"):
        self.exe = exe
        self.port = port or self._find_free_port()
        self.proc = None
        self.sock = None

    def _find_free_port(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(('', 0))
            return s.getsockname()[1]

    def start(self):
        """Start the gawm process and connect."""
        cmd = [self.exe, "-p", str(self.port)]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        
        # Try to connect
        for _ in range(20):
            try:
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.sock.connect(("localhost", self.port))
                return True
            except ConnectionRefusedError:
                time.sleep(0.1)
        return False

    def send_command(self, cmd):
        """Send a command string to gawm."""
        if not self.sock:
            raise RuntimeError("Not connected to gawm")
        self.sock.sendall((cmd + "\n").encode("utf-8"))
        # Wait for ack (\n or error message)
        data = self.sock.recv(4096).decode("utf-8")
        if data.strip() != "":
            print(f"Gawm Error: {data.strip()}")
            return False
        return True

    def plot_analysis(self, analysis, table_name="pyspice"):
        """Plot a PySpice Analysis object."""
        # 1. Create new table
        self.send_command(f"table_new {table_name}")
        
        # 2. Define variables
        # X variable is always the first one (time or frequency)
        if hasattr(analysis, "time"):
            x_name = "time"
            x_data = np.array(analysis.time)
        elif hasattr(analysis, "frequency"):
            x_name = "frequency"
            x_data = np.array(analysis.frequency)
        else:
            # For DC sweep etc.
            x_name = "x"
            # Get the first waveform as X if possible
            x_data = np.array(next(iter(analysis.waveforms.values())))

        y_names = list(analysis.waveforms.keys())
        all_names = [x_name] + y_names
        self.send_command(f"variables {' '.join(all_names)}")
        
        # 3. Set types (optional but good for units)
        # TODO: map PySpice units to gawm types if needed
        
        # 4. Send data row by row
        self.send_command("rowdatas")
        
        y_data_list = [np.array(analysis.waveforms[name]) for name in y_names]
        
        for i in range(len(x_data)):
            row = [x_data[i]] + [y_data[i] for y_data in y_data_list]
            row_str = " ".join(map(str, row))
            self.sock.sendall((row_str + "\n").encode("utf-8"))
        
        self.send_command("enddata")

    def close(self):
        if self.sock:
            self.sock.close()
        if self.proc:
            self.proc.terminate()

if __name__ == "__main__":
    # Example test script
    g = Gawm()
    if g.start():
        print("Gawm started.")
        # Create dummy data
        class Dummy: pass
        d = Dummy()
        d.time = np.linspace(0, 0.01, 100)
        d.waveforms = {
            "sin": np.sin(2 * np.pi * 100 * d.time),
            "cos": np.cos(2 * np.pi * 100 * d.time)
        }
        g.plot_analysis(d)
        time.sleep(5)
        g.close()
