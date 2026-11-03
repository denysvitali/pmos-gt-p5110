# pmos-galaxy

postmarketOS on the Samsung Galaxy Tab 2 10.1 Wi-Fi (`GT-P5110`,
`samsung-espresso10`, TI OMAP4430) with a mainline kernel, aiming for SGX540
GPU acceleration.

- [Recon and plan](docs/recon.md)
- [Working rules](AGENTS.md)
- Pinned sources: `sources.lock.json`

## CI

`build` runs on every push: shellcheck, then a pinned pmbootstrap builds
the `samsung-espresso10` image and uploads `*.img.xz` + `SHA256SUMS`.
Manual runs accept a UI and a list of packages to rebuild from source.

Without the `PMOS_PASSWORD` secret, images use the dev password `147147`.

Local equivalent (on a machine with disk, not the Pi):

```sh
PMOS_PASSWORD=... scripts/ci/build.sh /tmp/pmos-work out
```

## Flash (after backups — see AGENTS.md)

```sh
heimdall flash --KERNEL <boot image from artifact>
heimdall flash --FACTORYFS <rootfs image from artifact>
```
