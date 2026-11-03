# Recon — 2026-11-03

Tags: **[verified]** seen on the device or fetched from the source this session;
**[inferred]** follows from verified facts; **[reported]** a third party says so,
not checked here.

## Device

| Item | Value | Tag |
|---|---|---|
| Model | Samsung Galaxy Tab 2 10.1 Wi-Fi, `GT-P5110` | verified (`ro.product.model`) |
| Android codename | `espresso10wifi`, board `piranha`, hardware `espresso10` | verified |
| pmOS codename | `samsung-espresso10` | verified (pmaports) |
| SoC | TI OMAP4430, 2x Cortex-A9, armv7 | verified (`ro.board.platform=omap4`, `armeabi-v7a`) |
| GPU | PowerVR SGX540 | verified (mainline `omap4.dtsi`: `img,powervr-sgx540`) |
| RAM | 1 GiB (`mem=512M@0x80000000 mem=512M@0xa0000000`) | verified (cmdline) |
| Current OS | CandyRom 5.1.1 (`candy5_p5110-userdebug`), CM kernel 3.0.31 | verified |
| Root | `adb root` works (userdebug, `persist.sys.root_access=3`); `su` from the shell returns rc=1 | verified |
| SELinux | permissive (`androidboot.selinux=permissive`) | verified (cmdline) |
| Battery | 99% at recon | verified |
| Serial | `c1607dbef487040` | verified |

### eMMC (`/dev/block/platform/omap/omap_hsmmc.1/by-name`) [verified]

| p | Name | Size |
|---|---|---|
| 1 | EFS | 20M — **unique per device, back up before anything** |
| 2 | SBL1 | 2M — bootloader, never write |
| 3 | SBL2 | 2M — bootloader, never write |
| 4 | PARAM | 8M |
| 5 | KERNEL | 8M — pmOS `boot.img` goes here |
| 6 | RECOVERY | 8M |
| 7 | CACHE | 700M |
| 8 | MODEM | 20M |
| 9 | FACTORYFS | 1.4G — pmOS rootfs default |
| 10 | DATAFS | 12.1G — larger rootfs option |
| 11 | HIDDEN | 512M |

Flash path: Samsung download mode (Power + Vol-Down) with `heimdall`;
`deviceinfo_flash_method="heimdall-bootimg"` [verified, pmaports].
Download mode lives in SBL, which a pmOS install does not touch [inferred].

## Mainline kernel

- `omap4-samsung-espresso10.dts` merged upstream as `16dcd2c7535e`
  (2026-03-06); first release containing it is **v7.1** (absent in v7.0)
  [verified, raw file per tag]. Latest tag at recon: v7.3.
- pmaports `device/community/device-samsung-espresso10` (pkgver 8) uses
  `linux-postmarketos-omap` **7.1.5** + 11 patches (incl. USB OTG/extcon for
  espresso) and `firmware-samsung-espresso` (BCM4330 Wi-Fi) [verified, pmaports
  `c33fd8ac`].
- Wiki status: display, touch, USB net, Wi-Fi (partial), battery work; audio,
  3D, RIL do not [reported].
  Wi-Fi may fail after a warm reboot (pmaports#1211) [reported].
  Never let the battery reach 0% — boot-loop [reported].

## GPU (the actual work)

- pmaports ships **no** SGX support; wiki points to out-of-tree PVRports
  [reported].
- PVRports `edge` (last commit 2026-03-15): `linux-openpvrsgx` pinned at
  **6.1** (tmlind fork), `sgx-ddk-um` 1.17.4948957 (non-free blobs),
  `mesa-pvr-dri-classic` 21.3.9, patched phoc [verified, APKBUILDs].
- `openpvrsgx-devgroup/linux_openpvrsgx` is active: branches `pvrsrvkm-7.1`
  (`c9c94731`, 2026-06-29) and `pvrsrvkm-7.3` (`101c687a`, 2026-10-19)
  [verified]. The 7.1 branch has `drivers/gpu/drm/pvrsgx` with
  `PVRSGX_1_17_4948957` — the same DDK version as the PVRports blobs — and
  OMAP4 `sgx_module` at 307.2 MHz [verified, source].
- SGX540 blobs reportedly lack some EGL configs, so Plasma Mobile does not
  run; phosh/weston have [reported]. For a kiosk, cage/weston + GLES2 is the
  realistic target [inferred].

## Plan

1. Baseline: build the stock pmaports `samsung-espresso10` image in CI and
   boot it unchanged (known-good before any change).
2. Kernel: add the `pvrsgx` driver from `pvrsrvkm-7.1` onto
   `linux-postmarketos-omap` 7.1.5 as an overlay package; build in CI.
3. Userspace: port `sgx-ddk-um` + `mesa-pvr-dri-classic` from PVRports into the
   overlay; target a kiosk compositor.

## Partition backup — 2026-11-03 [verified]

Read via `adb root` + `cat`; each image's SHA-256 matched the on-device
`sha256sum`. Images are in `scratch/backup-20261103/` on the Pi only (gitignored;
EFS is device-unique and must not be published).

```
3c98fba8c34d71a5ae0c9c09134492e97beb4098be6abeff192e67a575ac119b  EFS.img
b53d76e3a5462aad0ebb44fe4f2cbc890edabd273c824e6d8c13f01969791478  KERNEL.img
f3758eb7f2a32f07fdce41d54ed095ee970cee4733b61b3b69d8b11208b568ec  MODEM.img
679e5b1b4a53009b878013a160edaea49ad6e986cd6f5257dc99bbe8741998d0  PARAM.img
6f43b8d128a859e9155d4d4bce64d544e54c8025043f1971e5496e879b8c1833  RECOVERY.img
d36551538c9d409c8d899ea79a13aa9c935d3d9277aa6567b7e9fbeb94b15eb2  SBL1.img
5647f05ec18958947d32874eeb788fa396a05d0bab7c1b71f112ceb7e9b31eee  SBL2.img
```
