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

## B8 — one-shot test boot through RECOVERY, 2026-11-19

- Mainline has no reboot-mode support for espresso (no DT node,
  `SYSCON/NVMEM_REBOOT_MODE` off). The stock kernel (`sec_common.c`) writes
  the reason to the SAR scratchpad: `0x4A326FF8 = REBOOTMODE_RECOVERY (0x2)`,
  `0x4A326FF4 = "RESE"`, `0x4A326FE8 = 0x12345672`
  [verified, source]. `scripts/samsung-reboot-mode.py recovery` sets them from
  userspace through /dev/mem.
- Positive control, no flash writes: after `recovery` + `systemctl reboot`, SBL
  booted CWM from RECOVERY (`adb devices`: `c1607dbef487040 recovery`, CWM
  kernel 3.0.31, `ro.bootmode=reboot_recovery`). `adb reboot` from CWM was back
  in pmOS 7.0.5 within 80 s [verified]. After that boot the scratchpad held its
  pre-test values again, so SBL rewrites them each boot (one-shot)
  [inferred].
- `espresso-charger` came up by itself after the reboot [verified].
- `scripts/stage-test-kernel.sh` builds a boot image for a test kernel without
  touching /boot, KERNEL or the installed package. mkinitfs 2.11 keeps its
  archives only through boot-deploy (with `-no-bootdeploy` they are dropped,
  [verified, control run]). The real boot-deploy would flash KERNEL, so a stub
  boot-deploy first in PATH copies them out. Header parameters are copied from
  /boot/boot.img and compared field by field.
- r52 staged: `boot-test.img` sha256 3a5cb45b…, 7 315 456 bytes. Written to
  RECOVERY (mmcblk1p6, was the CWM image 6f43b8d1…), readback identical.
  KERNEL still ae4817a1…, /boot unchanged [verified]. CWM can be restored
  from `scratch/backup-20261103/RECOVERY.img`.
- Test boot of r52 from RECOVERY (09:15): the 7.0.5 system shut down
  (USB disconnect ~2 min after the reboot). After that, nothing on USB for
  11+ min: no pmOS gadget, no Wi-Fi [verified, Pi dmesg/ping]. Whether SBL
  started the RECOVERY image, and where 7.1.5 stopped: not known. No console
  (cmdline `quiet splash`, PANIC_TIMEOUT=0, no pstore). Next: the screen
  state, then a manual power-off, then boot 7.0.5 from KERNEL.
- Cause found from the test boot's own journal (boot -1, 09:20:43, kernel
  `7.1.5 #53`): the kernel booted and reached systemd, then `/boot: unknown
  filesystem type 'ext2'` failed local-fs.target, so emergency mode ran with no
  console. The initramfs gadget had no UDC and musb came up as host, so there was
  no USB networking; Wi-Fi never started. This was not the charger patches or the
  config trim: r52's `modules.alias` is empty and `modules.dep` lists no
  dependencies [verified]. Reproduced: busybox `depmod` on the same modules gives
  0 aliases / 126 dependency-less lines, kmod `depmod` gives 1085 aliases with
  `fs-ext2`, `usbfunc:ncm` and `summit,smb347` [verified]. The APKBUILD lacked
  `kmod`, so `modules_install` used busybox depmod, which cannot read .ko.zst.
  Fix (pkgrel 53): `kmod` in makedepends, plus a package() check that fails the
  build without the `fs-ext2` alias. `stage-test-kernel.sh` also re-runs depmod.
- Back on 7.0.5 after a manual power-off; KERNEL unchanged [verified].

## B9 — r53 test boot: 7.1.5 runs, charger driver works, 2026-11-19

Booted once from RECOVERY (`boot-test.img` a7891b82…, kernel `7.1.5 #54`
from release packages-3195a439c861). The shipped module index has 1085
aliases [verified]. USB gadget came up 12 s after shutdown, Wi-Fi
within 3 min, GNOME active, system `degraded` (only plymouth-start failed)
[verified].

Charger (0013/0014), on hardware [verified]:
- `smb347` bound to 4-0006, `smb347-usb` ONLINE=1, Charging, Fast.
- Registers set by the kernel driver: CMD_B=0x03 (HC), CFG_CURRENT_LIMIT=0x63
  (900 mA), CFG_PIN=0x04 (USB mode by register, SW charge control),
  CMD_A=0x02 (charging enabled, write-protect restored), STAT_C=0x05.
- `espresso-charger` saw the kernel driver and exited.
- Positive control for the re-init: forced CMD_B=0x02 by i2cset; within 8 s
  the driver logged `configuration lost (CMD_B=0x2), re-initialising`, and
  CMD_B/ICL/CFG_PIN were back to 0x03/0x63/0x04.
- Battery status now reports (not "Unknown"): `Discharging`, -250 mA at 99 %
  with GNOME starting while the charger says Charging. The net under GUI load
  on the Pi 4 port is not yet measured at steady state.
- Not tested: physical re-plug / VBUS loss with the kernel driver (the
  forced-register test covers the same detection path) [inferred].

GPU (not working yet) [verified, dmesg]: `pvrsrvkm` 56000000.gpu probe fails.
`vmalloc_node_range for size 33558528 failed: Address range restricted to
0xf6000000 - 0xff800000`, then `SysLocateDevices: Failed to map SGX registers`.
The error path WARNs in `drm_client_sysrq_unregister` (no crash). SBL's cmdline
carries `vmalloc=144m`, and SBL drops the boot.img cmdline (no `pmos_*` args in
/proc/cmdline). A larger vmalloc= needs a kernel-side cmdline
(CONFIG_CMDLINE_EXTEND or similar) [inferred, untested].
- Made permanent: backups of /usr/lib/modules/7.0.5 and /boot to
  /var/backup/k705 on the tablet (sha256 in SHA256SUMS there). Then
  `apk add --allow-untrusted` of the r53 file (pmbootstrap's CI key is not
  trusted on the device): 7.0.5-r2 -> 7.1.5-r53, world entry pinned by checksum.
  boot-deploy flashed KERNEL; KERNEL == /boot/boot.img == the tested
  RECOVERY image (sha256 a7891b82…, mkinitfs is deterministic). A normal reboot
  from KERNEL runs 7.1.5 #54: smb347 bound, Charging, GNOME active, espresso-charger
  exits [verified]. RECOVERY still holds the test slot (CWM backup on the Pi).

## B10 — GPU: vmalloc for the SGX register mapping, 2026-11-19

- pvrsgx vmallocs the whole `gpu@0` resource (`reg = <0x0 0x2000000>`, 32 MiB
  + 4 KiB). The vmalloc window is 152 MiB (`VmallocTotal`, from SBL's
  `vmalloc=144m`) with 55 MiB in use [verified, sizes in /proc/vmallocinfo].
  The free-gap layout could not be measured: /proc/vmallocinfo addresses are
  hashed even with kptr_restrict=0 [verified], so that measurement was dropped.
- r54: `CONFIG_CMDLINE="vmalloc=256m"` + `CMDLINE_EXTEND`. The ATAG cmdline from
  SBL lands in DT bootargs; EXTEND appends ours, and vmalloc= is last-wins
  [inferred from source, untested]. The old `console=tty0 ignore_loglevel`
  default was unused (FROM_BOOTLOADER, SBL always passes a cmdline) and is
  dropped.
- r54 test boot from RECOVERY: r54 zImage+DTB (DTB byte-identical to r53),
  ramdisk and modules = installed r53 [verified, image built from
  /boot/initramfs]. Results [verified]:
  - /proc/cmdline has SBL's `vmalloc=144m` (arg 15) and ours `vmalloc=256m`
    (arg 25), last wins: `VmallocTotal` 270336 kB (was 155648), LowTotal
    737652 kB (was 852340).
  - **pvrsrvkm probes**: no `vmalloc_node_range … failed` / `Failed to map
    SGX registers`. `/dev/dri/card1` + `renderD129` exist,
    `pvrsrvkm_omap4_sgx540_120` loaded, refcount 9.
  - Wi-Fi absent: cfg80211 (r53) refused, `failed to validate module
    [cfg80211] BTF: -22` (DEBUG_INFO_BTF_MODULES=y, no
    MODULE_ALLOW_BTF_MISMATCH). This comes from mixing r53 modules with the r54
    kernel, not a defect of r54 [inferred]. Boot stuck only on time-sync (no network).
  - A normal reboot returned to r53 from KERNEL (Wi-Fi, Charging) [verified].
- Lesson: a kernel-only A/B test with the other build's modules is invalid for
  BTF modules. Test kernel + modules from the same build.
- r54 installed (7.1.5-r53 -> r54, `apk add --allow-untrusted`). Backups:
  /var/backup/r53/{boot.img,linux-…-r53.apk} on the tablet, and
  `scratch/kernel-part/boot-r53.img` on the Pi (sha256 a7891b82…). KERNEL ==
  /boot/boot.img (6f0d85a4…), and its kernel+DTB are identical to the tested r54
  test image. Normal boot from KERNEL [verified]: `7.1.5 #55`, Wi-Fi in 90 s
  (brcmfmac loaded: matched modules, no BTF/taint messages), VmallocTotal
  270336 kB, `pvrsrvkm_omap4_sgx540_120` loaded with card1/renderD129, smb347
  Charging. Only plymouth-start failed (as on 7.0.5/r53).
- GPU kernel side done. Userspace (sgx-ddk-um blobs, mesa-pvr-dri-classic in
  pmaports-overlay/pvr) has not been built or installed yet. GNOME still renders
  in software.

## B11 — SGX userspace build

- CI log channel: job logs and artifacts need a token even on a public repo, so a
  failed push to main now publishes its build log as prerelease `ci-log-<sha>`.
- First userspace build (240d32a/1fc98fd): `mesa-pvr-dri-classic: dependency
  not found: libglvnd`. No libglvnd in Alpine edge main/community/testing or pmOS
  main for armv7. Positive control: `mesa 26.3.0` found in the same parsed edge
  main index [verified]. Fix: no libglvnd, `-Dglvnd=disabled`. The package then
  owns libEGL/libGLES/libGL, so it provides and replaces mesa-egl, mesa-gl and
  mesa-gles as well (pkgrel 51).
- Second attempt (f48d618) got past dependency resolution (libglvnd fix OK),
  then failed installing the armv7 buildroot: `I/O error` / `Connection reset by
  peer` from the mirror, apk exit 10 [verified, ci-log]. A transient failure:
  build.sh now retries a package build up to 3 times.
- Third/fourth attempt: deterministic, 3/3 retries: `systemd-262.2-r0: failed to
  commit usr/sbin/init: No such file or directory` (and udevadm) while
  installing makedepends into chroot_buildroot_armv7. Both pmaports pins have
  `supported_bin_merge=True`, so the pin bump hypothesis was refuted [verified].
  The buildroot is bin-merged as designed (`/sbin -> usr/bin`, `/usr/sbin ->
  bin`) [verified, CI layout dump], and the systemd 262.2 apk contains both
  `sbin/init` and `usr/sbin/init` [verified, file list], which both resolve to
  usr/bin/init. apk-tools 3.0.10. Workaround: build.sh sets
  `supported_bin_merge=False` in the build's pmaports.cfg (asserted old/new).
- The `supported_bin_merge=False` workaround did not work: the buildroot was
  still bin-merged (CI layout dump) and the same error came back [verified]. The
  layout does not come from that pmaports.cfg switch [inferred: from base
  packages]. Dropped. Instead, packages-only builds use
  `pmb config service_manager openrc`, so no systemd is pulled into the buildroot.
  Built objects link libudev.so.1 etc. by soname, which should run on the
  systemd device [inferred, to be checked on device]. Images stay systemd.
- Userspace built (fa154a4, 8 min): release `packages-fa154a42c1a5` with
  mesa-pvr-dri-classic 21.3.9-r51, sgx-ddk-um 1.17.4948957-r50 (+ti443x).
  Runtime deps are sonames only (no eudev by name); sgx-ddk-um needs gcompat
  [verified, .PKGINFO].
- Installed on the tablet (`apk add --allow-untrusted`, one transaction): mesa,
  mesa-{gl,egl,gles,dri-gallium,gbm} 26.1.6 removed, mesa-pvr + gcompat + sgx-ddk-um(+ti443x)
  installed, llvm22-libs/spirv-tools dropped. Prior package set:
  /var/backup/r53/apk-installed-before-pvr.txt. Rollback: `apk add mesa
  mesa-egl mesa-gl mesa-gles mesa-dri-gallium mesa-gbm` from the repo.
- `pvrsrvinit`: `PVR_K: UM DDK-(4948957) and KM DDK-(4948957) match. [ OK ]`;
  the unit is enabled through its preset [verified].
- **GLES on the SGX540** [verified]: `eglinfo -p gbm` → EGL vendor "Mesa Project
  (Amber)", GLES vendor Imagination Technologies, renderer **PowerVR SGX 540**,
  "OpenGL ES 2.0 build 1.17@4948957". This was before any compositor used it;
  GNOME still ran on the old (deleted, still mapped) mesa 26 libraries.
- **GNOME on the SGX540** [verified]: after `systemctl restart gdm` the greeter
  gnome-shell (`--mode=gdm`) mapped `pvr_dri.so`, `libGLESv2_PVR_MESA`,
  `libsrv_um` and `/dev/dri/renderD129`. mutter: "Created gbm renderer for
  /dev/dri/card0" (omapdrm scanout; card1 is render-only: "No suitable mode
  setting backend", expected). After a cold boot: pvrsrvinit active at boot, the
  autologin `gnome-shell --mode=user` maps the PVR libraries, panel DPMS On.
  Steady state 1 min after boot: CPU 85 % idle, gnome-shell 0–1 % (was ~21 %
  with software rendering, B2). Battery Full, +54 mA.
- Not verified: what the screen shows (no camera). One
  `gnome-session-init-worker` SIGABRT coredump from the gdm restart (16:22:16);
  none after the cold boot was checked yet.

## B12 — performance survey on r54 + PVR, 2026-11-19

Idle after boot [verified]: load 0.02, 577 MiB available, zram swap (zstd,
1.4 GiB) active, cpufreq ondemand 300–1008 MHz, 63.5 °C. Boot: 20.4 s kernel
(incl. initramfs) + 40.4 s userspace, graphical.target at 32 s.
Gaps in the kernel log [verified, dmesg]:
- 2.6 s extracting the 10 MB gzip initramfs-extra.
- 5.6 s + 4.2 s in initramfs stage 2 (root lookup; `/dev/input` not yet present).
- 4.6 s `brcmf_p2p_create_p2pdev: timeout occurred` (the P2P iface create fails,
  err=-5), which delays Wi-Fi.
- `systemd-time-wait-sync` 32.8 s (NTP), not on the graphical path.

**EGL Wayland clients cannot present** [verified]: `es2gears_wayland` (with the
session env, MESA_LOADER_DRIVER_OVERRIDE=pvr confirmed in the user manager) is
killed: `wl_display.error 7: failed to import supplied dmabufs: Arguments are
inconsistent` (EGL_BAD_MATCH). WAYLAND_DEBUG: the compositor advertises
`wl_drm.device(/dev/dri/renderD128)` (omapdrm) and only implicit modifiers. The
client sends one plane XR24 300x300 stride 1216 mod INVALID via create_immed.
So mutter's PVR EGL rejects the dmabuf the client's PVR stack produced. GTK4 apps
(gnome-control-center) start without errors. Presumed: GTK4's GL renderer needs
GLES ≥ 3.0 and falls back to cairo [inferred]. So: compositor accelerated, EGL
clients broken, GTK clients software-rendered.

## B13 — power button and "screen off", 2026-11-19

- No power-key input device: only gpio-keys, touchscreen, vibrator
  [verified]. `INPUT_TWL4030_PWRBUTTON=y` and the driver supports
  `ti,twl6030-pwrbutton`, but espresso's PMIC is a TWL6032 (`twl6032.dtsi`), and
  overlay 0003 only patched `twl6030.dtsi` [verified, DT + source].
- Test (DTB edited with dtc, r54 kernel, RECOVERY one-shot): `twl4030_pwrbutton`
  input0/event0 appears; presses raise its IRQ (/proc/interrupts 0→8→12)
  [verified]. STS_HW_CONDITIONS (0x48:0x21) idles at 0x3b, so bit 0 (STS_PWRON) is 1
  while released [verified]. The driver reports KEY_POWER = bit 0, so the key looked
  held down whenever released. gnome-shell logged `_powerButtonEvent` ~36/s
  (19 658 since 18:44; gnome-shell 8 % + journald 4 % CPU) [verified]. That is
  inverted polarity [inferred from idle value + autorepeat; the pressed value was not
  captured]. A normal reboot back to r54 stopped it (0 events).
- Fix `0016` (pkgrel 55): `ti,twl6032-pwrbutton` compatible with active-low
  STS_PWRON, node in twl6032.dtsi. twl6030 boards are unchanged.
- The "dark screen" was brightness 14 % in GNOME, i.e. level 1/7 = 4/255 PWM
  (DT brightness-levels 0 4 8 16 32 64 128 255) [verified]. Set to 80 % through
  gsd-power (level 6/7). It persists across reboot (systemd-backlight) [verified].
- r55 installed (backup /var/backup/r54 + scratch/kernel-part/boot-r54.img).
  Idle: pwrbutton present, 0 IRQs, 0 events. Two presses [verified]:
  `KEY_POWER 1` with STS_HW_CONDITIONS 0x3b→0x38, `KEY_POWER 0` with 0x38→0x3b,
  one down/up per press, no repeats. Active-low STS_PWRON is confirmed, so 0016
  is correct.
- The screen did not turn off because of GNOME: `POWERMANAGER: doing system action
  'suspend' (reason 'power key released')`, and sleep targets are masked. gnome-shell
  (GNOME Mobile) powerButtonEvent maps `power-button-action='nothing'` to
  'blank' (phones are forced to blank; this tablet is not "isPhone") [verified, JS
  in libshell-16.so]. Set to 'nothing' (user + dconf default in tablet-tune.sh).
- tablet-tune.sh's charger check now accepts the bound kernel smb347 driver
  (espresso-charger exits by design with 0013).
- Confirmed by the user and the journal: press → `doing system action 'blank'
  (reason 'power key released')` + `_turnOffScreen()`, next press → `cancelling
  action 'blank'` + `_turnOnScreen()` [verified, 20:52].

## B14 — RECOVERY as the B kernel slot, 2026-11-19

- A = KERNEL (normal boot), B = RECOVERY (one-shot via the SBL reboot reason,
  B8). B is the previous known-good kernel. `scripts/kernel-ab.sh install <apk>`
  saves the running A image and its modules, upgrades the package (boot-deploy
  flashes A), writes the saved image to B (readback checked), and keeps B's
  module tree installed. `kernel-ab boot-b` boots B once.
- That needs a unique kernel release per build, so that two module trees can
  coexist. r56 stamps `CONFIG_LOCALVERSION="-r$pkgrel"` in prepare() (7.1.5-r56).
  Earlier builds were all `7.1.5`, and mixing r54 kernel + r53 modules failed BTF
  validation (B10).
- CWM stays out of RECOVERY by user choice. Its backup is
  scratch/backup-20261103/RECOVERY.img.

## B15 — EGL Wayland clients on PVR: explicit MOD_INVALID, 2026-11-19

- Reproduced the import outside mutter (`/var/tmp/dmabuf-test.c` on the tablet:
  GBM BO on card0 with pvr, dmabuf export, eglCreateImageKHR
  EGL_LINUX_DMA_BUF_EXT) [verified]:
  XR24 300x300 stride 1216 with no modifier attributes: **OK**. Same buffer with
  explicit LINEAR or explicit DRM_FORMAT_MOD_INVALID: **EGL_BAD_MATCH**
  (0x3009). AR24 OK. eglQueryDmaBufFormatsEXT lists XR24.
- mutter 48 `meta-wayland-dma-buf.c` always passes a non-NULL modifiers array
  (= dma_buf->drm_modifier, also when INVALID), and `meta_egl_create_dmabuf_image`
  then adds MODIFIER_LO/HI [verified, mutter 48.0 source]. Mesa 21.3
  `dri2_create_image_dma_buf` routes any present modifier to
  createImageFromDmaBufs2, which fails on pvr [verified, mesa-pvr source].
- mesa-pvr patch (r52): treat an explicit DRM_FORMAT_MOD_INVALID as no modifier.
  Result [verified]: the import now succeeds, then **gnome-shell SIGSEGV** in the
  closed blob: `#0 libGLESv2_PVR_MESA.so + 0x16e6c` (SEGV_MAPERR), 22:47:42,
  during es2gears_wayland. GDM fell back to the greeter. That is worse than r51 (only
  the client was killed), so the tablet went back to r51 and the patch was removed
  (pkgrel 53 = r51 behaviour).
- So EGL clients fail in two layers: mutter's explicit-modifier import (fixable)
  and then the SGX blob crashing on the imported client texture (closed code,
  not debuggable from here). Options: a compositor that doesn't import
  client dmabufs into PVR GLES (e.g. shm/wl_drm path, or a weston/cage kiosk), or
  clients that use shm (software) buffers [inferred].
- Boot-time items, investigated, not changed:
  - Wi-Fi P2P timeout (4.6 s, Wi-Fi only): wpa_supplicant runs `-u` (D-Bus)
    with no config, `p2p_disabled` is per-interface, and NetworkManager recreates
    the interface over D-Bus, so no global switch [reported, unix.stackexchange
    765116]. Left as known.
  - initramfs-extra compression (2.6 s gunzip): the initramfs'
    `extract_initramfs_extra()` hardcodes `gzip -d -c … | cpio -iu` [verified,
    init_functions.sh]. lz4/none would halt boot. The boot.img initramfs cannot
    grow either (7.56 of 8 MiB). Dropped.
  - systemd-time-wait-sync (32.8 s) is not on the graphical path.
- Hunting the gnome-shell crash (r52 + gdb) [verified unless noted]:
  - Fault: `ldr.w r8,[r5,#12]; ldr.w r3,[r8]; ldr r2,[r3,#4]` with r3=NULL,
    blob +0x16e6c. A stack scan gives libmutter-cogl (near
    `_cogl_framebuffer_flush_journal`) ← libmutter (near `meta_shaped_texture_*`)
    ← clutter paint ← gjs.
  - gdb dprintf trace of mutter's GL calls: LINEAR filters, CLAMP_TO_EDGE, no
    glGenerateMipmap. The last calls before the SIGSEGV were `glDeleteTextures` x2.
  - Standalone repros (`/var/tmp/dmabuf-render2.c`, `dmabuf-delete.c`): cross-process
    GL-rendered client buffers, 2-buffer reuse, 300x300 NPOT, image destroyed
    after binding, texture or scissor FBO, deleting textures (also while a draw is
    in flight): **no crash, correct colours**. A separate blob bug turned up:
    `glGenerateMipmap` on any EGLImage texture segfaults at +0x14662 (not mutter's
    site).
  - Not reproduced outside mutter. Next approach: keep client buffers off mutter's
    linux-dmabuf path entirely.
- mesa-pvr r54: no `EGL_EXT_image_dma_buf_import_modifiers`. mutter's
  `meta_wayland_dma_buf_manager_new` returns NULL without it, so no
  zwp_linux_dmabuf_v1 global, and Mesa clients fall back to wl_drm
  (EGL_WAYLAND_BUFFER_WL import) [verified, mutter source]. Untested.
- mesa-pvr r54 (no modifiers ext) on hardware: no linux-dmabuf global; es2gears
  presents through wl_drm, 601 commits / 12 s, no errors [verified]. A second run
  crashed gnome-shell at the same blob site (+0x16e6c) [verified]. So the import path
  is not the cause.
- Full GL-call tracing (dprintf on all 142 blob GL entry points): no crash in 3
  client runs. It is timing-dependent [verified]. At the first crash the KMS thread
  was idle in poll(), so no evidence for a KMS-thread race [verified, thread
  dump]. `MUTTER_DEBUG_KMS_THREAD_TYPE=user` (KMS on the main thread,
  meta-thread.c wrap_main_context) avoided the crash in 3x40 s but broke the frame
  clock (`clutter_frame_clock_notify_ready: code should not be reached`, a frame
  every ~3 s for the whole UI) [verified]. Reverted.
- The drawn-after-client-exit, stale-name, delete-in-flight repros also do not crash.
  The GNOME (mutter + SGX blob) crash stays unexplained.
- Kiosk compositor route (B1 recon target): weston 14 refuses to start: `GL_EXT_unpack_subimage
  not available` → `failed to initialize egl` [verified; libseat needs seatd,
  no builtin backend]. cage 0.3.0 links wlroots 0.20, which also hard-requires
  unpack_subimage. PVRports patched wlroots 0.19 inside phoc. Ported to 0.20.2 as
  overlay `wlroots0.20` r50 (my own row-upload fallback; PVRports' fallback read an
  uninitialised pointer), plus their BGRA read quirk. EGL_KHR_platform_gbm is present
  [verified], so their MESA_platform_gbm patch is not needed.
- cage with patched wlroots: the GLES2 renderer starts on the SGX540 (`uploading
  rows`, BGRA quirk) [verified]. Then the swapchain test fails with atomic KMS
  (WLR_DRM_NO_ATOMIC=1 fixes that), and cage aborts on `Assertion failed:
  scale > 0 (wlr_surface_set_preferred_buffer_scale)`. wlroots logs `@ inf Hz`
  for a mode drm_info shows as 59.74 Hz [verified]. Refuted: FPU state after PVR
  init, float ABI (all VFP-args), d8-d15 preservation across blob calls, libm
  interposition by the blobs, ABI drift from Alpine's extra wlroots patch
  [all verified by test]. Unexplained.
- Working configuration (mesa-pvr r55): Wayland EGL clients default to swrast +
  wl_shm (`MESA_PVR_WAYLAND_HW=1` opts back in). mutter (GBM platform) keeps
  the SGX. The no-modifiers patch (r54) was dropped.

## B16 — CI-built userspace has float constants folded to 0, 2026-11-20

- cage under gdb with wlroots0.20-dbg [verified]: the mode is correct (clock 69818,
  htotal 1408, vtotal 830 → `calculate_refresh_rate` = 59743), yet it prints
  `inf Hz`. FPSCR is normal (0x20000010: no LEN/STRIDE/RMode). `scale=0` comes
  from types/scene/surface.c:148.
- `struct wlr_output.scale` is at offset 92 (ptype /o). In `wlr_output_init`:
  - Alpine 0.20.2-r0: `mov.w r3, #1065353216 (0x3f800000); str r3, [r4, #92]`
  - our CI r50:       `movs r3, #0;                       str r3, [r4, #92]`
  The source has `.scale = 1`. **The CI build folded the float constant to 0**
  [verified, disassembly of both]. A /1000.0f constant that became 0 gives `inf`.
- Code compiled natively on the tablet (all test programs) has correct floats
  [verified]. The kernel is cross-native (no floats) and unaffected. Every
  non-cross-native CI package is suspect (wlroots0.20 r50, mesa-pvr r51–r55,
  sgx-ddk-um). The GNOME/SGX crash may share this cause [inferred, testing].
- Native rebuild on the tablet: `abuild -r` in ~/build (abuild key and group
  present).
- Native build of wlroots0.20 r50 (pmbootstrap inside the Debian chroot on the
  tablet, armv7 host, no QEMU: `/usr/local/sbin/native-pkg-build`): wlr_output_init
  stores `0x3f800000` like Alpine's build [verified]. This confirms the CI
  environment (x86_64 runner, QEMU armv7 chroot + crossdirect) miscompiles floats.
- **cage kiosk with a GPU client on the SGX540 works** (native wlroots,
  WLR_DRM_NO_ATOMIC=1, seatd) [verified]: mode `1280x800 @ 59.743 Hz`, no
  assertion, es2gears_wayland full screen (hardware EGL, mesa-pvr r51) 762 and 899
  commits in two 60 s runs (~13–15 fps), 0 protocol errors, cage rc=0.
- Native mesa-pvr build started, to test whether the GNOME crash is the same
  miscompilation.
- **Cause isolated to crossdirect** [verified]: the same CI with only
  `options="!pmb:crossdirect"` added (wlroots0.20 r51, run 44657025888, QEMU-only
  build) stores `mov.w r3,#0x3f800000; str r3,[r4,#0x5c]` in wlr_output_init.
  Control, CI r50 (crossdirect), with the same `llvm-objdump` command: `movs r3,#0x0`.
  Why crossdirect's compiler does this was not investigated [unknown].
- Fix: every overlay userspace APKBUILD carries `!pmb:crossdirect` (mesa-pvr r56,
  sgx-ddk-um r51, wlroots0.20 r51). `scripts/ci/build.sh` fails at the start if
  any overlay APKBUILD lacks it and is not `pmb:cross-native`. The guard failed on
  the unfixed mesa and sgx APKBUILDs [verified locally].
- Native mesa-pvr r55 on the tablet, GNOME, client with `MESA_PVR_WAYLAND_HW=1
  MESA_LOADER_DRIVER_OVERRIDE=pvr`: hardware EGL, then mutter rejects the import
  (`failed to import supplied dmabufs`). That is the B15 modifier problem, expected
  without the r54 patch [verified]. So r55 cannot test the blob crash. mesa-pvr r57 =
  r56 (no crossdirect) + the r54 no-modifiers patch, swrast still the default.
- A boot with native r55 left gnome-shell hung: main thread in a PVR ioctl
  (`LinuxEventObjectWait`), D-Bus unanswered. The trigger is not known
  [verified hang, unknown cause]. The next boot was responsive. `MMU_Alloc: RA_Alloc
  of VMArena failed` (an 11 MB General-heap allocation at shell start) appears
  on every boot, CI mesa included [verified].
- **The GNOME crash is not the miscompilation** [verified]: CI mesa-pvr r57 (no
  crossdirect) + sgx-ddk-um r51 + wlroots0.20 r51 installed, fresh boot, GNOME.
  Client with `MESA_PVR_WAYLAND_HW=1 MESA_LOADER_DRIVER_OVERRIDE=pvr`. Run 1:
  libGLESv2_PVR_MESA mapped in the client, 1089 commits in 20 s. Afterwards
  mutter stopped answering D-Bus. By run 3 gnome-shell was gone: SIGSEGV
  `libGLESv2_PVR_MESA.so + 0x16e6c`, the same site as B15. The only PVR_K
  allocation failure that boot was the 11 MB one at shell start (106 s), not
  at the crash (~166 s). r57 keeps swrast as the client default; the session after
  reboot is responsive.

## B17 — the gnome-shell SGX crash is an FBO/EGLImage use-after-free, 2026-11-20

- Crash decoded (core of gnome-shell, mesa r57) [verified]: `libGLESv2_PVR_MESA +
  0x16e6c` is in a surface flush (`0x16d80`) that waits on `surface->[0xc]`
  (sync info). Its data pointer `[0]` is NULL. Chain: `glDeleteFramebuffers` →
  generic name delete `0x14bc4` → FBO delete callback `0xe1a0` (checks attachment
  types 0x8d41/0x1702) → `0xe0f0` → `0xe044` → flush. Stack frames above it are in
  cogl/mutter, from a Wayland request.
- Standalone repro `scripts/pvr-tests/fbo-uaf.c`, no compositor, run twice [verified]:
  an FBO with an EGLImage (dmabuf) texture attached, rendered to, then
  - attachment kept, texture deleted, image and bo freed, FBO deleted: OK (a);
  - **detached, texture deleted, image and bo freed, FBO deleted: SIGSEGV +0x16e6c** (f);
  - **texture deleted while the FBO is bound (implicit detach): SIGSEGV +0x16e6c** (g);
  - f with a plain glTexImage2D texture: OK (h); f with the image and bo kept: OK (i).
  So the FBO keeps a pointer to the image memory's sync after the detach drops
  the reference [inferred from h/i]. A separate crash: glReadPixels from such an FBO,
  `+0x14662` (e) [verified, not addressed].
- The gnome-shell SIGSEGV when GDM stops is the same site [verified, 08:56 core].
- Fix (mesa-pvr r58, `pvr-defer-eglimage-texture-delete-until-fbo-delete.patch`): the
  GLES2 dispatch wraps glEGLImageTargetTexture2DOES, glFramebufferTexture2D(OES),
  glDeleteTextures and glDeleteFramebuffers(OES). An EGLImage texture ever
  attached to a still-live FBO of the current context is deleted only after those
  FBOs, which turns f/g into a. The logic is unit-tested on the Pi
  (`scripts/pvr-tests/fbo-defer-harness.c`, 12 checks) [verified].
- r58 on hardware (CI build; the shipped pvr_dri.so carries the wrapper's name
  literals, which r57 lacks) [verified]: repro modes a b c d h i pass, **f and g pass
  2/2 each** (both crashed before), e still crashes (separate bug, control).
  GNOME with hardware EGL clients not yet retested (battery at 14%).
- Battery: with GNOME stopped and the backlight at 6, the tablet drew −0.75 A on
  the Pi port and browned out once (boot-ROM loop ~17 min). Backlight 0 gives
  +0.40 A [verified]. SMB347 had reverted to `CMD_A=0x02 CFG_PIN=0x04` after VBUS
  loss. Rewriting the stock values changed nothing measurable [verified].
- Charger on a wall supply (r56 kernel driver), 15 %, GNOME stopped, screen off
  [verified]: the driver state (`CFG_PIN=0x04 CMD_A=0x02 CMD_B=0x03`) with the DT's
  900 mA limit (`0x01=0x63`) nets +0.38–0.41 A. With 1.8 A (`0x01=0x66`, SMB347
  `icl_tbl` index 6) it nets +0.67–0.80 A. CFG_PIN `0x60` vs `0x04` changes
  nothing at the same limit. 900 mA stays the DT value because a Pi 4 port trips
  at 1.8 A (B9). Right after the replug it netted +0.02 A; the cause is unknown.
- **r58 under GNOME: not fixed** [verified]. 8-run test with hardware EGL clients
  (wall charger, backlight 3/7). Run 1 used the GPU (1093 commits), then mutter
  stopped answering D-Bus. No coredump; the shell hung. Main thread stack: Wayland
  request → mutter → cogl → `pvr_dri.so +0x3e63` (wrapper) → blob name delete →
  FBO flush → the wait loop (`0x16e88`) → libsrv_um event wait. That is the spin
  variant of the same use-after-free: the FBO delete went through the wrapper but
  still waited on freed memory.
- Hypothesis H2 (the FBO targets a plain texture and samples the EGLImage
  texture) is refuted: repro modes j (draw sampling the image texture into a
  plain-texture FBO, then free everything), k (j + glFinish) and m (j, image
  kept) all pass, 2 runs each [verified]. mutter's real sequence is unknown;
  r59 logs it (`PVR_FBO_DEFER_DEBUG=1`).
- Kernel r57 (DT `summit,usb-current-limit-microamp = <1800000>`, checked in the
  shipped dtb with `dtc`) installed with `kernel-ab` (A=r57, B=r56), on a wall
  charger [verified]. After boot, with no manual write, the chip reads
  `0x01=0x66 CFG_PIN=0x04 CMD_A=0x02 CMD_B=0x03 STAT_C=0x05`. Net +0.76–0.81 A
  with GNOME running at backlight 3/7, 25 %. Wi-Fi up.
- r59 log of mutter's sequence with a hardware client [verified]: the client
  buffer becomes image texture 3710053, which is *never attached*. Right after,
  offscreen FBOs 1890027/1960028/2030029 are created on plain textures. At
  client exit the image texture is deleted (`img 1 attached 0`, so r58 did not
  defer it), then the FBOs. The shell hung at `delete-fbo 1890027` in one run.
  In the next it got past that and died at `delete-fbo 1960028` (SIGSEGV
  +0x16e6c). GNOME with a hardware client reproduces it in 3/3 runs.
- Standalone repros of this order do not trigger it (2/2 each) [verified]:
  `scripts/pvr-tests/mutter-seq.c` p/q (200 frames, image textures drawn into an
  offscreen that is drawn onto a stage FBO), r/s (+ a second context rendering
  into the dmabufs), and `mutter-xproc.c` t/u (a real client process that renders
  and exits). What else mutter does is unknown.
- r60 also records a reference when a draw into an FBO has an EGLImage texture
  bound on any unit (wraps glActiveTexture/glBindTexture/glBindFramebuffer/
  glDrawArrays/glDrawElements). Harness: 18 checks [verified]. Test bench: GNOME.
- r60 under GNOME: still SIGSEGV +0x16e6c at `delete-fbo 1890027` [verified]. The
  log shows the deferral worked: `ref fbo 1890027 tex 3780054 (draw)`, then
  `delete-texture 3780054 img 1 attached 1` (deferred), then `delete-fbo
  1890027`. So keeping the GL texture alive does not keep its memory alive
  [inferred: the blob texture holds no __DRIimage reference]. This fits repro i
  (image kept alive: OK) vs f (image destroyed: crash).
- r61: each image texture also holds a __DRIimage reference
  (`PVRDRIScreenGetDRIImage` + `PVRDRIRefImage`) until its real deletion. Harness:
  24 checks [verified].
- r61 under GNOME (default animations): image 3710053 was now held past
  `delete-fbo 1890027`, which no longer crashed. The SIGSEGV +0x16e6c moved to
  `delete-fbo 2030029`, an FBO with no recorded image reference [verified].
  Probably a transitive dependency: it samples another offscreen's texture
  [inferred].
- **r61 + `org.gnome.desktop.interface enable-animations false`: 8/8 pass**
  [verified]. Hardware EGL in every run, 1129–1143 commits per 20 s (~56 fps),
  same shell PID, D-Bus answering, no new coredump. The per-window offscreen
  FBOs come from the window animations.
- Leak: the long-lived FBO 350005 draws every client buffer, so r61 keeps one
  image texture (and its __DRIimage) per client run: 7 pending after 7 runs
  [verified].
- **r62 (aging after 120 draws) + animations off: 8/8 pass** [verified]. Hardware
  EGL in every run, 1105–1142 commits per 20 s, same shell PID, D-Bus answering,
  no new coredump. Leak bounded: 9 deferred, 6 released by aging, 3 pending
  (the last runs). Stopping GDM (deletes the long-lived FBO) did not crash,
  unlike before [verified].
- Not fixed: with window animations on, the per-window offscreen FBOs still
  crash on a transitive dependency (r61 run, `delete-fbo 2030029`). The kiosk
  dconf (`tablet-tune.sh`) sets `enable-animations=false`.
- Power key: each press arrives twice, from `gpio-keys` (its DT `key-power`, GPIO,
  KEY_POWER, wakeup-source) and from `twl4030_pwrbutton` (0016). The duplicate
  logind lines and the blank/cancel pairs are verified (journal); that both
  devices emit is inferred (logind watches both). A per-device capture is pending. B13's "no power-key input
  device" missed gpio-keys' own power key.
- Per-device capture (`/dev/input/event0` twl4030_pwrbutton, `event1` gpio-keys;
  3 presses) [verified]: every press and release arrives on **both**, gpio-keys
  first, the TWL6032 pwrbutton ~10 ms later. gpio-keys' DT `key-power` is the
  upstream power key (wakeup-source), so 0016 duplicated it. Dropped 0016
  (kernel r58).
- mesa-pvr r63: Wayland EGL clients use the SGX by default (user decision, with
  animations off in the kiosk). `MESA_PVR_WAYLAND_HW=0` falls back to swrast +
  wl_shm (`egl-wayland-pvr-clients-swrast-opt-out.patch`).
