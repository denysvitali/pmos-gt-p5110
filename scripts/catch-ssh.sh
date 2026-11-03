#!/bin/sh
# Wait for the tablet's USB network, then (1) block Sxmo auto-suspend, which
# drops the USB gadget, (2) install the host SSH key, (3) save journals.
# Usage: catch-ssh.sh <seconds> <logdir>
# Uses the stock image's default password via SSH_ASKPASS until the key works.
set -u
deadline=$(($(date +%s) + ${1:?seconds}))
logs=${2:?logdir}
root=$(cd "$(dirname "$0")/.." && pwd)
key=$root/scratch/ssh/tablet
askpass=$(mktemp)
trap 'rm -f "$askpass"' EXIT
printf '#!/bin/sh\necho 147147\n' > "$askpass"
chmod 700 "$askpass"
mkdir -p "$logs"

ssh_t() {
	SSH_ASKPASS=$askpass SSH_ASKPASS_REQUIRE=force DISPLAY=:0 \
		ssh -i "$key" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
		-o ConnectTimeout=3 -o LogLevel=ERROR user@172.16.42.1 "$@"
}

while [ "$(date +%s)" -lt "$deadline" ]; do
	if [ -e /sys/class/net/eth1 ]; then
		ip link set eth1 up 2>/dev/null
		ip addr add 172.16.42.2/24 dev eth1 2>/dev/null
		if ssh_t true 2>/dev/null; then
			echo "$(date +%T) ssh up"
			ssh_t 'mkdir -p ~/.cache/sxmo && touch ~/.cache/sxmo/sxmo.nosuspend && pkill -f sxmo_autosuspend.sh; ls -l ~/.cache/sxmo/sxmo.nosuspend' || echo "WARN: suspend block failed"
			ssh_t 'mkdir -p ~/.ssh && chmod 700 ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys' < "$key.pub" || echo "WARN: key install failed"
			ssh_t 'uname -a; uptime; cat /proc/cmdline' > "$logs/info.txt" 2>&1
			ssh_t 'echo 147147 | sudo -S journalctl --list-boots --no-pager' > "$logs/boots.txt" 2>&1
			ssh_t 'echo 147147 | sudo -S journalctl -b -1 --no-pager' > "$logs/journal-prev.txt" 2>&1
			ssh_t 'echo 147147 | sudo -S journalctl -b 0 --no-pager' > "$logs/journal-cur.txt" 2>&1
			ssh_t 'echo 147147 | sudo -S dmesg' > "$logs/dmesg.txt" 2>&1
			wc -l "$logs"/*.txt
			cat "$logs/info.txt" "$logs/boots.txt"
			exit 0
		fi
	fi
	sleep 2
done
echo "$(date +%T) no ssh before deadline" >&2
exit 1
