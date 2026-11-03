#!/bin/sh
# Apply the kiosk/performance settings established in docs/bringup.md (B2) to a
# running tablet. Idempotent. Run on the tablet as root.
set -eu

# 1008 MHz faults (imprecise external aborts): no cpu0-supply in the DT, so
# cpufreq raises the clock without raising the voltage.
install -d /etc/tmpfiles.d
cat > /etc/tmpfiles.d/90-pmos-galaxy-cpufreq.conf <<'EOF'
# pmos-galaxy: 1008 MHz OPP faults (imprecise external aborts); no cpu0-supply in DT
w /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq - - - - 800000
w /sys/devices/system/cpu/cpu1/cpufreq/scaling_max_freq - - - - 800000
EOF
systemd-tmpfiles --create /etc/tmpfiles.d/90-pmos-galaxy-cpufreq.conf

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

echo "OK: cpufreq cap $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq) kHz; autologin set (applies on next gdm start)"
