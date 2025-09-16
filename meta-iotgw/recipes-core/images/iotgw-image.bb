# meta-iotgw/recipes-core/images/iotgw-image.bb
SUMMARY = "Industrial IoT Gateway (headless)"
LICENSE = "MIT"

inherit core-image
inherit sdcard_image-rpi

# Image features
IMAGE_FEATURES += " \
    ssh-server-openssh \
    read-only-rootfs \
"

# SD-card image for Raspberry Pi
IMAGE_FSTYPES += " rpi-sdimg "

# Userspace packages to include in the rootfs
IMAGE_INSTALL:append = " \
  ca-certificates \
  tzdata \
  systemd \
  kernel-modules \
  \
  wpa-supplicant \
  openssh-sshd \
  linux-firmware-bcm43455 \
  \
  i2c-tools \
  spidev-test \
  can-utils \
  \
  mosquitto \
  mosquitto-clients \
  libmodbus \
  libmicrohttpd \
  open62541 \
  libcoap \
  owfs \
  bluez5 \
  \
  iotgw-wifi-ssh \
  iotgwd \
"

# Keep images slim by avoiding recommended extras
BAD_RECOMMENDATIONS += "packagegroup-core-full-cmdline"
