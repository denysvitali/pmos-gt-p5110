# Bring-up log

## B1 — stock Nura v26.06 sxmo image, 2026-11-03

Image `20261030-1222-nura-v26.06-sxmo-de-dwm-samsung-espresso10` (sha256/sha512
checked against images.nura.eco). Written from CWM recovery with
`scripts/flash-recovery.sh`: DATAFS readback md5 `b33d1d9e…` and KERNEL
readback md5 `b556f052…` both matched [verified].

Observed after `adb reboot` (host clock):
- USB re-enumerated as `18d1:d001` "Samsung Galaxy Tab 2 (10.1 inch)", serial
  `postmarketOS`, `cdc_ncm` [verified] — the pmOS kernel and initramfs ran.
- For several minutes, DHCP from 172.16.42.1 worked; ICMP and TCP 22/23 timed out
  [verified].
- 09:01:40: mDNS advertised `samsung-espresso10.local` SSH on :22; one ICMP echo
  answered; the interface disappeared about 4 s later [verified, tcpdump].
- It re-enumerated (USB device 11): carrier up, no ICMP or SSH; USB disconnect at
  dmesg 61458 s (~09:04:28). No re-enumeration in the following 3+ min; not
  visible to adb or lsusb [verified].

Not known: SSH login (never reached), what the screen shows, and whether the
tablet is off, hung, or in a charge/bootloader screen. Candidate causes, none
tested: the gadget reconfiguring from initramfs to rootfs, followed by a hang or
power-off; a kernel panic (no auto-reboot); the reported power-off/charging
quirks [guessed].

### Correction: host firewall, not the tablet

The Pi carries `DUMPLING_INPUT -d 172.16.42.0/24 -j DROP` (the OnePlus project's
iptables chain). All inbound traffic from the tablet was dropped on the host:
tcpdump showed ICMP replies and SYN-ACKs reaching `eth1` while `ping`/`ssh` timed
out [verified]. The "no ICMP/TCP" observations above are therefore about the Pi,
not the tablet. Workaround: a non-persistent `iptables -I INPUT 1 -i eth1
-s 172.16.42.1 -m mac --mac-source <tablet MAC> -j ACCEPT`. The tablet's gadget MAC
changes every boot (26:21:e7:… then b6:f4:27:…), so the rule must be re-added each
boot.

## B1b — same image + sshd enabled, key, sxmo.nosuspend, 2026-11-03

The stock image ships `80-pmbootstrap-install-disable-sshd.preset` (sshd off)
[verified]. On the host's copy of the image we added the
`multi-user.target.wants/sshd.service` symlink, `~user/.ssh/authorized_keys`, and
`~user/.cache/sxmo/sxmo.nosuspend`; `e2fsck -fn` was clean. Flashed through recovery: DATAFS
readback md5 `c9884978…` matched [verified].

- SSH key login works; reachable at 10 min uptime [verified].
- Kernel `7.0.5 #3-postmarketOS`, Xorg + Sxmo (dwm), 988 MiB RAM, battery 87%
  with status "Unknown" [verified].
- DRM: `card0`, `card0-LVDS-1`, `renderD128`; omapdrm WARN at
  `dss_set_fck_rate` (dss.c:656) during probe [verified]. Possible link to the
  horizontal stripes in the photo [guessed].
- gp2ap002 proximity/light sensor probe fails (-121); some ti-sysc probe
  errors [verified].
- Root uses 1 GiB of the 12 GiB DATAFS; the GPT was not grown [verified, dmesg].
- Cause of the earlier USB drops: still unproven. A re-enumeration ~70 s into
  boot recurs (initramfs → rootfs gadget) [verified]. The later permanent drops
  may have been Sxmo suspend [guessed]; the journals of those boots were lost in
  the reflash.

## B2 — GNOME Mobile + Wi-Fi + native GPU build, 2026-11-03

- Wi-Fi: NM keyfile (root-only, from `/root/wifi.txt`) → `wlan0` connected,
  `192.168.30.236`, NTP fixed the clock; reachable from the Pi via
  192.168.10.1, and survives a warm reboot [verified]. The dev user has NOPASSWD
  sudo (`/etc/sudoers.d/90-pmos-galaxy-dev`).
- Root fs grown online is refused (`resize2fs: Resource busy`); the initramfs
  grew it on the next boot: `/` = 11.2G [verified].
- UI: `postmarketos-ui-gnome-mobile` installed, sxmo removed, gdm enabled,
  sleep targets masked. `gnome-shell --mode=gdm` runs on a GBM renderer
  using software rendering (zink: no Vulkan; EGL: dri2 screen fails) [verified, journal].
  Not yet seen on screen.
- GNOME/SGX compatibility (source/binary level, not run): mutter-mobile 48
  ships Cogl's GLES2 driver; it imports `gbm_bo_create_with_modifiers2`, which
  Mesa-PVR 21.3.9 exports [verified, strings/readelf/source]. mutter
  references `GL_EXT_unpack_subimage`, which PVRports had to patch around in phoc [inferred
  risk].
- Kernel: `linux-postmarketos-omap` 7.1.5-r50 + pvrsgx 1.17 building natively
  with GCC 15.2 (`abuild`, systemd unit `kbuild`, JOBS=2). Final .config:
  `SGX=y PVRSGX_1_17_4948957=y SGX_DRM=y SGX_OMAP=m`, CFI off [verified].

### B2 kernel build #1 failed: imprecise external aborts at 1008 MHz

At ~36 min, GCC (ICE "Bus error" in kernel/ptrace.c) and `as` (Bus error) died
together; dmesg shows `Unhandled fault: imprecise external abort (0x1406)` twice
at those times [verified]. Ruled out: /tmp full (0%), eMMC/ext4/OOM errors
(none in dmesg) [verified]. CPU was at 1008000 kHz (ondemand), thermal zone0 75 °C
[verified].
Mainline `omap443x.dtsi` OPPs: 300/600/800/1008 MHz at 1.025/1.2/1.313/1.375 V,
but espresso has no `cpu0-supply`, and no MPU core regulator is exposed (only
`abb_mpu`). cpufreq scales frequency without voltage [verified, DT and sysfs].
Hypothesis: undervolted at 1008 MHz [inferred]. Test: build #2 with
`scaling_max_freq=800000` on both CPUs, clean rebuild, watching for new aborts.

### "Device super slow" — measured and fixed (2026-11-03 11:25–11:40)

Before: load 8.6 on 2 cores, `/proc/pressure/cpu` some=87%, memory pressure ~0%,
410 MiB available [verified]. Consumers: kernel build 2×cc1 (~35%),
`localsearch-3` indexer 24%, software-rendered gnome-shell 21%, `gsd-color` 10–25%
(spinning), plus a second full GNOME stack: the GDM greeter stayed resident after login
[verified, top]. The CPU is capped at 800 MHz on purpose.

Applied (`scripts/tablet-tune.sh` makes it reproducible): build unit
`CPUWeight=idle IOWeight=10`; localsearch and gsd-color masked globally; GDM
autologin (no greeter); persistent 800 MHz cap via tmpfiles.d. Restarting GDM
once hit "gdm-greeter-2 not preallocated", which `systemctl stop/start gdm`
cleared. gsd-color needed `systemctl --user daemon-reload` before the mask
took effect.

After: user.slice CPU pressure 7%, 572 MiB available, one gnome-shell
(`--mode=user`), no new external aborts [verified]. Rendering is still
software-only until the SGX stack lands.

### Root cause of the aborts: secure-world RAM mapped as System RAM

- Build #2 at 800 MHz also died with imprecise external aborts (5540 s), which
  **refutes** the 1008 MHz/undervolt hypothesis [verified].
- Stock kernel (`CyanogenMod/android_kernel_samsung_espresso10` cm-10.2,
  `board-espresso.c:espresso_reserve`, `omap4_ion.c`) calls `memblock_remove()`
  on the SMC (3 MiB at `0xbfd00000`) and the secure-input heap (`tiler1d_size`,
  14 MiB at `0xbef00000`) [verified, source]. Mainline maps both as System RAM
  [verified, /proc/iomem].
- Positive control: `stress-ng --vm 2 --vm-bytes 75% --vm-method all --verify`
  hit an abort after 170 s (FSR 0x1c06) [verified].
- Fix: `reserved-memory` nodes `secure-input@bef00000` (0xe00000) and
  `smc@bfd00000` (0x300000), no-map. Tested on the running 7.0.5 kernel by
  editing `/boot/dtbs/…espresso10.dtb` + `mkinitfs` (KERNEL partition readback ==
  boot.img, fixed DTB inside). After reboot System RAM ends at `0xbeefffff`.
  The same stress run for 900 s: **0 aborts, 0 verify failures** [verified].
- Kernel overlay: `0012-ARM-dts-omap4-espresso-reserve-secure-RAM.patch`.
  Pre-fix KERNEL partition saved as `scratch/kernel-part/kernel-part-pre-dtfix.img`
  (sha256 `dbc21997…`).

## B3 — "screen doesn't turn on after boot", 2026-11-16

Running: stock kernel 7.0.5 with the secure-RAM DTB fix, GNOME Mobile, GDM
autologin, 1 d 15 h uptime [verified].
- Panel side was healthy: `card0-LVDS-1` connected, but DPMS `Off`, connector
  `disabled`, backlight `bl_power=4` (powerdown) [verified, sysfs].
- Software blanked it, not a driver fault. The gnome-shell journal shows
  `idle-blank` 1401 times this boot. The session was locked (`lock-enabled=true`,
  `idle-delay=300`). Once locked, every wake re-blanks after ~10 s
  [verified, journal].
- Wakes came from "external power" flipping true/false every 1–5 min
  [verified, journal]. The battery reports `STATUS=Unknown` and `smb347` fails
  probe (-2), so charger detection is the likely source [inferred].
- Fix: no lock, `idle-delay=0`, no idle dim, never sleep. Applied as user
  gsettings and as a system dconf db (`scripts/tablet-tune.sh`). Right after:
  DPMS `On`, `bl_power=0` [verified]. Backlight was at 1/7, set to 6 by hand
  (sysfs, not persisted).
- The earlier "screen dark after boot" reports: same cause probable, not
  proven per boot [inferred].

## B4 — tablet as the GitHub Actions runner, 2026-11-16

- The runner (v2.338.2, linux-arm, sha256 `9677d617…` matched in both the asset
  digest and the release notes) is glibc .NET, and pmOS is musl. It runs in a
  Debian armhf chroot `/srv/runner-debian` as user `runner`
  (`scripts/runner/install.sh`, unit `actions-runner.service`, CPUWeight=idle).
- A trixie chroot failed registration: `AuthenticationException … certificate
  chain: NotTimeValid`, with the clock correct and `curl` in the same chroot
  working [verified, _diag log]. With bookworm's `libssl3` 3.0.20 loaded
  (LD_LIBRARY_PATH, same chroot, same token) registration succeeded
  [verified]. Cause: the runner's .NET expects 32-bit time_t, but trixie armhf's
  `libssl3t64` uses 64-bit time_t [inferred from the paired runs].
  The chroot is therefore bookworm.
- CI job timeout raised to 720 min (native 2-core build). CI now also
  uploads the built `.apk`s, so a kernel can be installed into the running
  system without flashing a whole image.
- First jobs failed in `actions/checkout`: `InvalidOperationException: Linux
  ARM32 runners are no longer supported` [verified, Worker _diag log]. The
  workflow now uses only shell steps (git checkout with the job token).
  Outputs stay on the tablet in `/srv/runner-debian/home/runner/out/<sha>`.
- First image job failed in 7 min with git exit 128: the pmaports fetch gave
  `early EOF` after 540 s on the tablet; the Pi fetched the same commit in 20 s
  [verified]. Throughput, same chroot curl: USB to the Pi 5.9 MB/s,
  deb.debian.org over Wi-Fi 64 KB/s, Wi-Fi signal 70 % [verified]. With Wi-Fi
  power save off (NM `wifi.powersave=2`): deb.debian.org 3.95 MB/s, and the
  pmaports fetch succeeded in 39 s [verified]. Persisted in
  `scripts/tablet-tune.sh` (NM conf.d).
