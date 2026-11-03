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
