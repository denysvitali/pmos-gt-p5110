#!/bin/sh
# Recover Wi-Fi when the link stops passing traffic (docs/bringup.md B18: the
# brcmfmac link flapped and stayed down for 9 h while the tablet kept running).
# Run every 2 min by wifi-watchdog.timer. Healthy: the default gateway answers.
# Consecutive failures: 1-2 -> reconnect through NetworkManager, 3 -> reload
# brcmfmac, then start over.
set -u
iface=wlan0
state=/run/wifi-watchdog.failures

log() { echo "wifi-watchdog: $*"; }

gw=$(ip route show default dev "$iface" 2>/dev/null | awk '/default/ {print $3; exit}')
if [ -n "$gw" ] && ping -c 3 -W 2 -I "$iface" "$gw" >/dev/null 2>&1; then
	[ -e "$state" ] && log "link healthy again (gateway $gw)"
	rm -f "$state"
	exit 0
fi

n=$(($(cat "$state" 2>/dev/null || echo 0) + 1))
echo "$n" > "$state"
log "no reply from gateway '${gw:-none}' on $iface (failure $n)"

if [ "$n" -lt 3 ]; then
	log "reconnecting through NetworkManager"
	nmcli device disconnect "$iface" >/dev/null 2>&1
	if ! nmcli device connect "$iface"; then
		log "ERROR: nmcli device connect $iface failed"
		exit 1
	fi
	exit 0
fi

log "reloading brcmfmac"
rm -f "$state"
if ! modprobe -r brcmfmac; then
	log "ERROR: modprobe -r brcmfmac failed"
	exit 1
fi
sleep 2
if ! modprobe brcmfmac; then
	log "ERROR: modprobe brcmfmac failed"
	exit 1
fi
