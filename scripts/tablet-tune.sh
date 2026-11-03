#!/bin/sh
# Apply the kiosk/performance settings established in docs/bringup.md (B2) to a
# running tablet. Idempotent. Run on the tablet as root.
set -eu

# Earlier 800 MHz cap was a wrong diagnosis (see docs/bringup.md); remove it.
rm -f /etc/tmpfiles.d/90-pmos-galaxy-cpufreq.conf
for c in /sys/devices/system/cpu/cpu*/cpufreq; do
	cat "$c/cpuinfo_max_freq" > "$c/scaling_max_freq"
done

# CPU hogs with no kiosk value: file indexer, colour management.
systemctl --global mask \
	localsearch-3.service localsearch-control-3.service localsearch-writeback-3.service \
	org.gnome.SettingsDaemon.Color.service

# Never suspend: it drops the USB gadget and a kiosk must stay up.
systemctl mask sleep.target suspend.target hibernate.target \
	hybrid-sleep.target suspend-then-hibernate.target

# Autologin skips the GDM greeter, a second full GNOME stack resident in RAM.
conf=/etc/gdm/custom.conf
if ! grep -q '^AutomaticLoginEnable=True' "$conf"; then
	grep -q '^\[daemon\]$' "$conf" || { echo "ERROR: no [daemon] in $conf" >&2; exit 1; }
	sed -i 's/^\[daemon\]$/[daemon]\nAutomaticLoginEnable=True\nAutomaticLogin=user/' "$conf"
fi
grep -q '^AutomaticLogin=user$' "$conf" || { echo "ERROR: autologin not set" >&2; exit 1; }

echo "OK: max freq $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq) kHz; autologin set (applies on next gdm start)"
