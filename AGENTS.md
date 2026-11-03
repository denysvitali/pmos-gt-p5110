# Galaxy Tab 2 10.1 (GT-P5110) — postmarketOS port

## Goal

Mainline kernel + postmarketOS on `samsung-espresso10` (OMAP4430) with SGX540
GPU acceleration, for kiosk-style reuse. Findings live in `docs/recon.md`.

## Builds

- Never build on the host: it is a Raspberry Pi whose `/root` is full. Builds
  run in GitHub Actions (`.github/workflows/build.yml` → `scripts/ci/build.sh`).
- Every source is pinned in `sources.lock.json`; bump pins deliberately.
- Our package changes live in `pmaports-overlay/`, copied over pinned pmaports.
- Remote `git@github.com:denysvitali/pmos-gt-p5110.git`, pushed with the
  deploy key `/root/.ssh/pmos-gt-p5110_deploy` (set in `core.sshCommand`).

## Device access

- ADB over USB, serial `c1607dbef487040`. `adb root` gives a root shell
  (Android userdebug); `su` from the shell does not.
- Flashing: Samsung download mode (Power + Vol-Down) + `heimdall`.

## Authorization

Do without asking: read-only ADB inspection, CI builds.

Ask first: any heimdall flash or partition write, rebooting the tablet into
download mode, anything touching SBL1/SBL2/PARAM/EFS/MODEM. Before the first
flash, EFS, PARAM, KERNEL and RECOVERY must be backed up off-device and
checksummed.

## Recovery channels

Download mode lives in SBL (never written), so a bad KERNEL/FACTORYFS/DATAFS
flash is recoverable by reflashing over heimdall. Keep the battery above 30%;
the wiki reports a 0% boot-loop.
