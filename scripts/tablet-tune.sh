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

# Kiosk display: no lock, no idle blank/dim. A locked GNOME Mobile re-blanks
# 10 s after every wake, which looked like "screen never turns on".
mkdir -p /etc/dconf/profile /etc/dconf/db/local.d
[ -e /etc/dconf/profile/user ] || printf 'user-db:user\nsystem-db:local\n' > /etc/dconf/profile/user
grep -q '^system-db:local$' /etc/dconf/profile/user || { echo "ERROR: dconf profile lacks system-db:local" >&2; exit 1; }
cat > /etc/dconf/db/local.d/90-pmos-galaxy-kiosk <<'KIOSK'
[org/gnome/desktop/session]
idle-delay=uint32 0

[org/gnome/desktop/screensaver]
lock-enabled=false
idle-activation-enabled=false

[org/gnome/settings-daemon/plugins/power]
idle-dim=false
sleep-inactive-ac-type='nothing'
sleep-inactive-battery-type='nothing'
KIOSK
dconf update

# brcmfmac with Wi-Fi power save on: ~64 KB/s to the internet; off: ~4 MB/s
# (docs/bringup.md B4). 2 = disable.
mkdir -p /etc/NetworkManager/conf.d
printf '[connection]\nwifi.powersave=2\n' > /etc/NetworkManager/conf.d/90-pmos-galaxy-wifi-powersave.conf

echo "OK: max freq $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq) kHz; autologin set (applies on next gdm start)"
