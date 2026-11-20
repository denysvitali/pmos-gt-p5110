#!/bin/sh
# Keep the espresso SMB347 charger in high-current mode (docs/bringup.md B6).
#
# Mainline smb347 never binds here (no monitored-battery -> -ENOENT), and even
# bound it never selects USB/HC mode by register. The chip then runs on its
# OTP defaults: USB mode selected by pin, 100/500 mA input. Samsung's
# bootloader programs it like the stock kernel does, but any VBUS drop or
# re-plug reloads the OTP defaults. This loop re-applies the stock values
# whenever they are lost. All writes are to volatile registers.
#
# ICL_REG: CFG_CURRENT_LIMIT, DC nibble | USB-HC nibble. Default 0x66 = 1.8 A
# both, for a wall charger (the kernel DT uses the same). On a Raspberry Pi 4
# port use ICL_REG=0x63 (USB-HC 900 mA): 1.8 A trips its over-current limit.
set -u
bus=4
addr=0x06
icl=${ICL_REG:-0x66}
dev=/sys/bus/i2c/devices/$bus-0006

g() { i2cget -y -a $bus $addr "$1" b; }
w() {
	i2cset -y -a $bus $addr "$1" "$2" b
	got=$(g "$1")
	[ "$got" = "$2" ] || { echo "ERROR: reg $1 wrote $2, read $got" >&2; return 1; }
}

apply() {
	w 0x30 0x80 && # CMD_A: allow volatile config writes
	w 0x06 0x60 && # CFG_PIN: USB5/1/HC by register (stock value)
	w 0x01 "$icl" && # CFG_CURRENT_LIMIT
	w 0x31 0x03 # CMD_B: high-current mode
}

while :; do
	if [ -e "$dev/driver" ]; then
		echo "kernel driver bound to $dev; exiting"
		exit 0
	fi
	cur=
	for r in 0x30 0x06 0x01 0x31; do
		v=$(g $r) || { echo "ERROR: cannot read SMB347 reg $r on i2c-$bus" >&2; exit 1; }
		cur=${cur:+$cur:}$v
	done
	if [ "$cur" != "0x80:0x60:$icl:0x03" ]; then
		echo "re-applying (was $cur)"
		apply || exit 1
	fi
	sleep 5
done
