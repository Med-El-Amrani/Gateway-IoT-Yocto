# Yocto Industrial IoT Gateway (RPi4-64)

Ce dépôt versionne **meta-iotgw** (couches et recettes spécifiques) + docs/outils.
Les layers lourds (poky, meta-openembedded, meta-raspberrypi) ne sont **pas** suivis.

## Cloner & préparer
```bash
git clone git@github.com:Med-El-Amrani/-Gateway-IoT-Yocto.git yocto-iotgw
cd yocto-iotgw
./scripts/fetch_layers.sh
source poky/oe-init-build-env build-rpi4
bitbake iotgw-image

