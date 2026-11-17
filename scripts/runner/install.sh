#!/bin/sh
# Install the GitHub Actions runner on the tablet. The runner is glibc-only and
# postmarketOS is musl, so it lives in a Debian armhf chroot and runs as the
# unprivileged user "runner" with NOPASSWD sudo, which pmbootstrap needs.
# Bookworm, not trixie: the runner's .NET is built for 32-bit time_t, and
# trixie's armhf libssl3t64 uses 64-bit time_t, so every TLS handshake fails
# with NotTimeValid (docs/bringup.md B4). Run on the tablet as root.
# Usage: [RUNNER_TOKEN=<registration token>] install.sh
# The token is needed only for the first registration and is never written to disk.
set -eu

root=/srv/runner-debian
version=2.338.2
sha256=9677d6171f2c60040efc9111c4e7dd34fdd714cd7b24b171a7925eba7d09c5fa
url=https://github.com/denysvitali/pmos-gt-p5110
here=$(cd "$(dirname "$0")" && pwd)

if [ ! -x "$root/usr/bin/python3" ]; then
	apk add -q debootstrap debian-archive-keyring
	debootstrap --arch=armhf --variant=minbase \
		--include=git,python3,sudo,curl,ca-certificates,shellcheck,xz-utils,openssl,procps,kmod,kpartx,libicu72,libssl3,liblttng-ust1,libkrb5-3,zlib1g \
		bookworm "$root" http://deb.debian.org/debian
fi
grep -q '^VERSION_CODENAME=bookworm$' "$root/etc/os-release" ||
	{ echo "ERROR: $root is not Debian bookworm" >&2; exit 1; }

install -D -m 755 "$here/chroot-mounts.sh" /usr/local/sbin/runner-chroot-mounts
install -m 644 "$here/actions-runner.service" /etc/systemd/system/actions-runner.service
/usr/local/sbin/runner-chroot-mounts up

in_root() { chroot "$root" "$@"; }

in_root id runner >/dev/null 2>&1 || in_root useradd -m -s /bin/bash runner
echo 'runner ALL=(ALL) NOPASSWD: ALL' > "$root/etc/sudoers.d/90-runner"
chmod 440 "$root/etc/sudoers.d/90-runner"
cp /etc/resolv.conf "$root/etc/resolv.conf"
# sudo warns on stderr when the host name does not resolve, which breaks
# pmbootstrap parsing `sudo losetup --json` output.
grep -q "[[:space:]]$(hostname)\$" "$root/etc/hosts" ||
	printf '127.0.1.1\t%s\n' "$(hostname)" >> "$root/etc/hosts"

dir=/home/runner/actions-runner
tarball=actions-runner-linux-arm-$version.tar.gz
if [ ! -x "$root$dir/run.sh" ]; then
	in_root runuser -u runner -- sh -c "mkdir -p $dir && cd $dir &&
		curl -fsSLo $tarball https://github.com/actions/runner/releases/download/v$version/$tarball &&
		echo '$sha256  $tarball' | sha256sum -c - && tar xzf $tarball && rm $tarball"
fi

if [ ! -e "$root$dir/.runner" ]; then
	: "${RUNNER_TOKEN:?RUNNER_TOKEN must be set for the first registration}"
	in_root runuser -u runner -- sh -c "cd $dir && ./config.sh --unattended --replace \
		--url $url --name samsung-espresso10 --labels pmos-gt-p5110 \
		--work _work --token \"\$0\"" "$RUNNER_TOKEN"
fi
[ -e "$root$dir/.runner" ] || { echo "ERROR: runner not configured" >&2; exit 1; }

systemctl daemon-reload
systemctl enable --now actions-runner.service
sleep 5
systemctl is-active actions-runner.service
