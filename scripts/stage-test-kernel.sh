#!/bin/sh
# Build a boot image for a test kernel next to the installed one, without
# touching /boot, the installed kernel package or any partition.
# Usage: stage-test-kernel.sh <linux-*.apk> <stage-dir>   (run on the tablet as root)
#
# - Adds the package's /usr/lib/modules/<kver> next to the installed modules.
# - Builds an initramfs for <kver> (mkinitfs reads the kernel version from
#   /usr/share/kernel/*/kernel.release, so that file is swapped for the call
#   and restored on exit).
# - Builds <stage-dir>/boot-test.img with the header parameters of the current
#   /boot/boot.img, kernel = zImage + espresso10 DTB from the package.
# Flash it to RECOVERY and boot it once with samsung-reboot-mode.py recovery.
set -eu

apk=${1:?usage: stage-test-kernel.sh <apk> <stage-dir>}
stage=${2:?usage: stage-test-kernel.sh <apk> <stage-dir>}
dtb_name=omap4-samsung-espresso10
release_file=$(ls /usr/share/kernel/*/kernel.release)
[ "$(echo "$release_file" | wc -l)" = 1 ] || { echo "ERROR: expected one kernel.release" >&2; exit 1; }

rm -rf "$stage"
mkdir -p "$stage/pkg" "$stage/initfs" "$stage/cur"
tar -xzf "$apk" -C "$stage/pkg" 2>/dev/null
kver=$(cat "$stage/pkg/usr/share/kernel/"*/kernel.release)
cur_kver=$(cat "$release_file")
[ -n "$kver" ] && [ "$kver" != "$cur_kver" ] ||
	{ echo "ERROR: test kernel '$kver' vs installed '$cur_kver'" >&2; exit 1; }
for f in boot/vmlinuz "boot/dtbs/$dtb_name.dtb" "usr/lib/modules/$kver/modules.dep"; do
	[ -s "$stage/pkg/$f" ] || { echo "ERROR: package lacks $f" >&2; exit 1; }
done

# Modules: additive. Refuse to replace a directory that a package owns.
mods=/usr/lib/modules/$kver
if [ -e "$mods" ] && apk info -W "$mods/modules.dep" >/dev/null 2>&1; then
	echo "ERROR: $mods belongs to an installed package" >&2
	exit 1
fi
rm -rf "$mods"
cp -a "$stage/pkg/usr/lib/modules/$kver" "$mods"
# Regenerate the index with kmod (r52 shipped a busybox-depmod index with no
# aliases, so nothing was autoloaded and /boot (ext2) failed to mount).
depmod -a "$kver"
grep -q "^alias fs-ext2 " "$mods/modules.alias" ||
	{ echo "ERROR: no module aliases for $kver after depmod" >&2; exit 1; }

# initramfs for the test kernel
cp "$release_file" "$stage/kernel.release.orig"
trap 'cp "$stage/kernel.release.orig" "$release_file"' EXIT INT TERM
echo "$kver" > "$release_file"
# mkinitfs 2.11 keeps its archives only via boot-deploy (-no-bootdeploy
# discards them). Shadow boot-deploy with a stub that copies them out: the
# real one would flash KERNEL (deviceinfo_flash_kernel_on_update). If the stub
# were bypassed, the real one stops at get_kernel: no vmlinuz in the out dir.
mkdir -p "$stage/bin"
cat > "$stage/bin/boot-deploy" <<'STUB'
#!/bin/sh
set -eu
while getopts i:k:d:o:c: o; do
	case $o in i) i=$OPTARG ;; d) d=$OPTARG ;; o) out=$OPTARG ;; *) ;; esac
done
cp "$d/$i" "$d/initramfs-extra" "$out/"
echo stub > "$out/.stub-ran"
STUB
chmod 755 "$stage/bin/boot-deploy"
PATH="$stage/bin:$PATH" mkinitfs -d "$stage/initfs"
[ -e "$stage/initfs/.stub-ran" ] || { echo "ERROR: boot-deploy stub did not run" >&2; exit 1; }
cp "$stage/kernel.release.orig" "$release_file"
trap - EXIT INT TERM
[ "$(cat "$release_file")" = "$cur_kver" ] || { echo "ERROR: kernel.release not restored" >&2; exit 1; }
[ -s "$stage/initfs/initramfs" ] || { echo "ERROR: mkinitfs produced no initramfs" >&2; exit 1; }
if gzip -dc "$stage/initfs/initramfs" | cpio -t 2>/dev/null | grep -q "lib/modules/$cur_kver/"; then
	echo "ERROR: initramfs contains $cur_kver modules" >&2
	exit 1
fi

# Boot image with the current image's header parameters.
unpackbootimg -i /boot/boot.img -o "$stage/cur" >/dev/null
p() { cat "$stage/cur/boot.img-$1"; }
cat "$stage/pkg/boot/vmlinuz" "$stage/pkg/boot/dtbs/$dtb_name.dtb" > "$stage/kernel-dtb"
mkbootimg-osm0sis \
	--kernel "$stage/kernel-dtb" \
	--ramdisk "$stage/initfs/initramfs" \
	--cmdline "$(p cmdline)" \
	--board "$(p board)" \
	--base "$(p base)" \
	--pagesize "$(p pagesize)" \
	--kernel_offset "$(p kernel_offset)" \
	--ramdisk_offset "$(p ramdisk_offset)" \
	--second_offset "$(p second_offset)" \
	--tags_offset "$(p tags_offset)" \
	--hashtype "$(p hashtype)" \
	--header_version "$(p header_version)" \
	-o "$stage/boot-test.img"

# Verify: same header parameters, our kernel+DTB and initramfs inside.
mkdir -p "$stage/new"
unpackbootimg -i "$stage/boot-test.img" -o "$stage/new" >/dev/null
for k in cmdline board base pagesize kernel_offset ramdisk_offset second_offset tags_offset hashtype header_version; do
	cmp -s "$stage/cur/boot.img-$k" "$stage/new/boot-test.img-$k" ||
		{ echo "ERROR: header $k differs" >&2; exit 1; }
done
cmp -s "$stage/kernel-dtb" "$stage/new/boot-test.img-kernel" || { echo "ERROR: kernel blob differs" >&2; exit 1; }
cmp -s "$stage/initfs/initramfs" "$stage/new/boot-test.img-ramdisk" || { echo "ERROR: ramdisk differs" >&2; exit 1; }
size=$(stat -c %s "$stage/boot-test.img")
part=$(blockdev --getsize64 /dev/disk/by-partlabel/RECOVERY)
[ "$size" -le "$part" ] || { echo "ERROR: image $size > RECOVERY $part" >&2; exit 1; }
echo "OK: $stage/boot-test.img ($size bytes, RECOVERY $part) kernel $kver"
sha256sum "$stage/boot-test.img"
