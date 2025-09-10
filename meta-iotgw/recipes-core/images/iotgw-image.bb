SUMMARY = "Industrial IoT Gateway (headless)"
LICENSE = "MIT"
inherit core-image sdcard_image-rpi

IMAGE_FEATURES += " ssh-server-openssh read-only-rootfs "
IMAGE_FSTYPES  += " rpi-sdimg "

IMAGE_INSTALL:append = " \
  systemd \
  wpa-supplicant \
  openssh-sshd \
  linux-firmware-bcm43455 \
  i2c-tools can-utils \
  mosquitto mosquitto-clients \
  libmodbus \
  iotgw-wifi-ssh \
  iotgwd \
"
