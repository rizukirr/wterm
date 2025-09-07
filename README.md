# WiFi Terminal (wterm)

A lightweight, interactive WiFi management tool for Linux systems using `iwd` and `fzf`. Manage WiFi connections directly from your terminal with an intuitive fuzzy-finder interface.

![wterm Screenshot](assets/wterm.png)

## Features

- 🔍 Interactive WiFi network selection with `fzf`
- 🔄 Real-time network scanning
- 🔐 Automatic password prompting for secured networks
- ✅ Connection status feedback
- 🔌 Easy disconnect from current networks
- 🚀 Auto-detects WiFi interface
- 📦 Simple installation and uninstallation

## Prerequisites

- **Arch Linux** (currently supported - other distributions coming soon!)
- `iwd` (Intel's WiFi daemon)
- `fzf` (fuzzy finder)
- Root access for installation

> **Note**: This tool is currently designed for Arch Linux and uses `pacman` for dependency management. Support for other Linux distributions is planned for future releases.

## Installation

1. Clone or download this repository
2. Run the installer with root privileges:

```bash
sudo ./install.sh
```

**Why root privileges are required:**
- Installing the executable to `/usr/local/bin/` (system-wide access)
- Installing system packages (`iwd`, `fzf`) via `pacman`
- Enabling and starting the `iwd` systemd service
- Setting executable permissions on the installed binary

The installer will:
- Install the `wterm` command to `/usr/local/bin/`
- Check and optionally install dependencies (`iwd`, `fzf`)
- Enable and start the `iwd` service if needed

## Usage

After installation, simply run:

```bash
wterm
```

### Interface

- Use arrow keys to navigate through available WiFi networks
- Press `Enter` to connect to a selected network
- Select "🔄 Rescan" to refresh the network list
- Press `Esc` to exit
- If already connected to a network, you'll be prompted to disconnect

## Dependencies

- **iwd**: Modern WiFi daemon for Linux
- **fzf**: Command-line fuzzy finder

Both dependencies are automatically installed if you choose the auto-install option during installation.

## Uninstallation

To remove WiFi Terminal:

```bash
sudo ./uninstall.sh
```

This will:
- Remove the `wterm` command
- Optionally remove `fzf` if no longer needed
- Keep `iwd` installed (as it may be used by other applications)

## How It Works

1. **Network Detection**: Auto-detects your WiFi interface (falls back to `wlan0`)
2. **Scanning**: Uses `iwctl` to scan for available networks
3. **Selection**: Presents networks in a clean `fzf` interface
4. **Connection**: Handles connection with automatic password prompting
5. **Status**: Provides clear feedback on connection success/failure

## Troubleshooting

### No WiFi interface detected
Ensure your WiFi adapter is recognized and `iwd` service is running:
```bash
sudo systemctl status iwd
iwctl device list
```

### Connection failures
- Verify the network password is correct
- Check if the network requires special authentication
- Ensure `iwd` has proper permissions

### Interface issues
The script auto-detects WiFi interfaces, but you can manually check available interfaces:
```bash
iwctl device list
```

## Contributing

Feel free to submit issues, fork the repository, and create pull requests for any improvements.

## License

This project is open source. Feel free to use, modify, and distribute as needed.

## Author

Created for easy WiFi management in terminal environments.