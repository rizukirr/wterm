#!/bin/bash

##
# install.sh - System installation script for wterm v2
#
# This script builds and installs wterm with proper dependency management
# for Arch Linux systems.
##

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Track installation state
INSTALLED_FILES=()

# Function to print colored output
print_status() {
    case $1 in
        "OK") echo -e "${GREEN}✅ $2${NC}" ;;
        "ERROR") echo -e "${RED}❌ $2${NC}" ;;
        "INFO") echo -e "${BLUE}ℹ️  $2${NC}" ;;
        "WARN") echo -e "${YELLOW}⚠️  $2${NC}" ;;
    esac
}

# Function to handle interruption
cleanup_on_interrupt() {
    echo
    print_status "WARN" "Installation interrupted!"
    echo

    if [[ ${#INSTALLED_FILES[@]} -gt 0 ]]; then
        print_status "WARN" "Files that were installed:"
        for file in "${INSTALLED_FILES[@]}"; do
            echo "  • $file"
        done
        echo
        print_status "INFO" "To remove manually:"
        for file in "${INSTALLED_FILES[@]}"; do
            echo "  sudo rm -f $file"
        done
    else
        print_status "INFO" "No files were installed."
    fi

    exit 130
}

# Set up trap for interruption signals
trap cleanup_on_interrupt SIGINT SIGTERM

echo "=== wterm v2 Installation Script ==="
echo

# Check if running as root
if [[ $EUID -eq 0 ]]; then
   print_status "WARN" "Running as root - this is not recommended"
   print_status "INFO" "Consider running as regular user (will prompt for sudo when needed)"
   echo
   read -p "Continue anyway? [y/N]: " -n 1 -r
   echo
   [[ ! $REPLY =~ ^[Yy]$ ]] && exit 1
fi

# Check for required build tools
check_build_dependencies() {
    local missing_deps=()

    # Check for CMake
    if ! command -v cmake &> /dev/null; then
        missing_deps+=("cmake")
    fi

    # Check for make or ninja
    if ! command -v make &> /dev/null && ! command -v ninja &> /dev/null; then
        missing_deps+=("make")
    fi

    # Check for gcc or clang
    if ! command -v gcc &> /dev/null && ! command -v clang &> /dev/null; then
        missing_deps+=("gcc")
    fi

    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        print_status "WARN" "Missing build dependencies: ${missing_deps[*]}"
        echo
        print_status "INFO" "Install with: sudo pacman -S base-devel cmake"
        echo
        read -p "Do you want to auto-install build dependencies? [y/N]: " -r
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            print_status "INFO" "Installing build dependencies..."
            sudo pacman -S --needed --noconfirm base-devel cmake
            print_status "OK" "Build dependencies installed"
        else
            print_status "ERROR" "Build dependencies required for installation"
            exit 1
        fi
    fi
}

# Check for network manager conflicts
check_network_manager_conflicts() {
    print_status "INFO" "Checking current network manager configuration..."

    # Check if NetworkManager is already active
    if systemctl is-active --quiet NetworkManager; then
        print_status "OK" "NetworkManager is already active - no conflicts"
        return 0  # All good, continue installation
    fi

    # NetworkManager is not active - check for known conflicting managers
    print_status "INFO" "NetworkManager is not active, checking for conflicts..."

    local detected_manager=""
    local known_managers=("systemd-networkd" "wicd" "connman" "iwd" "netctl" "dhcpcd")

    # Detect which known manager is active
    for manager in "${known_managers[@]}"; do
        if systemctl is-active --quiet "$manager"; then
            detected_manager="$manager"
            break
        fi
    done

    # Handle detected manager
    if [[ -n "$detected_manager" ]]; then
        echo
        print_status "WARN" "Detected active network manager: $detected_manager"
        print_status "INFO" "wterm requires NetworkManager to function"
        echo
        print_status "INFO" "Actions needed:"
        echo "  1. Disable $detected_manager"
        echo "  2. Install and enable NetworkManager"
        echo "  3. Install fzf for interactive network selection"
        echo
        print_status "WARN" "This will change your system's network management!"
        echo
        read -p "Do you want to proceed? [y/N]: " -r
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            export DETECTED_MANAGER="$detected_manager"  # Pass to switch function
            return 0  # Proceed with switching
        else
            print_status "ERROR" "Cannot install wterm without NetworkManager"
            print_status "INFO" "Installation cancelled by user"
            exit 1
        fi
    else
        # Unknown or no network manager detected
        echo
        print_status "WARN" "No known network manager detected, but NetworkManager is not active"
        print_status "INFO" "You may have an unknown network manager or custom network setup"
        echo
        print_status "ERROR" "Please manually disable your current network manager and enable NetworkManager:"
        echo "  1. Disable your current network manager"
        echo "  2. Install NetworkManager: sudo pacman -S networkmanager"
        echo "  3. Enable NetworkManager: sudo systemctl enable --now NetworkManager"
        echo "  4. Re-run this install script"
        echo
        exit 1
    fi
}

# Switch to NetworkManager safely
switch_to_networkmanager() {
    local manager_to_disable="${DETECTED_MANAGER}"

    if [[ -z "$manager_to_disable" ]]; then
        print_status "ERROR" "No network manager specified to disable"
        return 1
    fi

    print_status "INFO" "Switching from $manager_to_disable to NetworkManager..."

    # Create state directory for tracking changes
    sudo mkdir -p /etc/wterm

    # Store current network manager state for uninstall
    local state_file="/etc/wterm/previous_state.txt"
    sudo rm -f "$state_file"

    echo "previous_netmgr=$manager_to_disable" | sudo tee -a "$state_file" >/dev/null

    # For systemd-networkd, also track systemd-resolved
    if [[ "$manager_to_disable" == "systemd-networkd" ]]; then
        echo "systemd_resolved_was_active=$(systemctl is-active --quiet systemd-resolved && echo 'true' || echo 'false')" | sudo tee -a "$state_file" >/dev/null
    fi

    # Store installation timestamp
    echo "install_date=$(date -Iseconds)" | sudo tee -a "$state_file" >/dev/null
    echo "installed_by_wterm=true" | sudo tee -a "$state_file" >/dev/null

    # Disable the detected network manager
    print_status "INFO" "Disabling $manager_to_disable..."
    if ! sudo systemctl disable --now "$manager_to_disable"; then
        print_status "ERROR" "Failed to disable $manager_to_disable"
        return 1
    fi

    # Special handling for systemd-networkd (also disable systemd-resolved)
    if [[ "$manager_to_disable" == "systemd-networkd" ]] && systemctl is-active --quiet systemd-resolved; then
        print_status "INFO" "Disabling systemd-resolved..."
        sudo systemctl disable --now systemd-resolved
    fi

    # Install NetworkManager, iw, and fzf
    print_status "INFO" "Installing NetworkManager, iw, and fzf..."
    if ! sudo pacman -S --needed --noconfirm networkmanager iw fzf; then
        print_status "ERROR" "Failed to install NetworkManager, iw, and fzf"
        return 1
    fi

    # Enable and start NetworkManager
    print_status "INFO" "Enabling NetworkManager..."
    if ! sudo systemctl enable --now NetworkManager; then
        print_status "ERROR" "Failed to enable NetworkManager"
        return 1
    fi

    # Wait a moment for NetworkManager to initialize
    sleep 2

    # Verify NetworkManager is working
    if systemctl is-active --quiet NetworkManager; then
        print_status "OK" "NetworkManager is now active"

        # Test if nmcli is working
        if nmcli device status >/dev/null 2>&1; then
            print_status "OK" "NetworkManager is functioning correctly"
        else
            print_status "WARN" "NetworkManager started but nmcli test failed"
        fi
    else
        print_status "ERROR" "Failed to start NetworkManager"
        return 1
    fi

    return 0
}

# Check final network manager availability (after conflict resolution)
check_network_manager_availability() {
    local missing_deps=()

    # Check if NetworkManager is working after conflict resolution
    if ! command -v nmcli &> /dev/null || ! nmcli device status >/dev/null 2>&1; then
        print_status "ERROR" "NetworkManager is not working properly"
        print_status "INFO" "This might be due to service conflicts or installation issues"
        return 1
    fi

    # Check for iw (kernel-level WiFi checks)
    if ! command -v iw &> /dev/null; then
        missing_deps+=("iw")
    fi

    # Check for fzf (interactive network selection)
    if ! command -v fzf &> /dev/null; then
        missing_deps+=("fzf")
    fi

    print_status "OK" "NetworkManager is working correctly"

    # Handle missing dependencies
    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        print_status "INFO" "Missing optional dependencies: ${missing_deps[*]}"
        if [[ " ${missing_deps[*]} " =~ " iw " ]]; then
            print_status "INFO" "iw enables better zombie connection detection"
        fi
        if [[ " ${missing_deps[*]} " =~ " fzf " ]]; then
            print_status "INFO" "fzf enables interactive network selection"
        fi
        echo
        read -p "Do you want to install missing dependencies? [Y/n]: " -r
        if [[ ! $REPLY =~ ^[Nn]$ ]]; then
            print_status "INFO" "Installing ${missing_deps[*]}..."
            sudo pacman -S --needed --noconfirm "${missing_deps[@]}"
            print_status "OK" "Dependencies installed"
        else
            print_status "INFO" "Skipping optional dependencies"
        fi
    else
        print_status "OK" "All optional dependencies available"
    fi

    return 0
}

# Build the project
build_project() {
    print_status "INFO" "Building wterm v2..."

    cd "$PROJECT_ROOT"

    # Use our build script for consistency
    if [[ -x "scripts/build.sh" ]]; then
        ./scripts/build.sh release
    else
        # Fallback to direct CMake
        mkdir -p build
        cd build
        cmake -DCMAKE_BUILD_TYPE=Release ..
        cmake --build . --parallel $(nproc)
    fi

    # Verify build succeeded
    if [[ ! -f "$PROJECT_ROOT/build/bin/wterm" ]]; then
        print_status "ERROR" "Build failed - executable not found"
        exit 1
    fi

    print_status "OK" "Build completed successfully"
}

# Install the executable
install_executable() {
    print_status "INFO" "Installing wterm executable..."

    local src_file="$PROJECT_ROOT/build/bin/wterm"
    local dest_file="/usr/local/bin/wterm"

    # Check if already installed
    if [[ -f "$dest_file" ]]; then
        print_status "WARN" "wterm is already installed at $dest_file"
        read -p "Overwrite existing installation? [y/N]: " -n 1 -r
        echo
        [[ ! $REPLY =~ ^[Yy]$ ]] && exit 1
    fi

    # Install with sudo
    sudo cp "$src_file" "$dest_file"
    sudo chmod +x "$dest_file"
    INSTALLED_FILES+=("$dest_file")

    print_status "OK" "Executable installed to $dest_file"
}

# Install hotspot management script
install_hotspot_script() {
    print_status "INFO" "Installing hotspot management script..."

    local src_script="$PROJECT_ROOT/scripts/hotspot_nm.sh"
    local dest_script="/usr/local/bin/hotspot_nm.sh"

    # Check if script exists in source
    if [[ ! -f "$src_script" ]]; then
        print_status "WARN" "Hotspot script not found at $src_script"
        print_status "INFO" "Hotspot functionality may not work"
        return 0
    fi

    # Install with sudo
    sudo cp "$src_script" "$dest_script"
    sudo chmod +x "$dest_script"
    INSTALLED_FILES+=("$dest_script")

    print_status "OK" "Hotspot script installed to $dest_script"
}

# Run tests before installation
run_tests() {
    print_status "INFO" "Running tests to verify installation..."

    cd "$PROJECT_ROOT/build"
    if ctest --output-on-failure; then
        print_status "OK" "All tests passed"
    else
        print_status "WARN" "Some tests failed, but installation will continue"
        read -p "Continue with installation? [y/N]: " -n 1 -r
        echo
        [[ ! $REPLY =~ ^[Yy]$ ]] && exit 1
    fi
}

# Verify installation
verify_installation() {
    print_status "INFO" "Verifying installation..."

    if command -v wterm &> /dev/null; then
        local version_output=$(wterm --version 2>/dev/null || echo "version check failed")
        print_status "OK" "wterm is installed and accessible"
        print_status "INFO" "Version: $version_output"
    else
        print_status "ERROR" "Installation verification failed"
        exit 1
    fi
}

# Show post-installation information
show_post_install_info() {
    echo
    print_status "OK" "wterm v2 installation complete!"
    echo
    print_status "INFO" "Usage examples:"
    echo "  wterm                # List available WiFi networks"
    echo "  wterm --rescan       # Rescan and list networks"
    echo "  wterm --help         # Show help information"
    echo
    print_status "INFO" "The original POCO F4 network display issue has been fixed!"
    echo
    print_status "INFO" "To uninstall:"
    echo "  sudo rm /usr/local/bin/wterm"
    echo
}

# Main installation process
main() {
    print_status "INFO" "Starting wterm v2 installation..."
    echo

    check_build_dependencies
    echo

    # Check for network manager conflicts first
    check_network_manager_conflicts

    # If DETECTED_MANAGER is set, user agreed to switch
    if [[ -n "${DETECTED_MANAGER:-}" ]]; then
        switch_to_networkmanager
        if [[ $? -ne 0 ]]; then
            print_status "ERROR" "Failed to switch to NetworkManager"
            exit 1
        fi
        echo
    fi

    # Verify NetworkManager is working
    check_network_manager_availability
    echo

    build_project
    echo

    run_tests
    echo

    install_executable
    echo

    install_hotspot_script
    echo

    verify_installation
    echo

    show_post_install_info
}

# Run main installation
main "$@"