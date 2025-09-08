#!/bin/bash

# WiFi Terminal Tool Installer
# Installs wterm.sh as 'wterm' command

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Track installation state
INSTALLED_FILES=()
SCRIPT_PID=$$

# Function to print colored output
print_status() {
    case $1 in
        "OK") echo -e "${GREEN}✅ $2${NC}" ;;
        "ERROR") echo -e "${RED}❌ $2${NC}" ;;
        "INFO") echo -e "${BLUE}ℹ️  $2${NC}" ;;
        "WARN") echo -e "${YELLOW}⚠️  $2${NC}" ;;
    esac
}

# Function to show uninstall instructions
show_uninstall_info() {
    echo
    print_status "INFO" "To uninstall wterm manually, run these commands:"
    if [[ -f "/usr/local/bin/wterm" ]]; then
        echo "  sudo rm /usr/local/bin/wterm"
    fi
    echo
    print_status "INFO" "Dependencies (if no longer needed):"
    echo "  sudo pacman -R iwd fzf"
    echo "  sudo systemctl disable --now iwd"
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
        show_uninstall_info
    else
        print_status "INFO" "No files were installed."
    fi
    
    exit 130
}

# Set up trap for interruption signals
trap cleanup_on_interrupt SIGINT SIGTERM

echo "=== WiFi Terminal (wterm) Installer ==="
echo

# Check if running as root
if [[ $EUID -ne 0 ]]; then
   print_status "ERROR" "This script must be run as root"
   echo
   print_status "INFO" "Root access is required to:"
   echo "  • Copy wterm to /usr/local/bin/ (system-wide installation)"
   echo "  • Install system packages (iwd, fzf) via pacman"
   echo "  • Enable/configure system services (iwd daemon)"
   echo
   echo "Usage: sudo ./install.sh"
   exit 1
fi

# Get the directory where this script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
WTERM_SCRIPT="$SCRIPT_DIR/wterm.sh"

# Check if wterm.sh exists in the same directory as this script
if [[ ! -f "$WTERM_SCRIPT" ]]; then
   print_status "ERROR" "wterm.sh not found in script directory: $SCRIPT_DIR"
   exit 1
fi

# Check if wterm already exists
if [[ -f "/usr/local/bin/wterm" ]]; then
    print_status "WARN" "wterm is already installed at /usr/local/bin/wterm"
    read -p "Overwrite existing installation? [y/N]: " -n 1 -r
    echo
    [[ ! $REPLY =~ ^[Yy]$ ]] && exit 1
fi

# Copy the script to /usr/local/bin as 'wterm'
print_status "INFO" "Installing wterm to /usr/local/bin..."
if cp "$WTERM_SCRIPT" /usr/local/bin/wterm; then
    INSTALLED_FILES+=("/usr/local/bin/wterm")
else
    print_status "ERROR" "Failed to copy file from $WTERM_SCRIPT"
    exit 1
fi

# Make it executable
if chmod +x /usr/local/bin/wterm; then
    print_status "INFO" "Made wterm executable"
else
    print_status "ERROR" "Failed to make wterm executable"
    show_uninstall_info
    exit 1
fi

# Check dependencies
check_dependencies() {
    local missing_deps=()
    local iwd_service_needed=false
    
    # Check if iwd is installed
    if ! pacman -Q iwd &>/dev/null; then
        missing_deps+=("iwd")
    fi
    
    # Check if fzf is installed
    if ! pacman -Q fzf &>/dev/null; then
        missing_deps+=("fzf")
    fi
    
    # Check if iwd service is enabled
    if ! systemctl is-enabled --quiet iwd 2>/dev/null; then
        iwd_service_needed=true
    fi
    
    # If no missing dependencies and service is enabled, we're good
    if [[ ${#missing_deps[@]} -eq 0 && $iwd_service_needed == false ]]; then
        print_status "OK" "All dependencies are already installed and configured!"
        return 0
    fi
    
    echo
    print_status "INFO" "Checking dependencies..."
    
    # Show what's missing
    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        print_status "WARN" "Missing dependencies: ${missing_deps[*]}"
    fi
    
    if [[ $iwd_service_needed == true ]]; then
        print_status "WARN" "iwd service needs to be enabled"
    fi
    
    echo
    print_status "INFO" "You can:"
    echo "  1. Install automatically (requires root privileges)"
    echo "  2. Install manually and run this script again"
    echo
    
    read -p "Do you want to auto-install missing dependencies? [y/N]: " -r
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        # Check if we're running as root for auto-install
        if [[ $EUID -ne 0 ]]; then
            print_status "ERROR" "Auto-install requires root privileges"
            echo "Please run with sudo: sudo ./install.sh"
            echo
            print_status "INFO" "Or install manually:"
            if [[ ${#missing_deps[@]} -gt 0 ]]; then
                echo "  sudo pacman -S ${missing_deps[*]}"
            fi
            if [[ $iwd_service_needed == true ]]; then
                echo "  sudo systemctl enable --now iwd"
            fi
            exit 1
        fi
        
        # Auto-install missing packages
        if [[ ${#missing_deps[@]} -gt 0 ]]; then
            print_status "INFO" "Installing missing packages: ${missing_deps[*]}"
            if pacman -S --noconfirm "${missing_deps[@]}"; then
                print_status "OK" "Dependencies installed successfully!"
            else
                print_status "ERROR" "Failed to install dependencies"
                exit 1
            fi
        fi
        
        # Enable iwd service if needed
        if [[ $iwd_service_needed == true ]]; then
            print_status "INFO" "Enabling and starting iwd service..."
            if systemctl enable --now iwd; then
                print_status "OK" "iwd service enabled and started!"
            else
                print_status "ERROR" "Failed to enable iwd service"
                exit 1
            fi
        fi
    else
        print_status "INFO" "Please install the missing dependencies manually:"
        if [[ ${#missing_deps[@]} -gt 0 ]]; then
            echo "  sudo pacman -S ${missing_deps[*]}"
        fi
        if [[ $iwd_service_needed == true ]]; then
            echo "  sudo systemctl enable --now iwd"
        fi
        echo
        print_status "INFO" "Then run this installer again: sudo ./install.sh"
        exit 1
    fi
}

# Verify installation
if [[ -x /usr/local/bin/wterm ]]; then
   print_status "OK" "wterm installed successfully!"
   echo
   
   # Check and install dependencies
   check_dependencies
   
   echo
   print_status "INFO" "You can now run it from anywhere with: wterm"
   echo
   show_uninstall_info
else
   print_status "ERROR" "Installation failed"
   show_uninstall_info
   exit 1
fi