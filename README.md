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
Gawm now features a powerful Python integration layer.
- **Python Shell**: Launch an interactive Python shell directly from **Tools > Python Shell**.
- **`gawm` Python Module**: A built-in bridge that connects the shell to the running viewer.
  - `g.get_data("V(name)")`: Pull raw waveform data into NumPy arrays for processing.
  - `g.get_tables()`: List all loaded data files/tables.
  - `g.plot(...)`: Send processed waveforms (e.g. sums or differences) back to Gawm for visualization.
  - `g.plot_analysis(analysis)`: Directly plot PySpice `Analysis` objects.
- **Configurable Terminal**: The shell opens in your preferred terminal emulator (set via `up_termCmd` in `gawrc`).

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
