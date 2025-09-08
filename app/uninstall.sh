#!/bin/bash

# WiFi Terminal Tool Uninstaller
# Removes wterm command and optionally removes dependencies

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Track what was removed
REMOVED_FILES=()

# Function to print colored output
print_status() {
    case $1 in
        "OK") echo -e "${GREEN}✅ $2${NC}" ;;
        "ERROR") echo -e "${RED}❌ $2${NC}" ;;
        "INFO") echo -e "${BLUE}ℹ️  $2${NC}" ;;
        "WARN") echo -e "${YELLOW}⚠️  $2${NC}" ;;
    esac
}

echo "=== WiFi Terminal (wterm) Uninstaller ==="
echo

# Check if running as root
if [[ $EUID -ne 0 ]]; then
   print_status "ERROR" "This script must be run as root"
   echo
   print_status "INFO" "Root access is required to:"
   echo "  • Remove wterm from /usr/local/bin/ (system directory)"
   echo "  • Uninstall system packages (fzf) via pacman"
   echo
   echo "Usage: sudo ./uninstall.sh"
   exit 1
fi

# Check if wterm is installed
if [[ ! -f "/usr/local/bin/wterm" ]]; then
    print_status "WARN" "wterm is not installed at /usr/local/bin/wterm"
    echo
    print_status "INFO" "Nothing to uninstall."
    exit 0
fi

# Remove wterm binary
print_status "INFO" "Removing wterm from /usr/local/bin..."
if rm /usr/local/bin/wterm 2>/dev/null; then
    REMOVED_FILES+=("/usr/local/bin/wterm")
    print_status "OK" "wterm removed successfully!"
else
    print_status "ERROR" "Failed to remove /usr/local/bin/wterm"
    exit 1
fi

echo

# Ask about fzf dependency
print_status "INFO" "Do you want to remove fzf (fuzzy finder)?"
print_status "WARN" "Only remove if you're not using fzf elsewhere!"
echo
echo "Optional dependency:"
echo "  • fzf (fuzzy finder) - used for interactive WiFi network selection"
echo
print_status "INFO" "Note: iwd is kept as it's essential for Arch Linux wireless networking"
echo

read -p "Remove fzf? [y/N]: " -r
if [[ $REPLY =~ ^[Yy]$ ]]; then
    # Check if fzf is installed
    if pacman -Q fzf &>/dev/null; then
        print_status "INFO" "Removing fzf..."
        if pacman -R --noconfirm fzf; then
            print_status "OK" "fzf removed successfully!"
        else
            print_status "ERROR" "Failed to remove fzf"
        fi
    else
        print_status "INFO" "fzf is not installed"
    fi
else
    print_status "INFO" "fzf kept. You can remove it manually later if needed:"
    echo "  sudo pacman -R fzf"
fi

echo
print_status "OK" "Uninstallation complete!"

if [[ ${#REMOVED_FILES[@]} -gt 0 ]]; then
    echo
    print_status "INFO" "Files that were removed:"
    for file in "${REMOVED_FILES[@]}"; do
        echo "  • $file"
    done
fi

echo
print_status "INFO" "To reinstall wterm, run: sudo ./install.sh"