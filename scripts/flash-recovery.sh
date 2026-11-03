#!/bin/sh
# Write an image to a partition through the CWM recovery's adbd, in chunks,
# verifying each chunk and the full readback. Works around old adbd, which
# unlinks the target of `adb push` and has no exec-in.
# Usage: flash-recovery.sh <image> <by-name partition>   (e.g. DATAFS, KERNEL)
set -eu

img=${1:?usage: flash-recovery.sh <image> <partition>}
part=${2:?usage: flash-recovery.sh <image> <partition>}
chunk_mib=64
tmp=/dev/pmos-chunk.bin
host_tmp=$(mktemp)
trap 'rm -f "$host_tmp"' EXIT

die() { echo "ERROR: $*" >&2; exit 1; }
sh_dev() { adb shell "$1" 2>&1 | grep -v tzdata | tr -d '\r'; }

[ "$(adb get-state)" = recovery ] || die "device is not in recovery"

dev=$(sh_dev "readlink /dev/block/platform/omap/omap_hsmmc.1/by-name/$part")
case "$dev" in /dev/block/mmcblk0p*) ;; *) die "cannot resolve $part: $dev" ;; esac
node=$(sh_dev "ls -l $dev")
case "$node" in b*) ;; *) die "$dev is not a block device: $node" ;; esac

size=$(stat -c %s "$img")
[ $((size % 1048576)) -eq 0 ] || die "image size $size is not MiB-aligned"
mib=$((size / 1048576))
part_kib=$(sh_dev "cat /proc/partitions" | awk -v p="${dev##*/}" '$4 == p { print $3 }')
[ -n "$part_kib" ] || die "$dev not in /proc/partitions"
[ $((mib * 1024)) -le "$part_kib" ] || die "image ($mib MiB) larger than $part ($part_kib KiB)"
echo "writing $img ($mib MiB) to $part = $dev"

off=0
while [ $off -lt $mib ]; do
	n=$chunk_mib
	[ $((off + n)) -le $mib ] || n=$((mib - off))
	dd if="$img" of="$host_tmp" bs=1M skip=$off count=$n status=none
	want=$(md5sum < "$host_tmp" | cut -d' ' -f1)
	adb push "$host_tmp" "$tmp" > /dev/null
	got=$(sh_dev "md5sum $tmp" | cut -d' ' -f1)
	[ "$got" = "$want" ] || die "chunk at $off MiB: pushed $got, expected $want"
	sh_dev "dd if=$tmp of=$dev bs=1048576 seek=$off conv=notrunc,fsync && rm $tmp" > /dev/null
	echo "  $((off + n))/$mib MiB"
	off=$((off + n))
done

want=$(md5sum < "$img" | cut -d' ' -f1)
got=$(sh_dev "sync; dd if=$dev bs=1048576 count=$mib 2>/dev/null | md5sum" | cut -d' ' -f1)
[ "$got" = "$want" ] || die "readback of $part is $got, expected $want"
echo "OK $part readback md5 $got"
