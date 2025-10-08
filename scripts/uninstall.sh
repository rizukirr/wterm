#!/bin/bash

##
# uninstall.sh - System uninstallation script for wterm
#
# This script safely removes wterm and optionally restores the previous
# network manager configuration.
##

set -e # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# State file for tracking installation changes
STATE_FILE="/etc/wterm/previous_state.txt"

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
  print_status "WARN" "Uninstallation interrupted!"
  echo
  print_status "INFO" "System may be in an inconsistent state"
  print_status "INFO" "You may need to manually check network manager services"
  exit 130
}

# Set up trap for interruption signals
trap cleanup_on_interrupt SIGINT SIGTERM

# Check if running as root
check_permissions() {
  if [[ $EUID -eq 0 ]]; then
    print_status "WARN" "Running as root - this is not recommended"
    print_status "INFO" "Consider running as regular user (will prompt for sudo when needed)"
    echo
    read -p "Continue anyway? [y/N]: " -n 1 -r
    echo
    [[ ! $REPLY =~ ^[Yy]$ ]] && exit 1
  fi
}

# Load previous installation state
load_installation_state() {
  if [[ ! -f "$STATE_FILE" ]]; then
    print_status "WARN" "No installation state file found"
    print_status "INFO" "wterm may not have been installed by the install script"
    print_status "INFO" "Will only remove the executable"
    return 1
  fi

  # Source the state file safely
  source "$STATE_FILE"
  print_status "OK" "Loaded installation state from $STATE_FILE"
  return 0
}

# Remove wterm executable
remove_wterm_executable() {
  print_status "INFO" "Removing wterm executable..."

  local wterm_path="/usr/local/bin/wterm"

  if [[ -f "$wterm_path" ]]; then
    sudo rm -f "$wterm_path"
    print_status "OK" "wterm executable removed from $wterm_path"
  else
    print_status "INFO" "wterm executable not found at $wterm_path"
  fi

  # Remove hotspot management script
  local script_path="/usr/local/bin/hotspot_nm.sh"
  if [[ -f "$script_path" ]]; then
    sudo rm -f "$script_path"
    print_status "OK" "Hotspot script removed from $script_path"
  else
    print_status "INFO" "Hotspot script not found at $script_path"
  fi
}

# Restore previous network manager
restore_previous_network_manager() {
  if [[ -z "${previous_netmgr:-}" ]]; then
    print_status "INFO" "No previous network manager to restore"
    return 0
  fi

  print_status "INFO" "Found previous network manager: $previous_netmgr"
  echo
  read -p "Do you want to restore $previous_netmgr as your network manager? [y/N]: " -r
  if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    print_status "INFO" "Keeping current network manager configuration"
    return 0
  fi

  case "$previous_netmgr" in
  "systemd-networkd")
    print_status "INFO" "Restoring systemd-networkd configuration..."

    # Disable NetworkManager
    sudo systemctl disable --now NetworkManager
    print_status "OK" "NetworkManager disabled"

    # Enable systemd-networkd
    sudo systemctl enable --now systemd-networkd
    print_status "OK" "systemd-networkd enabled"

    # Restore systemd-resolved if it was active
    if [[ "${systemd_resolved_was_active:-false}" == "true" ]]; then
      sudo systemctl enable --now systemd-resolved
      print_status "OK" "systemd-resolved restored"
    fi

    # Wait for services to initialize
    sleep 2

    # Verify systemd-networkd is working
    if systemctl is-active --quiet systemd-networkd; then
      print_status "OK" "systemd-networkd is now active"
    else
      print_status "WARN" "systemd-networkd failed to start"
    fi
    ;;
  *)
    print_status "WARN" "Unknown previous network manager: $previous_netmgr"
    print_status "INFO" "You may need to manually configure your network manager"
    ;;
  esac
}

# Handle NetworkManager removal
handle_networkmanager_removal() {
  if ! systemctl is-enabled --quiet NetworkManager 2>/dev/null; then
    print_status "INFO" "NetworkManager is not enabled, skipping removal options"
    return 0
  fi

  echo
  print_status "INFO" "NetworkManager removal options:"
  echo "  1. Remove NetworkManager completely"
  echo "  2. Disable NetworkManager but keep it installed"
  echo "  3. Keep NetworkManager as-is"
  echo
  read -p "Choose option [1/2/3]: " -r

  case $REPLY in
  1)
    print_status "INFO" "Removing NetworkManager..."
    sudo systemctl disable --now NetworkManager
    sudo pacman -Rs networkmanager
    print_status "OK" "NetworkManager removed"
    ;;
  2)
    print_status "INFO" "Disabling NetworkManager..."
    sudo systemctl disable --now NetworkManager
    print_status "OK" "NetworkManager disabled but not removed"
    ;;
  3 | *)
    print_status "INFO" "Keeping NetworkManager configuration unchanged"
    ;;
  esac
}

# Handle fzf removal
handle_fzf_removal() {
  if ! command -v fzf &>/dev/null; then
    print_status "INFO" "fzf is not installed, skipping"
    return 0
  fi

  echo
  read -p "Do you want to remove fzf? [y/N]: " -r
  if [[ $REPLY =~ ^[Yy]$ ]]; then
    print_status "INFO" "Removing fzf..."
    sudo pacman -Rs fzf
    print_status "OK" "fzf removed"
  else
    print_status "INFO" "Keeping fzf installed"
  fi
}

# Clean up installation state
cleanup_installation_state() {
  print_status "INFO" "Cleaning up installation state..."

  if [[ -f "$STATE_FILE" ]]; then
    sudo rm -f "$STATE_FILE"
    print_status "OK" "Installation state file removed"
  fi

  if [[ -d "/etc/wterm" ]]; then
    sudo rmdir /etc/wterm 2>/dev/null || true
    if [[ ! -d "/etc/wterm" ]]; then
      print_status "OK" "wterm configuration directory removed"
    fi
  fi
}

# Verify system state after uninstallation
verify_uninstallation() {
  print_status "INFO" "Verifying system state after uninstallation..."

  # Check if wterm is removed
  if command -v wterm &>/dev/null; then
    print_status "WARN" "wterm command is still accessible"
    print_status "INFO" "It may be installed in a different location"
  else
    print_status "OK" "wterm command is no longer accessible"
  fi

  # Check network manager status
  local active_netmgrs=()
  if systemctl is-active --quiet NetworkManager; then
    active_netmgrs+=("NetworkManager")
  fi
  if systemctl is-active --quiet systemd-networkd; then
    active_netmgrs+=("systemd-networkd")
  fi

  if [[ ${#active_netmgrs[@]} -eq 0 ]]; then
    print_status "WARN" "No network manager is currently active"
    print_status "INFO" "You may need to manually configure network management"
  elif [[ ${#active_netmgrs[@]} -eq 1 ]]; then
    print_status "OK" "Active network manager: ${active_netmgrs[0]}"
  else
    print_status "WARN" "Multiple network managers active: ${active_netmgrs[*]}"
    print_status "INFO" "This may cause conflicts"
  fi
}

# Show post-uninstallation information
show_post_uninstall_info() {
  echo
  print_status "OK" "wterm uninstallation complete!"
  echo

  if [[ -n "${previous_netmgr:-}" ]]; then
    print_status "INFO" "Network manager configuration:"
    case "$previous_netmgr" in
    "systemd-networkd")
      echo "  • Restored to systemd-networkd"
      echo "  • Your network should continue working normally"
      ;;
    *)
      echo "  • Previous manager was $previous_netmgr"
      echo "  • Please check your network configuration"
      ;;
    esac
  else
    print_status "INFO" "Network manager configuration unchanged"
  fi

  echo
  print_status "INFO" "If you experience network issues, you may need to:"
  echo "  • Restart your system"
  echo "  • Manually configure your network manager"
  echo "  • Check systemctl status for network services"
  echo
}

# Main uninstallation process
main() {
  echo "=== wterm Uninstallation Script ==="
  echo
  print_status "INFO" "Starting wterm uninstallation..."
  echo

  check_permissions

  # Load installation state (may fail if not installed by script)
  load_installation_state
  local has_state=$?

  # Always remove the executable
  remove_wterm_executable
  echo

  # Only proceed with advanced uninstallation if we have state
  if [[ $has_state -eq 0 ]]; then
    # Restore previous network manager if requested
    restore_previous_network_manager
    echo

    # Handle NetworkManager removal
    handle_networkmanager_removal
    echo

    # Handle fzf removal
    handle_fzf_removal
    echo

    # Clean up state files
    cleanup_installation_state
    echo
  else
    print_status "INFO" "Skipping network manager restoration (no state file)"
    echo
  fi

  # Verify final state
  verify_uninstallation
  echo

  show_post_uninstall_info
}

# Run main uninstallation
main "$@"
