#!/bin/sh
# Size the runner's CPU quota by battery level (docs/bringup.md B6). On a
# Raspberry Pi 4 USB port: 2 cores of build load drain the battery (-100 mA),
# 1 core still charges (+155 mA). Hysteresis between 40 % and 50 %.
set -u
cap=/sys/class/power_supply/max170xx_battery/capacity
unit=actions-runner.service
quota=

while :; do
	c=$(cat $cap) || { echo "ERROR: cannot read $cap" >&2; exit 1; }
	want=$quota
	if [ "$c" -lt 20 ]; then
		if systemctl is-active -q $unit; then
			echo "battery $c %: stopping $unit"
			systemctl stop $unit
		fi
	elif [ "$c" -ge 30 ] && systemctl is-enabled -q $unit &&
		! systemctl is-active -q $unit; then
		echo "battery $c %: starting $unit"
		systemctl start $unit
	fi
	if [ "$c" -lt 20 ]; then
		:
	elif [ "$c" -ge 50 ]; then
		want=200%
	elif [ "$c" -lt 40 ] || [ -z "$want" ]; then
		want=100%
	fi
	if [ "$want" != "$quota" ]; then
		systemctl set-property --runtime $unit CPUQuota="$want" ||
			{ echo "ERROR: set CPUQuota=$want failed" >&2; exit 1; }
		echo "battery $c %: CPUQuota=$want"
		quota=$want
	fi
	sleep 60
done
