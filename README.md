![Version](https://img.shields.io/badge/version-3.0.0-blue)
![License](https://img.shields.io/badge/license-Open%20Source-green)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey)

# wterm v2 - WiFi TUI Network Manager

A lightweight terminal base WiFi management tool written in C with modern CMake build system. This is a complete rewrite of the original shell-based wterm in v1.0.0, offering improved performance, memory safety, and maintainability.

## Features

- **Fast Network Scanning**: Native C implementation for quick WiFi network discovery
- **Interactive Rescan**: Live network rescanning with smooth loading animations
- **fzf Integration**: Modern terminal UI with fuzzy search and network selection
- **WiFi Hotspot Management**: Create and manage WiFi hotspots (Access Points) using NetworkManager
- **Memory Safe**: Comprehensive bounds checking and safe string operations
- **Well Tested**: Complete unit and integration test suite
- **Modern Build System**: CMake with professional project structure
- **Intuitive Interface**: Easy navigation with arrow keys, search, and selection
- **Modular Design**: Clean separation of concerns for easy maintenance

## Quick Start

### Option 1: Pre-built Binary (Fastest)

Download the latest release binary from [GitHub Releases](https://github.com/your-repo/wterm/releases):

```bash
# Download the binary (replace VERSION with actual version)
wget https://github.com/your-repo/wterm/releases/download/vVERSION/wterm-x86_64-linux

# Install runtime dependencies based on your distro
# Arch Linux
sudo pacman -S networkmanager fzf

# Ubuntu/Debian
sudo apt update && sudo apt install network-manager fzf

# Fedora
sudo dnf install NetworkManager fzf

# openSUSE
sudo zypper install NetworkManager fzf

# Make executable and install
chmod +x wterm-x86_64-linux
sudo mv wterm-x86_64-linux /usr/local/bin/wterm

# Verify installation
wterm --version
```

**Requirements:**
- Linux x86_64 with glibc 2.31+ (most modern distros from 2020+)
- NetworkManager and fzf installed

### Option 2: Install Script from Source

```bash
# Clone and install in one step
git clone <repository-url> wterm
cd wterm
sudo ./scripts/install.sh
```

The install script automatically handles dependencies on Arch Linux.

### Option 3: Manual Build from Source

```bash
# Build release version
./scripts/build.sh release

# Run the executable
./build/bin/wterm
```

## Prerequisites

### Build Dependencies

- **CMake** (>= 3.12)
- **GCC** or **Clang** compiler
- **Make** or **Ninja** build system

### Runtime Dependencies

- **NetworkManager** (`nmcli` command)
- **fzf** (for interactive network selection)
- **Linux** system with glibc 2.31+ (works on most modern distros: Arch, Ubuntu 20.04+, Fedora, Debian, openSUSE)

### Auto-Installation

The install script can automatically install missing dependencies on Arch Linux:

```bash
sudo pacman -S base-devel cmake networkmanager fzf
```

## Build System

### Quick Build Commands

```bash
# Show all available options
./scripts/build.sh help

# Clean build with tests
./scripts/build.sh all

# Debug build with sanitizers
./scripts/build.sh debug --sanitize

# Release build
./scripts/build.sh release

# Run tests only
./scripts/build.sh test
```

### CMake Options

```bash
# Configure with options
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTS=ON \
      -DENABLE_SANITIZERS=OFF \
      ..

# Build
cmake --build . --parallel $(nproc)

# Run tests
ctest --output-on-failure
```

## Usage

### Interactive Mode (Default)

```bash
# Launch interactive fzf interface
wterm
```

**Interface Controls:**

- `↑↓` - Navigate networks
- `Enter` - Connect to selected network
- `🔄 Rescan` - Refresh network list with loading animation
- `Type` - Search/filter networks
- `q/Esc` - Quit

### Command Line Options

```bash
# Show help
wterm --help

# Show version information
wterm --version
```

### Network Connection

When you select a network:

- **Open networks**: Connect immediately
- **Secured networks**: Prompt for password securely
- **Connection status**: Real-time feedback with success/error messages

## Hotspot Management

wterm includes a NetworkManager-based hotspot management tool for creating and managing WiFi Access Points.

### Quick Hotspot Commands

```bash
# Create and start a secured hotspot
sudo ./scripts/hotspot_nm.sh create MyHotspot mypassword123

# Create an open hotspot
sudo ./scripts/hotspot_nm.sh create PublicWiFi --open

# Create with custom settings
sudo ./scripts/hotspot_nm.sh create MyHotspot password123 --band a --interface wlan0

# Manage hotspots
sudo ./scripts/hotspot_nm.sh start MyHotspot
sudo ./scripts/hotspot_nm.sh stop MyHotspot
sudo ./scripts/hotspot_nm.sh restart MyHotspot
sudo ./scripts/hotspot_nm.sh delete MyHotspot

# List and check status
sudo ./scripts/hotspot_nm.sh list
sudo ./scripts/hotspot_nm.sh status [MyHotspot]
```

### Hotspot Options

- `--interface <iface>` - WiFi interface (default: wlan0)
- `--band <bg|a>` - Frequency band: bg=2.4GHz, a=5GHz (default: bg)
- `--no-start` - Create configuration without starting
- `--open` - Create open hotspot (no password)
- `--gateway <ip/prefix>` - Gateway IP (default: 192.168.12.1/24)

### Important Limitations

**WiFi-to-WiFi Sharing Not Supported**

Due to hardware limitations, wterm cannot share a WiFi connection through a WiFi hotspot on the same adapter:

- **Single Radio Limitation**: Most WiFi adapters have a single radio that cannot operate in both client mode (connected to WiFi) and AP mode (hotspot) simultaneously
- **Current Behavior**: Starting a hotspot will automatically disconnect any active WiFi connection on the same interface
- **Supported Configuration**: Ethernet-to-WiFi sharing (wired internet connection shared via WiFi hotspot)

**To enable WiFi-to-WiFi sharing, you would need:**
- Two separate WiFi adapters (one for client mode, one for AP mode)
- Manual configuration to bridge traffic between the two interfaces

**Other Notes:**
- Requires root/sudo privileges
- Hotspot configurations are persistent across reboots
- NetworkManager automatically handles WiFi disconnection when starting hotspot

## Testing

### Run All Tests

```bash
./scripts/build.sh test
```

### Individual Test Suites

```bash
# String utilities tests
./build/tests/test_string_utils

# Network scanner tests
./build/tests/test_network_scanner

# Integration tests (includes original bug verification)
./build/tests/test_integration
```

### Test Coverage

The integration tests specifically verify that the original "POCO F4" bug is fixed and that all network types from the original problematic output now parse correctly.

## Development

### Code Style

- **C99 standard** compliance
- **Consistent naming**: snake_case for functions, ALL_CAPS for constants
- **Memory safety**: All string operations use bounds checking
- **Error handling**: Proper return codes and validation
- **Documentation**: Doxygen-style comments for public APIs

### Adding Features

1. Add new functions to appropriate modules (`src/core/` or `src/utils/`)
2. Update corresponding header files
3. Add unit tests in `tests/`
4. Update CMakeLists.txt if needed
5. Run `./scripts/build.sh all` to verify

### Memory Safety

This codebase prioritizes memory safety:

- No `malloc/free` - uses stack allocation
- All `strncpy` operations include bounds checking
- Input validation on all public functions
- Sanitizer support for development builds

## Installation

### System-wide Installation

```bash
# Using install script (recommended)
sudo ./scripts/install.sh

# Manual installation after build
sudo cmake --install build/
```

### Package Creation

```bash
# Create distributable package
./scripts/build.sh package

# Packages will be in build/ directory
ls build/*.tar.gz build/*.deb
```

## Comparison with v1

| Feature               | v1 (Shell)                  | v2 (C)                      |
| --------------------- | --------------------------- | --------------------------- |
| **Performance**       | Slower (multiple processes) | Fast (single binary)        |
| **User Interface**    | Basic text output           | Interactive fzf interface   |
| **Network Selection** | Manual SSID typing          | Fuzzy search + selection    |
| **Rescan**            | Manual restart required     | Live rescan with animations |
| **Memory Safety**     | Shell-safe                  | Explicit bounds checking    |
| **Dependencies**      | `iwd`, `fzf`, `bash`        | `NetworkManager`, `fzf`     |
| **Testing**           | Manual                      | Comprehensive test suite    |
| **Build System**      | None                        | Professional CMake          |
| **Bug Handling**      | Open network bug present    | Open network bug fixed      |
| **Maintainability**   | Script-based                | Modular C architecture      |

## Contributing

1. Fork the repository
2. Create a feature branch
3. Add tests for new functionality
4. Ensure all tests pass: `./scripts/build.sh all`
5. Submit a pull request

## License

This project is open source. Feel free to use, modify, and distribute.

## Acknowledgments

- Original wterm shell implementation for inspiration
- NetworkManager project for the reliable `nmcli` interface
- The bug report that led to this complete rewrite and improvement
