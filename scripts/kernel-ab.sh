#!/bin/sh
# A/B kernels on espresso: A = KERNEL partition (normal boot), B = RECOVERY
# partition (booted once via the SBL reboot reason). docs/bringup.md B14.
#
#   kernel-ab status
#   kernel-ab install <linux-postmarketos-omap-*.apk>
#       Save the running A (boot.img + its modules), upgrade the package (which
#       flashes A), then write the saved image to B and keep its modules
#       installed next to the new ones. B is then the previous known-good kernel.
#   kernel-ab boot-b
#       Reboot once into B. The next normal boot is A again.
#
# Modules must come from the same build as the kernel (BTF), so every kernel
# needs its own release string (APKBUILD stamps 7.1.5-rN). Run as root.
set -eu

state=/var/lib/kernel-ab
kpart=/dev/disk/by-partlabel/KERNEL
bpart=/dev/disk/by-partlabel/RECOVERY
release_file=$(ls /usr/share/kernel/*/kernel.release)
here=$(cd "$(dirname "$0")" && pwd)
reboot_mode=$(command -v samsung-reboot-mode || echo "$here/samsung-reboot-mode.py")

die() { echo "ERROR: $*" >&2; exit 1; }
sha() { sha256sum "$1" | cut -c1-16; }
# Hash the first N bytes of a partition (images are shorter than the partition).
part_sha() { head -c "$2" "$1" | sha256sum | cut -c1-16; }

write_part() { # write_part <image> <partition>
	img=$1 part=$2
	n=$(stat -c %s "$img")
	[ "$n" -le "$(blockdev --getsize64 "$part")" ] || die "$img larger than $part"
	dd if="$img" of="$part" bs=1M conv=fsync status=none
	sync
	cmp -n "$n" "$img" "$part" || die "readback of $part differs from $img"
}

status() {
	for slot in a b; do
		if [ -f "$state/$slot.info" ]; then
			. "$state/$slot.info"
			case $slot in a) part=$kpart ;; b) part=$bpart ;; esac
			now=$(part_sha "$part" "$size")
			[ "$now" = "$sha" ] && ok=ok || ok="CHANGED (now $now)"
			echo "$slot: kernel $kver, image $sha ($size bytes) $ok, modules $([ -d "/usr/lib/modules/$kver" ] && echo present || echo MISSING)"
		else
			echo "$slot: no record"
		fi
	done
	echo "running: $(uname -r)"
}

record() { # record <slot> <image> <kver>
	cat > "$state/$1.info" <<EOF
kver=$3
sha=$(sha "$2")
size=$(stat -c %s "$2")
EOF
}

install_kernel() {
	apk=${1:?usage: kernel-ab install <apk>}
	[ -f "$apk" ] || die "no such file: $apk"
	mkdir -p "$state/prev"
	old_kver=$(cat "$release_file")
	[ "$(uname -r)" = "$old_kver" ] || die "running $(uname -r), installed $old_kver: boot A first"
	n=$(stat -c %s /boot/boot.img)
	[ "$(part_sha $kpart "$n")" = "$(sha /boot/boot.img)" ] || die "KERNEL does not match /boot/boot.img"

	# Save A as the future B.
	cp /boot/boot.img "$state/prev/boot.img"
	tar -C /usr/lib/modules -cf "$state/prev/modules.tar" "$old_kver"

	apk add --allow-untrusted "$apk"
	new_kver=$(cat "$release_file")
	n=$(stat -c %s /boot/boot.img)
	[ "$(part_sha $kpart "$n")" = "$(sha /boot/boot.img)" ] || die "KERNEL not updated by the package"
	record a /boot/boot.img "$new_kver"

	# Previous A becomes B, with its modules kept installed.
	if [ "$new_kver" = "$old_kver" ]; then
		echo "WARNING: same kernel release $new_kver: B gets no modules of its own" >&2
	elif [ ! -d "/usr/lib/modules/$old_kver" ]; then
		tar -C /usr/lib/modules -xf "$state/prev/modules.tar"
		depmod -a "$old_kver"
	fi
	write_part "$state/prev/boot.img" $bpart
	record b "$state/prev/boot.img" "$old_kver"

	# Drop module trees of kernels that are neither A nor B (not package-owned).
	for d in /usr/lib/modules/7.*; do
		k=$(basename "$d")
		[ "$k" = "$new_kver" ] || [ "$k" = "$old_kver" ] && continue
		if apk info -W "$d/modules.dep" >/dev/null 2>&1; then
			continue
		fi
		echo "removing modules of retired kernel $k"
		rm -rf "$d"
	done
	status
}

boot_b() {
	[ -f "$state/b.info" ] || die "B has no record"
	. "$state/b.info"
	[ "$(part_sha $bpart "$size")" = "$sha" ] || die "RECOVERY does not hold the recorded B image"
	[ -d "/usr/lib/modules/$kver" ] || die "modules for B ($kver) missing"
	python3 -I "$reboot_mode" recovery
	sync
	systemctl reboot
}

mkdir -p "$state"
case "${1:-}" in
status) status ;;
install) shift; install_kernel "$@" ;;
boot-b) boot_b ;;
*) echo "usage: kernel-ab status|install <apk>|boot-b" >&2; exit 2 ;;
esac
