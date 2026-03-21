import socket
import subprocess
import time
import os
import numpy as np

class Gawm:
    def __init__(self, port=None, exe="gawm"):
        self.exe = exe
        self.port = port or int(os.environ.get("GAWM_PORT", 0)) or 4242
        self.proc = None
        self.sock = None

    def start(self):
        """Start a new gawm process and connect."""
        cmd = [self.exe, "-p", str(self.port)]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return self.connect()

    def connect(self):
        """Connect to an already running gawm instance."""
        for _ in range(20):
            try:
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.sock.connect(("localhost", self.port))
                return True
            except ConnectionRefusedError:
                time.sleep(0.1)
        return False

    def __repr__(self):
        status = "connected" if self.sock else "disconnected"
        return f"<Gawm object on port {self.port} ({status})>"

    def send_command(self, cmd):
        """Send a command string to gawm."""
        if not self.sock:
            raise RuntimeError("Not connected to gawm")
        self.sock.sendall((cmd + "\n").encode("utf-8"))
        # Note: some commands (like get_data) will send data before the final ack.
        # We don't read the ack here for get_data.
        return True

    def _wait_for_ack(self):
        data = self.sock.recv(4096).decode("utf-8")
        if data.strip() != "":
            if not data.startswith("\n"):
                print(f"Gawm Error: {data.strip()}")
                return False
        return True

    def get_data(self, name):
        """Get waveform data from gawm by name."""
        self.send_command(f"get_data {name}")
        
        # Read nrows
        f = self.sock.makefile()
        line = f.readline()
        if not line or not line.strip().isdigit():
            # Might be an error message
            print(f"Gawm Error: {line.strip()}")
            return None
        
        nrows = int(line.strip())
        x = np.zeros(nrows)
        y = np.zeros(nrows)
        
        for i in range(nrows):
            line = f.readline()
            vals = list(map(float, line.split()))
            x[i] = vals[0]
            y[i] = vals[1]
        
        return {"x": x, "y": y}

    def get_tables(self):
        """Get list of loaded table names."""
        self.send_command("table_list")
        f = self.sock.makefile()
        line = f.readline()
        if not line or not line.strip().isdigit():
            print(f"Gawm Error: {line.strip()}")
            return []
        
        count = int(line.strip())
        tables = []
        for _ in range(count):
            tables.append(f.readline().strip())
        return tables

    def add_waveform(self, name, x_data, y_data, table_name="python"):
        """Add a single waveform to a table."""
        self.plot(x_data, [y_data], [name], table_name=table_name)

    def plot(self, x_data, y_datas, names, table_name="python"):
        """Plot multiple waveforms sharing the same X axis.
        y_datas should be a list of arrays.
        names should be a list of strings.
        """
        self.send_command(f"table_new {table_name}")
        vars_str = "x " + " ".join(names)
        self.send_command(f"variables {vars_str}")
        self.send_command("rowdatas")
        
        # Convert list of arrays to a 2D matrix for row-wise iteration
        data_matrix = np.column_stack([x_data] + list(y_datas))
        
        for row in data_matrix:
            row_str = " ".join(map(str, row))
            self.sock.sendall((row_str + "\n").encode("utf-8"))
        
        self.send_command("enddata")

    def plot_analysis(self, analysis, table_name="pyspice"):
        """Plot a PySpice Analysis object."""
        self.send_command(f"table_new {table_name}")
        
        if hasattr(analysis, "time"):
            x_name = "time"
            x_data = np.array(analysis.time)
        elif hasattr(analysis, "frequency"):
            x_name = "frequency"
            x_data = np.array(analysis.frequency)
        else:
            x_name = "x"
            x_data = np.arange(len(next(iter(analysis.waveforms.values()))))

        y_names = list(analysis.waveforms.keys())
        all_names = [x_name] + y_names
        self.send_command(f"variables {' '.join(all_names)}")
        
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
    # If run standalone, it will try to connect to a running gawm
    g = Gawm()
    if g.connect():
        print(f"Connected to gawm on port {g.port}")
        # x = np.linspace(0, 10, 100)
        # g.add_waveform("test", x, np.sin(x))
    else:
        print("Could not connect to gawm. Is it running with -p?")
