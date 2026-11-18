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
- Manual `build.sh` runs on the tablet (same env as CI) then hit, in order:
  invalid `git commit -A` (fixed); pmbootstrap wanting an `origin` remote with
  the official pmaports URL and `origin/main` (faked at the pin in build.sh);
  `sudo: unable to resolve host` on stderr corrupting `losetup --json`
  (hostname added to the chroot's /etc/hosts); missing `kpartx` (added)
  [verified]. pmbootstrap's `detach_all` only detaches loops backed under its
  work dir. The tablet's own root is `/dev/loop0` on `/dev/mmcblk1p10`, outside
  it [verified, pmb/install/losetup.py].

## B5 — battery ran flat during the build, 2026-11-17

- ~28 min into the native build the tablet lost power: the journal of boot -1
  ends at 03:08:44 with no shutdown or panic. The Pi then saw the OMAP4430 boot
  ROM re-enumerate every ~6 s for ~3.5 min before pmOS came back [verified,
  journal + Pi dmesg]. Root cause power loss [inferred].
- After boot: 6 %, 3.39 V, `status=Unknown`. There is no charger power_supply
  (`smb347` probe -2). On the Pi's USB port the tablet is net discharging:
  idle about -20 to -45 mA, spikes to -350/-509 mA. Backlight off made no
  measurable difference [verified, max170xx sysfs].
- The Pi's USB port cannot sustain the tablet, let alone a build. The runner
  is disabled (`systemctl disable --now actions-runner`) until the tablet is on
  a wall charger. AGENTS.md's 30 % floor applies before any build or flash.

## B6 — charger: SMB347 stuck in USB mode; fixed from userspace, 2026-11-17

- `smb347` (module) fails probe with -2: `power_supply_get_battery_info()`
  returns -ENOENT from `fwnode_find_reference("monitored-battery")`, and
  `smb347_get_battery_info()` only tolerates -ENXIO/-ENODEV. The espresso DT has
  no battery node [verified, mainline source + running DT].
- Even when bound, mainline smb347 never writes `CMD_B` (USB1/USB5/HC) or the
  "USB mode by register" bit in `CFG_PIN`. The stock driver
  (cm-10.2 `drivers/power/smb347_charger.c`) does both: `CFG_PIN=0x60`, and
  `CMD_B=0x03` (HC) for a wall charger, `0x02` (USB5) for USB, `0x00` (USB1)
  otherwise [verified, source].
- Registers left by the bootloader: `CMD_B=0x02` (USB5, 500 mA),
  `STAT_C=0x05` (fast charge), net battery -35 mA on the Pi 4 port. Changing
  the input limit (0x01) in USB5 mode: no effect [verified]. HC mode with a
  1.8 A limit: +580 mA, then the Pi 4 logged `over-current change` and cut
  VBUS. The chip then reloaded its OTP defaults (`CFG_PIN=0x78`: mode by pin,
  `CMD_A=0x00`), and `CMD_B` writes no longer took effect [verified].
- Stock-like `CFG_PIN=0x60` + HC + 900 mA limit (0x63): +330 mA for 3 min,
  3.22 V -> 3.31 V, no over-current [verified].
- `scripts/charger/espresso-charger.sh` (systemd `espresso-charger`, installed
  by `tablet-tune.sh`) re-applies that every 5 s when lost, and exits if a kernel
  driver binds the chip. Positive control: a forced `CMD_B=0x02` was restored
  to 0x03 within 8 s [verified].
- gdm was stopped at 5 % / 3.15 V to cut load; restart it once the battery is
  above 30 %.
- Still open: a kernel fix (accept -ENOENT, or add a `simple-battery` node, plus
  HC selection by cable type through the P30 extcon from overlay 0007/0008),
  so the 900 mA Pi-port cap is not also applied on a 2 A wall charger
  [inferred design, not built].
- Kernel fix staged for 7.1.5 (pkgrel 51), not yet built or booted:
  `0013` smb347: accept -ENOENT from battery info; opt-in `summit,usb-hc-mode`
  (clears CFG_PIN USB-mode-by-pin bits 4:3, writes `CMD_B=0x03`); `CMD_B` made
  regmap-volatile and checked every 5 s; on a reload of the NV defaults, drop
  the regcache and re-run hw_init plus the IRQ setup. Also found by reading the
  source: the config registers are cached (`REGCACHE_MAPLE`), so without the
  cache drop, re-init writes after a reload would be skipped [verified,
  source]. `0014` espresso10 DT: `summit,usb-hc-mode`, 900 mA limit, and
  `power-supplies = <&smb347>` on the fuel gauge (max17042 reports "Unknown"
  when `power_supply_am_i_supplied()` fails) [verified, source]. Both patches
  apply to v7.1.5 and reproduce the edited files exactly [verified]. Compiled:
  not yet.
- When the kernel driver binds, `espresso-charger` exits (it checks
  `/sys/bus/i2c/devices/4-0006/driver`) [verified, script].
- Load test with HC at 900 mA, gdm stopped: idle +346 mA, `stress-ng --cpu 2`
  -100 mA, the same with `CPUQuota=100%` +155 mA [verified]. The runner unit now
  has `CPUQuota=100%` and refuses to start below 30 % (ExecCondition). Transient
  units `gdm-at-30` and `runner-at-30` restart GNOME and the runner at 30 %.
- Build of 647a00e started at 12:31 once the battery reached 30 % (runner-at-30
  fired, GNOME back). With one core the compile manages ~2 objects/min, which
  is at risk of the 720-min job timeout. `runner-battery-quota` (installer:
  `scripts/runner/install.sh`) sets the runner to 2 cores at >=50 % and 1 core
  below 40 %, stops it below 20 %, and starts it again at >=30 %. Verified at
  30 %: CPUQuota=100 %, running job untouched. The 50 % branch is untested.
- `Runner.Worker` died with SIGSEGV (systemd-coredump) at 12:36, 5 min into
  the image job; the build itself continued orphaned and is still compiling
  18 h later (`drivers/net` at 06:37) [verified]. Cause of the segfault unknown
  (not OOM) [verified: no OOM in dmesg]. The GitHub job result is therefore
  meaningless for this run; outputs still land in `out/<sha>`.
- 0013 compiles: `drivers/power/supply/smb347-charger.o` built on the tablet
  (GCC 15.2, 7.1.5), with no warnings or errors in the log for it
  [verified]. Behaviour on the device: untested.
- Build of 647a00e failed after ~23 h at modpost:
  `"__vmalloc_node_range_noprof" [.../pvrsrvkm_omap4_sgx540_120.ko] undefined`,
  and the same for sgx544 [verified, log]. The openpvrsgx tree exports it in
  mm/vmalloc.c (c9c94731, line 4104), but `pvrsgx-1.17.patch.xz` only
  carries the driver directory [verified]. Fix: `0015` adds the export
  (pkgrel 52). Everything before modpost, including smb347, compiled.

## B7 — repository public, hosted builds, 2026-11-18

- The repository went public. The tablet runner (`actions-runner`,
  `runner-battery-quota`) is stopped and disabled on the device. CI now runs
  on GitHub-hosted `ubuntu-24.04`: the kernel is `pmb:cross-native` (x86_64
  chroot with an armv7 cross toolchain), `PMOS_SKIP_INSTALL=1` builds packages
  only, and the result is uploaded as the `packages-<sha>` artifact.
- Hosted builds: f2dc7dc in 23 min, 20e3a85 (actions v7) and da9edd7
  passed. Release `packages-da9edd779701`:
  `linux-postmarketos-omap-7.1.5-r52.apk` (sha256 OK on the tablet). Contents
  [verified, extracted + `dtc -I dtb`]: `smb347-charger.ko` carries
  `summit,usb-hc-mode` and the re-init messages; `pvrsrvkm_omap4_sgx540_120.ko`
  present; espresso10 DTB has `summit,usb-hc-mode`, limit 0xdbba0 (900 mA), the
  fuel gauge `power-supplies` = charger phandle 0xb0, and the secure-input /
  SMC no-map reservations. Not booted.
- Rollback facts: KERNEL (mmcblk1p5) = `kernel-part-705-dtfix.img`
  (sha256 ae4817a1…), RECOVERY (mmcblk1p6) = the 2026-11-03 CWM backup
  (6f43b8d1…) [verified]. Upgrading the package replaces
  `/usr/lib/modules/7.0.5`, so restoring KERNEL alone would boot 7.0.5
  without its modules [inferred from packaging].
