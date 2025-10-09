# wterm TUI Module

## Building and Testing

### Build TUI Test
```bash
cd build
cmake --build . --target tui_test
```

### Run TUI Test
**Note**: Must be run in a real terminal (TTY), not via pipes or redirects.

```bash
./bin/tui_test
```

Press any key to exit the test.

## Development Phases

- [x] Phase 1: termbox2 integration and basic test
- [x] Phase 2: 3-row panel layout with borders
- [x] Phase 3: Render dummy network data with scrolling
- [x] Phase 4: Keyboard navigation (j/k for movement)
- [x] Phase 5: Help modal and status line
- [x] Phase 6: Real data integration

## ✅ ALL PHASES COMPLETE!

## Phase 3 & 4 Features (Completed)

✅ List rendering with 15 dummy networks
✅ Signal strength bars (`[████]` visual indicator)
✅ Scrolling support (auto-scroll when selection moves)
✅ Scroll indicators (cyan bar on right edge)
✅ j/k vim-style navigation
✅ Arrow key support (↑/↓)
✅ g/G for jump to top/bottom
✅ Selection highlighting (inverse colors)
✅ Active panel tracking
✅ Status line showing current selection
✅ Saved network indicator (green bullet •)

### Test Controls
- `j` / `↓` - Move down in current panel
- `k` / `↑` - Move up in current panel
- `g` - Jump to top of list
- `G` - Jump to bottom of list
- `Tab` / `h` / `l` - Switch between panels
- `q` / `Esc` - Quit

### Data Display Features
- Signal bars: `[████]` (0-4 bars based on signal strength)
- Signal percentage display
- Security type (WPA2, WPA3, Open)
- Saved indicator: green bullet (•) for known networks
- Selection arrow (→) for active item

## Phase 5 Features (Completed)

✅ Help modal overlay (press `?`)
✅ Centered modal with blue background
✅ Shadow effect for depth
✅ Organized sections (Navigation, Panel Switching, Actions)
✅ Key closes modal
✅ Status line with current selection info

### Help Modal
Press `?` to show/hide help overlay with all keyboard shortcuts.

## Architecture

### 3-Row Panel Layout (lazygit-style)
```
┌─ Saved Networks ──────────┐
│ • Network1                │
└───────────────────────────┘
┌─ Available Networks ──────┐
│ → Network1  [████] WPA2   │
│   Network2  [███ ] Open   │
└───────────────────────────┘
┌─ Keybindings ─────────────┐
│ j/↓:Down Tab:Next Enter   │
└───────────────────────────┘
```
