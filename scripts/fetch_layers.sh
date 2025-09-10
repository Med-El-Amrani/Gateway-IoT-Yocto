#!/usr/bin/env bash
set -euo pipefail
# Branch commune
BR=scarthgap

# Poky
[ -d poky ] || git clone -b "$BR" git://git.yoctoproject.org/poky poky
# meta-openembedded
[ -d meta-openembedded ] || git clone -b "$BR" https://github.com/openembedded/meta-openembedded.git meta-openembedded
# meta-raspberrypi
[ -d meta-raspberrypi ] || git clone -b "$BR" https://github.com/agherzan/meta-raspberrypi.git meta-raspberrypi

echo "OK. Pense à sourcer : source poky/oe-init-build-env build-rpi4"
