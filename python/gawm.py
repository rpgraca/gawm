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
        self._reader = None
        self._reader_sock = None

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
        return True

    def _get_reader(self):
        if not self.sock:
            raise RuntimeError("Not connected to gawm")
        if self._reader_sock is not self.sock:
            if self._reader is not None:
                self._reader.close()
            self._reader = self.sock.makefile()
            self._reader_sock = self.sock
        return self._reader

    def _wait_for_ack(self):
        data = self._get_reader().readline()
        if data == "":
            print("Gawm Error: unexpected EOF waiting for acknowledgement")
            return False
        if data.strip() != "":
            print(f"Gawm Error: {data.strip()}")
            return False
        return True

    # --- Data retrieval ---

    def get_data(self, name):
        """Get waveform data from gawm by name. Returns dict with 'x' and 'y' numpy arrays."""
        self.send_command(f"get_data {name}")

        f = self._get_reader()
        line = f.readline()
        if not line or not line.strip().isdigit():
            print(f"Gawm Error: {line.strip()}")
            return None

        nrows = int(line.strip())
        x = np.zeros(nrows)
        y = np.zeros(nrows)

        for i in range(nrows):
            line = f.readline()
            if not line:
                print("Gawm Error: unexpected EOF reading waveform data")
                return None
            vals = list(map(float, line.split()))
            x[i] = vals[0]
            y[i] = vals[1]

        if not self._wait_for_ack():
            return None
        return {"x": x, "y": y}

    def get_tables(self):
        """Get list of loaded table names."""
        self.send_command("table_list")
        f = self._get_reader()
        line = f.readline()
        if not line or not line.strip().isdigit():
            print(f"Gawm Error: {line.strip()}")
            return []

        count = int(line.strip())
        tables = []
        for _ in range(count):
            line = f.readline()
            if not line:
                print("Gawm Error: unexpected EOF reading table list")
                return []
            tables.append(line.strip())
        if not self._wait_for_ack():
            return []
        return tables

    def get_cursor(self, idx=0):
        """Get cursor X position. Returns float or None if cursor is not visible."""
        self.send_command(f"get_cursor {idx}")
        f = self._get_reader()
        response = f.readline()
        if response == "":
            print("Gawm Error: unexpected EOF reading cursor")
            return None
        line = response.strip()
        if line == "":
            self._wait_for_ack()
            return None
        try:
            x = float(line)
        except ValueError:
            print(f"Gawm Error: {line}")
            return None
        if not self._wait_for_ack():
            return None
        return x

    def get_values_at(self, x):
        """Interpolate all signals at X position. Returns dict {name: value}."""
        self.send_command(f"get_values_at {x}")
        f = self._get_reader()
        line = f.readline()
        if not line or not line.strip().lstrip("-").isdigit():
            print(f"Gawm Error: {line.strip()}")
            return {}
        nvars = int(line.strip())
        result = {}
        for _ in range(nvars):
            line = f.readline()
            if not line:
                print("Gawm Error: unexpected EOF reading values")
                return {}
            parts = line.split()
            if len(parts) >= 2:
                result[parts[0]] = float(parts[1])
        if not self._wait_for_ack():
            return {}
        return result

    def get_cursor_values(self, idx=0):
        """Get all signal values at a cursor position. Returns dict with 'x' and 'values'."""
        x = self.get_cursor(idx)
        if x is None:
            return None
        return {"x": x, "values": self.get_values_at(x)}

    # --- Plotting ---

    def add_waveform(self, name, x_data, y_data, table_name="python", panel=None):
        """Add a single waveform. If panel is set, auto-display it."""
        self.plot(x_data, [y_data], [name], table_name=table_name, panel=panel)

    def plot(self, x_data, y_datas, names, table_name="python", panel=None):
        """Plot multiple waveforms sharing the same X axis.

        Args:
            x_data: X axis array.
            y_datas: List of Y data arrays.
            names: List of signal names.
            table_name: Table name in gawm.
            panel: If set to an int, auto-display waveforms in that panel.
        """
        self.send_command(f"table_new {table_name}")
        self._wait_for_ack()
        vars_str = "x " + " ".join(names)
        self.send_command(f"variables {vars_str}")
        self._wait_for_ack()
        self.send_command("rowdatas")
        self._wait_for_ack()

        data_matrix = np.column_stack([x_data] + list(y_datas))

        for row in data_matrix:
            row_str = " ".join(map(str, row))
            self.sock.sendall((row_str + "\n").encode("utf-8"))
            self._wait_for_ack()

        self.send_command("enddata")
        self._wait_for_ack()

        if panel is not None:
            for name in names:
                self.add_to_panel(name, panel=panel)

    def add_to_panel(self, name, panel=0, color=None):
        """Add a variable from the current table to a panel.

        Args:
            name: Variable name.
            panel: Panel index (0-based).
            color: Optional color as '#rrggbb' or '#rrggbbaa'.
        """
        color_str = f" {color}" if color else ""
        self.send_command(f"copyvar {name} p{panel}{color_str}")
        return self._wait_for_ack()

    def plot_analysis(self, analysis, table_name="pyspice", panel=None):
        """Plot a PySpice Analysis object.

        Handles transient, AC (complex -> magnitude/phase), and DC sweep analyses.

        Args:
            analysis: PySpice Analysis object.
            table_name: Table name in gawm.
            panel: If set, auto-display waveforms in that panel.
        """
        # Detect X axis
        if hasattr(analysis, "time"):
            x_name = "time"
            x_data = np.array(analysis.time)
        elif hasattr(analysis, "frequency"):
            x_name = "frequency"
            x_data = np.array(analysis.frequency)
        elif hasattr(analysis, "sweep"):
            x_name = "sweep"
            x_data = np.array(analysis.sweep)
        else:
            x_name = "x"
            x_data = np.arange(len(next(iter(analysis.waveforms.values()))))

        y_names = list(analysis.waveforms.keys())
        y_data_list = [np.array(analysis.waveforms[name]) for name in y_names]

        # AC analysis: split complex into magnitude (dB) and phase (degrees)
        is_complex = any(np.iscomplexobj(y) for y in y_data_list)
        if is_complex:
            mag_names = [f"{name}_dB" for name in y_names]
            phase_names = [f"{name}_phase" for name in y_names]
            mag_data = [20 * np.log10(np.maximum(np.abs(y), 1e-30)) for y in y_data_list]
            phase_data = [np.angle(y, deg=True) for y in y_data_list]

            self.plot(x_data, mag_data, mag_names,
                      table_name=table_name + "_mag", panel=panel)
            self.plot(x_data, phase_data, phase_names,
                      table_name=table_name + "_phase")
            return

        # Real-valued analysis (transient, DC, etc.)
        self.plot(x_data, y_data_list, [str(n) for n in y_names],
                  table_name=table_name, panel=panel)

    # --- Simulation ---

    def simulate(self, spice_file, raw_file=None, ngspice_cmd="ngspice"):
        """Run an ngspice simulation and load the results.

        Args:
            spice_file: Path to the SPICE netlist.
            raw_file: Output raw file path (default: replaces extension with .raw).
            ngspice_cmd: ngspice executable name or path.

        Returns:
            Path to the raw file.
        """
        if raw_file is None:
            base, _ = os.path.splitext(spice_file)
            raw_file = base + ".raw"
        subprocess.run([ngspice_cmd, "-b", "-r", raw_file, spice_file], check=True)
        self.load_file(raw_file)
        return raw_file

    # --- Display control ---

    def set_panels(self, num):
        """Set the number of panels."""
        self.send_command(f"panel {num}")
        return self._wait_for_ack()

    def grid(self, on=True):
        """Toggle grid display."""
        self.send_command(f"grid {'on' if on else 'off'}")
        return self._wait_for_ack()

    def logx(self, on=True):
        """Toggle logarithmic X axis."""
        self.send_command(f"logx {'on' if on else 'off'}")
        return self._wait_for_ack()

    def color_bg(self, color):
        """Set background color (e.g., '#000000')."""
        self.send_command(f"color_bg {color}")
        return self._wait_for_ack()

    # --- File operations ---

    def load_file(self, filename, fmt=None):
        """Load a waveform file (raw, csv, etc.)."""
        cmd = f"load {filename}"
        if fmt:
            cmd += f" {fmt}"
        self.send_command(cmd)
        return self._wait_for_ack()

    def reload_all(self):
        """Reload all loaded files."""
        self.send_command("reload_all")
        return self._wait_for_ack()

    def export_img(self, filename, fmt="png"):
        """Export panel image to file."""
        self.send_command(f"export_img {filename} {fmt}")
        return self._wait_for_ack()

    def export_data(self, filename, fmt=None):
        """Export displayed data to file."""
        cmd = f"export_data {filename}"
        if fmt:
            cmd += f" {fmt}"
        self.send_command(cmd)
        return self._wait_for_ack()

    def set_table(self, name):
        """Set the current table by name."""
        self.send_command(f"table_set {name}")
        return self._wait_for_ack()

    def delete_table(self, name):
        """Delete a table by name."""
        self.send_command(f"tabledel {name}")
        return self._wait_for_ack()

    # --- Xschem integration ---

    def xschem_annotate(self, cursor_idx=0):
        """Send signal values at cursor to xschem for backannotation."""
        self.send_command(f"xschem_annotate {cursor_idx}")
        return self._wait_for_ack()

    def xschem_clear_annotations(self):
        """Clear backannotation data in xschem."""
        self.send_command("xschem_clear_annotations")
        return self._wait_for_ack()

    # --- Lifecycle ---

    def close(self):
        if self._reader:
            self._reader.close()
            self._reader = None
            self._reader_sock = None
        if self.sock:
            self.sock.close()
        if self.proc:
            self.proc.terminate()

if __name__ == "__main__":
    g = Gawm()
    if g.connect():
        print(f"Connected to gawm on port {g.port}")
    else:
        print("Could not connect to gawm. Is it running with -p?")
