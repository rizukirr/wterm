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
    local active_managers=()
    local conflicts_found=false

    print_status "INFO" "Checking current network manager configuration..."

    # Detect active network managers
    if systemctl is-active --quiet systemd-networkd; then
        active_managers+=("systemd-networkd")
        conflicts_found=true
    fi

    if systemctl is-active --quiet NetworkManager; then
        active_managers+=("NetworkManager")
    fi

    if systemctl is-active --quiet wicd; then
        active_managers+=("wicd")
        conflicts_found=true
    fi

    if systemctl is-active --quiet connman; then
        active_managers+=("connman")
        conflicts_found=true
    fi

    # Show current state
    if [[ ${#active_managers[@]} -gt 0 ]]; then
        print_status "INFO" "Active network managers: ${active_managers[*]}"
    fi

    # Handle systemd-networkd conflict
    if [[ $conflicts_found == true ]]; then
        echo
        print_status "WARN" "Network manager conflict detected!"
        print_status "INFO" "wterm requires NetworkManager as the main network daemon"

        if systemctl is-active --quiet systemd-networkd; then
            print_status "INFO" "systemd-networkd is currently managing your network"
            print_status "INFO" "This conflicts with NetworkManager"
        fi

        echo
        print_status "INFO" "To use wterm, we need to:"
        echo "  1. Disable conflicting network managers (systemd-networkd, etc.)"
        echo "  2. Install and enable NetworkManager"
        echo "  3. Install fzf for interactive network selection"
        echo
        print_status "WARN" "This will change your system's network management!"
        print_status "INFO" "You can restore the previous setup using uninstall.sh later"
        echo
        read -p "Do you want to proceed with switching to NetworkManager? [y/N]: " -r
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            return 0  # Proceed with changes
        else
            print_status "ERROR" "Cannot install wterm without NetworkManager"
            print_status "INFO" "Installation cancelled by user"
            exit 1
        fi
    elif systemctl is-active --quiet NetworkManager; then
        print_status "OK" "NetworkManager is already active - no conflicts detected"
        return 0
    else
        print_status "INFO" "No active network managers detected - will install NetworkManager"
        return 0
    fi
}

# Switch to NetworkManager safely
switch_to_networkmanager() {
    print_status "INFO" "Switching to NetworkManager as primary network manager..."

    # Create state directory for tracking changes
    sudo mkdir -p /etc/wterm

    # Store current network manager state for uninstall
    local state_file="/etc/wterm/previous_state.txt"
    sudo rm -f "$state_file"

    if systemctl is-active --quiet systemd-networkd; then
        echo "previous_netmgr=systemd-networkd" | sudo tee -a "$state_file" >/dev/null
        echo "systemd_resolved_was_active=$(systemctl is-active --quiet systemd-resolved && echo 'true' || echo 'false')" | sudo tee -a "$state_file" >/dev/null
    fi

    # Store installation timestamp
    echo "install_date=$(date -Iseconds)" | sudo tee -a "$state_file" >/dev/null
    echo "installed_by_wterm=true" | sudo tee -a "$state_file" >/dev/null

    # Disable conflicting services
    if systemctl is-active --quiet systemd-networkd; then
        print_status "INFO" "Disabling systemd-networkd..."
        sudo systemctl disable --now systemd-networkd
    fi

    if systemctl is-active --quiet systemd-resolved; then
        print_status "INFO" "Disabling systemd-resolved..."
        sudo systemctl disable --now systemd-resolved
    fi

    if systemctl is-active --quiet wicd; then
        print_status "INFO" "Disabling wicd..."
        sudo systemctl disable --now wicd
    fi

    if systemctl is-active --quiet connman; then
        print_status "INFO" "Disabling connman..."
        sudo systemctl disable --now connman
    fi

    # Install NetworkManager and fzf
    print_status "INFO" "Installing NetworkManager and fzf..."
    if ! sudo pacman -S --needed --noconfirm networkmanager fzf; then
        print_status "ERROR" "Failed to install NetworkManager and fzf"
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

    # Check for fzf (interactive network selection)
    if ! command -v fzf &> /dev/null; then
        missing_deps+=("fzf")
    fi

    print_status "OK" "NetworkManager is working correctly"

    # Handle missing fzf
    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        print_status "INFO" "Missing optional dependency: fzf"
        print_status "INFO" "fzf enables interactive network selection"
        echo
        read -p "Do you want to install fzf? [Y/n]: " -r
        if [[ ! $REPLY =~ ^[Nn]$ ]]; then
            print_status "INFO" "Installing fzf..."
            sudo pacman -S --needed --noconfirm fzf
            print_status "OK" "fzf installed"
        else
            print_status "INFO" "Skipping fzf - will use text-based selection"
        fi
    else
        print_status "OK" "fzf is available for interactive selection"
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

    # If conflicts were detected and user agreed, switch to NetworkManager
    if [[ $? -eq 0 ]] && ! systemctl is-active --quiet NetworkManager; then
        switch_to_networkmanager
        if [[ $? -ne 0 ]]; then
            print_status "ERROR" "Failed to switch to NetworkManager"
            exit 1
        fi
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

    verify_installation
    echo

    show_post_install_info
}

# Run main installation
main "$@"