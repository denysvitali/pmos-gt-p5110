#!/bin/sh
# Build a postmarketOS image for samsung-espresso10 from the pins in
# sources.lock.json, with pmaports-overlay/ copied over pmaports.
# Usage: build.sh <workdir> <outdir>
# Env: PMOS_PASSWORD (required), PMOS_UI (default: console),
#      PMOS_FORCE_BUILD (space-separated packages to rebuild from source),
#      PMOS_SKIP_INSTALL=1 (packages only, no image)
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
work=${1:?usage: build.sh <workdir> <outdir>}
out=${2:?usage: build.sh <workdir> <outdir>}
skip_install=${PMOS_SKIP_INSTALL:-0}
[ "$skip_install" = 1 ] || : "${PMOS_PASSWORD:?PMOS_PASSWORD must be set}"
ui=${PMOS_UI:-console}
device=samsung-espresso10

pin() {
	python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))[sys.argv[2]][sys.argv[3]])' \
		"$root/sources.lock.json" "$1" "$2"
}

# fetch <name>: shallow-fetch the pinned commit and assert it
fetch() {
	dir="$work/$1"
	commit=$(pin "$1" commit)
	rm -rf "$dir"
	git init -q "$dir"
	git -C "$dir" fetch -q --depth 1 "$(pin "$1" url)" "$commit"
	git -C "$dir" checkout -q FETCH_HEAD
	got=$(git -C "$dir" rev-parse HEAD)
	if [ "$got" != "$commit" ]; then
		echo "ERROR: $1 is at $got, expected $commit" >&2
		exit 1
	fi
	echo "$1: $commit"
}

mkdir -p "$work" "$out"
fetch pmbootstrap
fetch pmaports
# pmbootstrap reads channels.cfg from <remote>/main, where <remote> must point
# at the official URL; our pin comes from a mirror. Fake both, at the pin.
git -C "$work/pmaports" remote add origin https://gitlab.postmarketos.org/postmarketOS/pmaports.git
git -C "$work/pmaports" update-ref refs/remotes/origin/main "$(pin pmaports commit)"
git -C "$work/pmaports" checkout -q -B main

# Build in a non-bin-merged root. systemd 262.2 in the binary repo ships both
# sbin/init and usr/sbin/init; with bin-merge both resolve to usr/bin/init and
# apk 3.0.10 fails ("failed to commit usr/sbin/init"). See docs/bringup.md B11.
cfg="$work/pmaports/pmaports.cfg"
grep -qx 'supported_bin_merge=True' "$cfg" ||
	{ echo "ERROR: $cfg: expected supported_bin_merge=True" >&2; exit 1; }
sed -i 's/^supported_bin_merge=True$/supported_bin_merge=False/' "$cfg"
grep -qx 'supported_bin_merge=False' "$cfg" ||
	{ echo "ERROR: $cfg: bin-merge not disabled" >&2; exit 1; }

if [ -d "$root/pmaports-overlay" ]; then
	cp -a "$root/pmaports-overlay/." "$work/pmaports/"
	git -C "$work/pmaports" add -A
	git -C "$work/pmaports" -c user.name=ci -c user.email=ci@localhost \
		commit -q --allow-empty -m "pmos-galaxy overlay"
	echo "overlay applied:"
	git -C "$work/pmaports" show --stat --format= HEAD
fi

pmb() {
	"$work/pmbootstrap/pmbootstrap.py" --aports "$work/pmaports" \
		--details-to-stdout -y "$@"
}

# Keep pmbootstrap's own log for CI (it holds the failing command's output).
save_pmb_log() {
	cp "$(pmb config work 2>/dev/null)/log.txt" "$work/pmbootstrap-log.txt" 2>/dev/null || true
}
trap save_pmb_log EXIT

yes '' | "$work/pmbootstrap/pmbootstrap.py" --aports "$work/pmaports" \
	--details-to-stdout init
pmb config device "$device"
pmb config ui "$ui"
pmb config build_pkgs_on_install True

# Mirror downloads into the build chroots fail now and then ("I/O error",
# "Connection reset by peer", apk exit 10). Retry a build a few times; the
# apk cache keeps what was already fetched. A real build error fails 3 times.
for pkg in ${PMOS_FORCE_BUILD:-}; do
	n=1
	until pmb build --force --arch armv7 "$pkg"; do
		root=$(pmb config work)/chroot_buildroot_armv7
		echo "DEBUG: buildroot layout after failed attempt $n:" >&2
		sudo ls -la "$root" "$root/usr" >&2 || true
		sudo ls -la "$root/usr/sbin" "$root/sbin" "$root/usr/bin/init" "$root/usr/sbin/init" >&2 || true
		sudo "$(pmb config work)/apk.static" --version >&2 || true
		if [ "$n" -ge 3 ]; then
			echo "ERROR: building $pkg failed $n times" >&2
			exit 1
		fi
		n=$((n + 1))
		echo "WARNING: building $pkg failed, attempt $n of 3" >&2
		sleep 30
	done
done

if [ "$skip_install" != 1 ]; then
	pmb install --password "$PMOS_PASSWORD"
fi

# Packages we built, so a kernel can be installed into an existing system.
pkgs=$(pmb config work)/packages
mkdir -p "$out/packages"
find "$pkgs" -name '*.apk' -newer "$work/pmaports/.git/HEAD" -exec cp {} "$out/packages/" \;
for pkg in ${PMOS_FORCE_BUILD:-}; do
	if ! ls "$out/packages/$pkg"-[0-9]*.apk >/dev/null 2>&1; then
		echo "ERROR: forced package $pkg not in $out/packages" >&2
		exit 1
	fi
done

if [ "$skip_install" = 1 ]; then
	(cd "$out" && sha256sum packages/*.apk > SHA256SUMS && cat SHA256SUMS)
	exit 0
fi

exp="$work/export"
rm -rf "$exp"
pmb export "$exp"
ls -l "$exp"
for f in "$exp"/*; do
	case "$f" in
	*.img) cp -L "$f" "$out/" ;;
	esac
done
if ! ls "$out"/*.img >/dev/null 2>&1; then
	echo "ERROR: export produced no images" >&2
	exit 1
fi
xz -T0 -v "$out"/*.img
(cd "$out" && sha256sum ./*.xz packages/*.apk > SHA256SUMS && cat SHA256SUMS)
