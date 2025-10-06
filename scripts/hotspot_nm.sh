#!/bin/bash

# hotspot_nm.sh - NetworkManager-based WiFi hotspot management
# Uses nmcli to create and manage WiFi hotspots (Access Points)

set -euo pipefail

VERSION="1.0.0"
SCRIPT_NAME="$(basename "$0")"

# Default values
DEFAULT_INTERFACE="wlan0"
DEFAULT_BAND="bg"  # 2.4GHz
DEFAULT_GATEWAY="192.168.12.1/24"
CONNECTION_PREFIX="wterm-hotspot-"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print colored output
print_error() {
    echo -e "${RED}ERROR: $1${NC}" >&2
}

print_success() {
    echo -e "${GREEN}$1${NC}"
}

print_warning() {
    echo -e "${YELLOW}WARNING: $1${NC}"
}

print_info() {
    echo -e "${BLUE}INFO: $1${NC}"
}

# Usage information
usage() {
    cat << EOF
Usage: $SCRIPT_NAME <command> [arguments]

NetworkManager-based WiFi hotspot management tool.

Commands:
    create <name> <password> [options]    Create and optionally start a hotspot
    start <name>                          Start an existing hotspot
    stop <name>                           Stop a running hotspot
    delete <name>                         Delete a hotspot configuration
    list                                  List all hotspot configurations
    status [name]                         Show status of hotspot(s)
    restart <name>                        Restart a hotspot
    help                                  Show this help message
    version                               Show version information

Create Options:
    --interface <iface>                   WiFi interface (default: $DEFAULT_INTERFACE)
    --band <bg|a>                         Frequency band: bg=2.4GHz, a=5GHz (default: $DEFAULT_BAND)
    --no-start                            Create configuration without starting
    --open                                Create open hotspot (no password)
    --gateway <ip/prefix>                 Gateway IP (default: $DEFAULT_GATEWAY)

Examples:
    # Create and start a secured hotspot
    sudo $SCRIPT_NAME create MyHotspot mypassword123

    # Create an open hotspot on 5GHz
    sudo $SCRIPT_NAME create PublicWiFi --open --band a

    # Create without starting
    sudo $SCRIPT_NAME create MyHotspot password123 --no-start

    # Start/stop hotspots
    sudo $SCRIPT_NAME start MyHotspot
    sudo $SCRIPT_NAME stop MyHotspot

    # List all configured hotspots
    sudo $SCRIPT_NAME list

    # Check status
    sudo $SCRIPT_NAME status
    sudo $SCRIPT_NAME status MyHotspot

Notes:
    - Requires root/sudo privileges
    - Starting a hotspot will disconnect active WiFi connection
    - Hotspot and WiFi client mode cannot run simultaneously on same channel
    - Configurations are persistent (survive reboots)

EOF
}

# Check if running as root
check_root() {
    if [[ $EUID -ne 0 ]]; then
        print_error "This script must be run as root (use sudo)"
        exit 1
    fi
}

# Check if nmcli is available
check_dependencies() {
    if ! command -v nmcli &> /dev/null; then
        print_error "nmcli (NetworkManager) is not installed"
        exit 1
    fi
}

# Check if interface exists and supports WiFi
check_interface() {
    local iface="$1"

    if ! nmcli device status | grep -q "^$iface"; then
        print_error "Interface '$iface' not found"
        return 1
    fi

    if ! nmcli device status | grep "^$iface" | grep -q "wifi"; then
        print_error "Interface '$iface' is not a WiFi device"
        return 1
    fi

    return 0
}

# Get connection name with prefix
get_connection_name() {
    local name="$1"
    echo "${CONNECTION_PREFIX}${name}"
}

# Check if connection exists
connection_exists() {
    local conn_name="$1"
    nmcli connection show "$conn_name" &>/dev/null
}

# Check if connection is active
connection_is_active() {
    local conn_name="$1"
    nmcli connection show --active | grep -q "^$conn_name"
}

# Create hotspot configuration
cmd_create() {
    local name=""
    local password=""
    local interface="$DEFAULT_INTERFACE"
    local band="$DEFAULT_BAND"
    local gateway="$DEFAULT_GATEWAY"
    local no_start=false
    local open=false

    # Parse arguments
    if [[ $# -lt 1 ]]; then
        print_error "Hotspot name required"
        echo "Usage: $SCRIPT_NAME create <name> <password> [options]"
        exit 1
    fi

    name="$1"
    shift

    # Check for --open flag before requiring password
    local args=("$@")
    for arg in "${args[@]}"; do
        if [[ "$arg" == "--open" ]]; then
            open=true
            break
        fi
    done

    # Require password if not open
    if [[ "$open" == false ]]; then
        if [[ $# -lt 1 ]]; then
            print_error "Password required (or use --open for open hotspot)"
            echo "Usage: $SCRIPT_NAME create <name> <password> [options]"
            exit 1
        fi
        password="$1"
        shift
    fi

    # Parse options
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --interface)
                interface="$2"
                shift 2
                ;;
            --band)
                band="$2"
                shift 2
                ;;
            --no-start)
                no_start=true
                shift
                ;;
            --open)
                open=true
                shift
                ;;
            --gateway)
                gateway="$2"
                shift 2
                ;;
            *)
                print_error "Unknown option: $1"
                exit 1
                ;;
        esac
    done

    # Validate inputs
    check_interface "$interface" || exit 1

    if [[ "$open" == false ]] && [[ ${#password} -lt 8 || ${#password} -gt 63 ]]; then
        print_error "Password must be 8-63 characters"
        exit 1
    fi

    if [[ ! "$band" =~ ^(bg|a)$ ]]; then
        print_error "Band must be 'bg' (2.4GHz) or 'a' (5GHz)"
        exit 1
    fi

    local conn_name
    conn_name=$(get_connection_name "$name")

    # Check if connection already exists
    if connection_exists "$conn_name"; then
        print_error "Hotspot '$name' already exists. Delete it first with: $SCRIPT_NAME delete $name"
        exit 1
    fi

    print_info "Creating hotspot '$name' on interface $interface..."

    # Create the connection
    if nmcli connection add \
        type wifi \
        ifname "$interface" \
        con-name "$conn_name" \
        autoconnect no \
        ssid "$name" \
        802-11-wireless.mode ap \
        802-11-wireless.band "$band" \
        ipv4.method shared \
        ipv4.addresses "$gateway" &>/dev/null; then

        # Add security if not open
        if [[ "$open" == false ]]; then
            if nmcli connection modify "$conn_name" \
                802-11-wireless-security.key-mgmt wpa-psk \
                802-11-wireless-security.psk "$password" &>/dev/null; then
                print_success "Created secured hotspot '$name'"
            else
                print_error "Failed to set password for hotspot '$name'"
                nmcli connection delete "$conn_name" &>/dev/null
                exit 1
            fi
        else
            print_success "Created open hotspot '$name'"
        fi

        # Start if not disabled
        if [[ "$no_start" == false ]]; then
            print_info "Starting hotspot '$name'..."
            cmd_start "$name"
        else
            print_info "Hotspot created but not started. Start with: $SCRIPT_NAME start $name"
        fi
    else
        print_error "Failed to create hotspot '$name'"
        exit 1
    fi
}

# Start hotspot
cmd_start() {
    local name="$1"
    local conn_name
    conn_name=$(get_connection_name "$name")

    if ! connection_exists "$conn_name"; then
        print_error "Hotspot '$name' not found. Create it first with: $SCRIPT_NAME create"
        exit 1
    fi

    if connection_is_active "$conn_name"; then
        print_warning "Hotspot '$name' is already running"
        return 0
    fi

    # Get the interface for this connection
    local iface
    iface=$(nmcli -t -f connection.interface-name connection show "$conn_name" | cut -d: -f2)

    # Check if interface is currently connected to a network
    if nmcli device status | grep "^$iface" | grep -q "connected"; then
        print_warning "Disconnecting from WiFi network on $iface..."
        # Get active connection on this interface
        local active_conn
        active_conn=$(nmcli -t -f DEVICE,NAME connection show --active | grep "^$iface:" | cut -d: -f2)
        if [[ -n "$active_conn" ]]; then
            nmcli connection down "$active_conn" &>/dev/null || true
        fi
    fi

    print_info "Starting hotspot '$name'..."

    if nmcli connection up "$conn_name" &>/dev/null; then
        print_success "Hotspot '$name' started successfully"
        print_info "SSID: $name"
        print_info "Gateway: $(nmcli -t -f IP4.ADDRESS connection show "$conn_name" | cut -d: -f2 | cut -d/ -f1)"
    else
        print_error "Failed to start hotspot '$name'"
        print_info "Possible reasons:"
        print_info "  - WiFi adapter doesn't support AP mode"
        print_info "  - Channel conflict with existing networks"
        print_info "  - NetworkManager is busy"
        exit 1
    fi
}

# Stop hotspot
cmd_stop() {
    local name="$1"
    local conn_name
    conn_name=$(get_connection_name "$name")

    if ! connection_exists "$conn_name"; then
        print_error "Hotspot '$name' not found"
        exit 1
    fi

    if ! connection_is_active "$conn_name"; then
        print_warning "Hotspot '$name' is not running"
        return 0
    fi

    print_info "Stopping hotspot '$name'..."

    if nmcli connection down "$conn_name" &>/dev/null; then
        print_success "Hotspot '$name' stopped"
    else
        print_error "Failed to stop hotspot '$name'"
        exit 1
    fi
}

# Delete hotspot configuration
cmd_delete() {
    local name="$1"
    local conn_name
    conn_name=$(get_connection_name "$name")

    if ! connection_exists "$conn_name"; then
        print_error "Hotspot '$name' not found"
        exit 1
    fi

    # Stop if running
    if connection_is_active "$conn_name"; then
        print_info "Stopping hotspot '$name' before deletion..."
        nmcli connection down "$conn_name" &>/dev/null || true
    fi

    print_info "Deleting hotspot '$name'..."

    if nmcli connection delete "$conn_name" &>/dev/null; then
        print_success "Hotspot '$name' deleted"
    else
        print_error "Failed to delete hotspot '$name'"
        exit 1
    fi
}

# List all hotspot configurations
cmd_list() {
    local found=false

    echo "Configured hotspots:"
    echo "===================="

    while IFS= read -r line; do
        if [[ "$line" =~ ^${CONNECTION_PREFIX}(.+)$ ]]; then
            local name="${BASH_REMATCH[1]}"
            local conn_name="${line%%:*}"
            local status="Stopped"

            if connection_is_active "$conn_name"; then
                status="Running"
            fi

            echo "  - $name [$status]"
            found=true
        fi
    done < <(nmcli -t -f NAME connection show | grep "^${CONNECTION_PREFIX}")

    if [[ "$found" == false ]]; then
        echo "  (none)"
    fi
}

# Show status
cmd_status() {
    local name="${1:-}"

    if [[ -z "$name" ]]; then
        # Show status of all hotspots
        local found=false

        echo "Hotspot Status:"
        echo "==============="

        while IFS= read -r line; do
            if [[ "$line" =~ ^${CONNECTION_PREFIX}(.+)$ ]]; then
                local hotspot_name="${BASH_REMATCH[1]}"
                show_hotspot_status "$hotspot_name"
                found=true
                echo ""
            fi
        done < <(nmcli -t -f NAME connection show | grep "^${CONNECTION_PREFIX}")

        if [[ "$found" == false ]]; then
            echo "No hotspots configured"
        fi
    else
        # Show status of specific hotspot
        local conn_name
        conn_name=$(get_connection_name "$name")

        if ! connection_exists "$conn_name"; then
            print_error "Hotspot '$name' not found"
            exit 1
        fi

        show_hotspot_status "$name"
    fi
}

# Show detailed status of a hotspot
show_hotspot_status() {
    local name="$1"
    local conn_name
    conn_name=$(get_connection_name "$name")

    echo "Hotspot: $name"
    echo "  Connection Name: $conn_name"

    if connection_is_active "$conn_name"; then
        echo "  Status: ${GREEN}Running${NC}"

        local iface
        iface=$(nmcli -t -f GENERAL.DEVICES connection show "$conn_name" | cut -d: -f2)
        echo "  Interface: $iface"

        local ip
        ip=$(nmcli -t -f IP4.ADDRESS connection show "$conn_name" | cut -d: -f2 | cut -d/ -f1)
        echo "  Gateway IP: $ip"

        local ssid
        ssid=$(nmcli -t -f 802-11-wireless.ssid connection show "$conn_name" | cut -d: -f2)
        echo "  SSID: $ssid"

        local security
        security=$(nmcli -t -f 802-11-wireless-security.key-mgmt connection show "$conn_name" | cut -d: -f2)
        if [[ -n "$security" && "$security" != "--" ]]; then
            echo "  Security: WPA2-PSK"
        else
            echo "  Security: Open"
        fi
    else
        echo "  Status: ${YELLOW}Stopped${NC}"
    fi
}

# Restart hotspot
cmd_restart() {
    local name="$1"

    print_info "Restarting hotspot '$name'..."
    cmd_stop "$name"
    sleep 1
    cmd_start "$name"
}

# Interactive mode functions for C UI integration
cmd_interactive() {
    local action="$1"
    shift

    case "$action" in
        list-actions)
            # Output available actions for main menu (format: id\tdisplay_text)
            echo -e "create\tCreate new hotspot"
            echo -e "start\tStart existing hotspot"
            echo -e "stop\tStop running hotspot"
            echo -e "restart\tRestart hotspot"
            echo -e "delete\tDelete hotspot"
            echo -e "list\tList all hotspots"
            echo -e "status\tShow status"
            echo -e "exit\tExit"
            ;;

        list-hotspots)
            # Output hotspots in parseable format (format: name\tstatus\tdetails)
            check_dependencies
            local found=false

            while IFS= read -r line; do
                if [[ "$line" =~ ^${CONNECTION_PREFIX}(.+)$ ]]; then
                    local name="${BASH_REMATCH[1]}"
                    local conn_name="${line%%:*}"
                    local status="stopped"
                    local details=""

                    if connection_is_active "$conn_name"; then
                        status="running"

                        # Get details for running hotspot
                        local iface security band clients ip
                        iface=$(nmcli -t -f GENERAL.DEVICES connection show "$conn_name" 2>/dev/null | cut -d: -f2)
                        security=$(nmcli -t -f 802-11-wireless-security.key-mgmt connection show "$conn_name" 2>/dev/null | cut -d: -f2)
                        band=$(nmcli -t -f 802-11-wireless.band connection show "$conn_name" 2>/dev/null | cut -d: -f2)
                        ip=$(nmcli -t -f IP4.ADDRESS connection show "$conn_name" 2>/dev/null | cut -d: -f2 | cut -d/ -f1)

                        # Convert band
                        if [[ "$band" == "bg" ]]; then
                            band="2.4GHz"
                        elif [[ "$band" == "a" ]]; then
                            band="5GHz"
                        fi

                        # Determine security type
                        if [[ -n "$security" && "$security" != "--" ]]; then
                            security="WPA2"
                        else
                            security="Open"
                        fi

                        details="$band, $security, IP: $ip"
                    else
                        # Get details for stopped hotspot
                        local security band
                        security=$(nmcli -t -f 802-11-wireless-security.key-mgmt connection show "$conn_name" 2>/dev/null | cut -d: -f2)
                        band=$(nmcli -t -f 802-11-wireless.band connection show "$conn_name" 2>/dev/null | cut -d: -f2)

                        if [[ "$band" == "bg" ]]; then
                            band="2.4GHz"
                        elif [[ "$band" == "a" ]]; then
                            band="5GHz"
                        fi

                        if [[ -n "$security" && "$security" != "--" ]]; then
                            security="WPA2"
                        else
                            security="Open"
                        fi

                        details="$band, $security"
                    fi

                    echo -e "$name\t$status\t$details"
                    found=true
                fi
            done < <(nmcli -t -f NAME connection show | grep "^${CONNECTION_PREFIX}")

            [[ "$found" == false ]] && exit 1
            ;;

        get-interfaces)
            # Output available WiFi interfaces (format: interface\tstatus)
            check_dependencies
            nmcli device status | grep wifi | awk '{print $1 "\t" $3}'
            ;;

        list-bands)
            # Output available frequency bands
            echo -e "bg\t2.4GHz (Better range, more compatible)"
            echo -e "a\t5GHz (Faster, less interference)"
            ;;

        list-security)
            # Output security options
            echo -e "secured\tSecured (WPA2-PSK)"
            echo -e "open\tOpen (No password)"
            ;;

        *)
            print_error "Unknown interactive action: $action"
            exit 1
            ;;
    esac
}

# Main function
main() {
    if [[ $# -lt 1 ]]; then
        usage
        exit 1
    fi

    local command="$1"
    shift

    case "$command" in
        create)
            check_root
            check_dependencies
            cmd_create "$@"
            ;;
        start)
            check_root
            check_dependencies
            if [[ $# -lt 1 ]]; then
                print_error "Hotspot name required"
                exit 1
            fi
            cmd_start "$1"
            ;;
        stop)
            check_root
            check_dependencies
            if [[ $# -lt 1 ]]; then
                print_error "Hotspot name required"
                exit 1
            fi
            cmd_stop "$1"
            ;;
        delete)
            check_root
            check_dependencies
            if [[ $# -lt 1 ]]; then
                print_error "Hotspot name required"
                exit 1
            fi
            cmd_delete "$1"
            ;;
        list)
            check_dependencies
            cmd_list
            ;;
        status)
            check_dependencies
            cmd_status "$@"
            ;;
        restart)
            check_root
            check_dependencies
            if [[ $# -lt 1 ]]; then
                print_error "Hotspot name required"
                exit 1
            fi
            cmd_restart "$1"
            ;;
        interactive)
            # Interactive mode for C UI integration
            cmd_interactive "$@"
            ;;
        help|--help|-h)
            usage
            ;;
        version|--version|-v)
            echo "$SCRIPT_NAME version $VERSION"
            ;;
        *)
            print_error "Unknown command: $command"
            echo ""
            usage
            exit 1
            ;;
    esac
}

main "$@"
