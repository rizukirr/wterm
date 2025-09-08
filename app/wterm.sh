#!/bin/bash

# Auto-detect WiFi interface (fallback to wlan0 if not found)
INTERFACE=$(iwctl device list 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | awk 'NF>=4 && $1!="Name" && $1!~/^-+$/ && $1!="Devices" {print $1; exit}')
[ -z "$INTERFACE" ] && INTERFACE="wlan0"

scan_networks() {
  iwctl station $INTERFACE scan &>/dev/null
  sleep 2

  # Show all networks - let user pick from full list
  iwctl station $INTERFACE get-networks 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | tail -n +5 | head -n -1 | sed 's/^[[:space:]]*//' | sed 's/>[[:space:]]*//'
}

while true; do
  # Get networks and add rescan option
  NETWORKS=$(scan_networks)
  
  # Create menu options
  OPTIONS="🔄 Rescan
$NETWORKS"

  # Show menu with fzf
  SELECTED=$(echo "$OPTIONS" | fzf --prompt="Select WiFi network: " --height=40% --border --header="Use arrow keys to navigate, Enter to select, Esc to exit")

  # Exit if nothing selected
  [ -z "$SELECTED" ] && exit 0

  if [ "$SELECTED" = "🔄 Rescan" ]; then
    continue
  fi

  # Extract network name from the selected line
  CLEAN_SELECTED=$(echo "$SELECTED" | sed 's/^[[:space:]]*>//' | sed 's/^[[:space:]]*//' | awk '{
    for(i=1; i<=NF; i++) {
      if($i == "psk" || $i == "none" || $i == "wep" || $i == "open") break;
      if(i > 1) printf " ";
      printf "%s", $i;
    }
    print "";
  }' | sed 's/[[:space:]]*$//')

  # Check if already connected to this network
  CURRENT_NETWORK=$(iwctl station $INTERFACE show 2>/dev/null | grep "Connected network" | awk '{for(i=3;i<=NF;i++) printf "%s ", $i; print ""}' | sed 's/[[:space:]]*$//')
  
  if [ "$CURRENT_NETWORK" = "$CLEAN_SELECTED" ]; then
    read -p "Already connected to $CLEAN_SELECTED. Disconnect? [y/N]: " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
      iwctl station $INTERFACE disconnect
      echo "✅ Disconnected from $CLEAN_SELECTED"
    else
      echo "Staying connected to $CLEAN_SELECTED"
    fi
  else
    echo "Connecting to: $CLEAN_SELECTED"
    # Let iwctl handle password prompting automatically
    # Suppress stderr to hide "Invalid network name" warnings while preserving password prompts
    iwctl station $INTERFACE connect "$CLEAN_SELECTED" 2> >(grep -v "Invalid network name" >&2)
    
    # Check connection status
    sleep 2
    if iwctl station $INTERFACE show | grep -q "connected"; then
      echo "✅ Connected to $CLEAN_SELECTED"
    else
      echo "❌ Failed to connect to $CLEAN_SELECTED"
    fi
  fi

  # Stop loop after attempt
  break
done