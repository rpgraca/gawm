# Gawm - Gtk Analog Waveform viewer (Modernized Fork)

**Gawm** is a modernized fork of [Gaw](http://www.rvq.fr/linux/gaw.php), a tool for viewing analog data, such as the output of Spice simulations. This fork, maintained by **rpgraca**, aims to provide improved visualization and modern integration.

### Compiling from Source
```bash
autoreconf -fi
./configure
make
sudo make install
```

## Usage

Run the application from the command line:
```bash
gawm [OPTIONS]... [FILE]...
```

## Changes from original Gaw

### Signal Lists (Global & Per-Pane)
- The global signal list is embedded as a resizable sidebar (left side of the main window).
- **Independent Pane Resizing**: Each waveform panel now has its own draggable divider, allowing you to resize the signal list/measurements area independently for each pane.
- **Improved Space Efficiency**: Signal labels now use **ellipsizing** (showing `...` at the end) if they don't fit the available space.
- **Tooltips**: Hovering over any signal label (in the main sidebar or individual panes) or a file name shows the full name in a tooltip.
- **Hidden Wave Indication**: Hidden waveforms are visually indicated in the list by **strikethrough text** and reduced opacity.

### Mouse Interactions & Hit Detection
- **Improved Selection**: Waveform selection logic is now more robust, accurately handling steep or instantaneous transitions by checking a circular 10-pixel threshold.
- **Double-Click Shortcuts**:
  - Fast double-left-click on a **waveform drawing** hides it instantly.
  - Fast double-left-click on a **pane menu entry** toggles its hidden state.

#### Mouse Buttons (Updated)
| Action | Effect |
|--------|--------|
| Left click on wave trace | Select/highlight wave (toggle active state on release) |
| Left click on empty area | Place/drag cursor |
| Left click near cursor line | Grab and drag that cursor |
| Left drag from wave | Drag-and-drop wave to another panel |
| Shift + left drag | Draw zoom rectangle (zoom X+Y to selection) |
| Middle drag | Pan X axis |
| Right click on wave trace | Select wave and show wave-specific context menu |
| Right click on empty area | Show pane-level context menu |

#### Cursors
- Click near an existing cursor line to grab and reposition it.
- **Smart Default**: The cursor moved by a click in an empty area is always the last one that was dragged.
- Cursor annotations drawn on each panel:
  - X-value box at top of each cursor line.
  - Y-value labels at cursor/wave intersection points (colored dot + value box).
  - Delta X box between cursors when both are visible.
  - **Per-signal Delta Y**: Shows the vertical difference for each wave between the two cursor positions.

### Python Integration & PySpice Bridge
Gawm features a Python integration layer accessible from **Tools > Python Shell**.
The `gawm` module connects to the running viewer and provides:

#### Waveform Data
| Method | Description |
|--------|-------------|
| `g.get_data(name)` | Get waveform data by variable name (e.g., `"V(out)"`). Returns `{"x": array, "y": array}`. |
| `g.get_tables()` | List all loaded table names. Returns list of strings. |
| `g.get_cursor(idx=0)` | Get cursor X position (idx: 0 or 1). Returns float or None. |
| `g.get_values_at(x)` | Interpolate all signals at X value. Returns `{name: value}` dict. |
| `g.get_cursor_values(idx=0)` | Get all signal values at cursor position. Returns `{"x": float, "values": dict}`. |

#### Plotting
| Method | Description |
|--------|-------------|
| `g.plot(x_data, y_datas, names, table_name="python", panel=None)` | Plot waveforms. `y_datas`: list of arrays, `names`: list of strings. Set `panel` to an int to auto-display. |
| `g.add_waveform(name, x_data, y_data, table_name="python", panel=None)` | Plot a single waveform. |
| `g.add_to_panel(name, panel=0, color=None)` | Display a variable in a panel. `color`: optional `"#rrggbb"` or `"#rrggbbaa"`. |
| `g.plot_analysis(analysis, table_name="pyspice", panel=None)` | Plot a PySpice `Analysis` object. Supports transient, AC, and DC sweep. AC data is split into magnitude (dB) and phase tables. |

#### Simulation & File Operations
| Method | Description |
|--------|-------------|
| `g.simulate(spice_file, raw_file=None, ngspice_cmd="ngspice")` | Run ngspice in batch mode and load results. Returns raw file path. |
| `g.load_file(filename, fmt=None)` | Load a waveform file. `fmt`: optional format string (e.g., `"raw"`, `"csv"`). |
| `g.reload_all()` | Reload all loaded files from disk. |
| `g.export_img(filename, fmt="png")` | Export panel image. |
| `g.export_data(filename, fmt=None)` | Export displayed waveform data. |
| `g.set_table(name)` | Set the active table by name. |
| `g.delete_table(name)` | Delete a table by name. |

#### Display Control
| Method | Description |
|--------|-------------|
| `g.set_panels(num)` | Set the number of waveform panels. |
| `g.grid(on=True)` | Enable/disable grid. |
| `g.logx(on=True)` | Enable/disable logarithmic X axis. |
| `g.color_bg(color)` | Set background color (e.g., `"#000000"`). |

#### Xschem from Python
| Method | Description |
|--------|-------------|
| `g.xschem_annotate(cursor_idx=0)` | Send signal values at cursor to xschem for backannotation. |
| `g.xschem_clear_annotations()` | Clear backannotation data in xschem. |

The shell opens in your preferred terminal emulator (set via `up_termCmd` in `gawrc`).

### Xschem Integration
Gawm can interact with **Xschem** for cross-probing.
- **Bidirectional Highlight Sync**: Toggling a waveform highlight in Gawm highlights/unhighlights the corresponding net in Xschem. Manual highlights set in Xschem are preserved.
- **Cursor Annotations**: Right-click a panel to send all signal values at a cursor position to Xschem for backannotation display on the schematic.
- Highlight sync can be toggled from **Preferences > Xschem highlight sync**.
- **Configuration** in `gawrc`:
  - `up_xschemHost`: Hostname where Xschem is running (default: `localhost`).
  - `up_xschemPort`: TCP port for `xschem_listen_port` (default: `2021`).
  - `up_xschemHighlight`: Enable highlight sync (default: `1`).

## License

Gawm is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

## Origin

This project is a fork of the original **Gaw** by Hervé Quillévéré. We thank the original authors and contributors for their foundational work in the open-source EDA community.

---
**Project Origin**: [https://github.com/rpgraca/gawm](https://github.com/rpgraca/gawm)
