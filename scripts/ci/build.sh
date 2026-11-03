#!/bin/sh
# Build a postmarketOS image for samsung-espresso10 from the pins in
# sources.lock.json, with pmaports-overlay/ copied over pmaports.
# Usage: build.sh <workdir> <outdir>
# Env: PMOS_PASSWORD (required), PMOS_UI (default: console),
#      PMOS_FORCE_BUILD (space-separated packages to rebuild from source)
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
work=${1:?usage: build.sh <workdir> <outdir>}
out=${2:?usage: build.sh <workdir> <outdir>}
: "${PMOS_PASSWORD:?PMOS_PASSWORD must be set}"
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

if [ -d "$root/pmaports-overlay" ]; then
	cp -a "$root/pmaports-overlay/." "$work/pmaports/"
	git -C "$work/pmaports" -c user.name=ci -c user.email=ci@localhost \
		commit -q -A --allow-empty -m "pmos-galaxy overlay"
	echo "overlay applied:"
	git -C "$work/pmaports" show --stat --format= HEAD
fi

pmb() {
	"$work/pmbootstrap/pmbootstrap.py" --aports "$work/pmaports" \
		--details-to-stdout -y "$@"
}

yes '' | "$work/pmbootstrap/pmbootstrap.py" --aports "$work/pmaports" \
	--details-to-stdout init
pmb config device "$device"
pmb config ui "$ui"
pmb config build_pkgs_on_install True

for pkg in ${PMOS_FORCE_BUILD:-}; do
	pmb build --force --arch armv7 "$pkg"
done

pmb install --password "$PMOS_PASSWORD"

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
(cd "$out" && sha256sum ./*.xz > SHA256SUMS && cat SHA256SUMS)
