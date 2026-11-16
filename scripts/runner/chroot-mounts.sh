#!/bin/sh
# Bind the kernel filesystems pmbootstrap needs (loop devices, /proc, /sys)
# into the runner's Debian chroot. Usage: chroot-mounts.sh up|down
set -eu
root=/srv/runner-debian

case "${1:?up|down}" in
up)
	for d in proc sys dev dev/pts; do
		mkdir -p "$root/$d"
		mountpoint -q "$root/$d" && continue
		case $d in
		proc) mount -t proc proc "$root/proc" ;;
		*) mount --rbind "/$d" "$root/$d" && mount --make-rslave "$root/$d" ;;
		esac
		mountpoint -q "$root/$d" || { echo "ERROR: $root/$d not mounted" >&2; exit 1; }
	done
	;;
down)
	for d in dev/pts dev sys proc; do
		if mountpoint -q "$root/$d"; then umount -R "$root/$d"; fi
	done
	;;
*)
	echo "usage: $0 up|down" >&2
	exit 1
	;;
esac
