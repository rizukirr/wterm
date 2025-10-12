#!/bin/bash

SSID=$1
PASSWORD=$2

sudo nmcli device set wlan0 managed no
sleep 2
sudo nmcli device set wlan0 managed yes
sleep 2
sudo nmcli device wifi connect "$SSID" password "$PASSWORD"
nmcli device status
nmcli connection show --active
ip addr show wlan0 | grep "inet "
iw dev wlan0 link
