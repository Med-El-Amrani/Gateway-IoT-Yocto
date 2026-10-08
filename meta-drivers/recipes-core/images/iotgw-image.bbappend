# Include the LED-only example in the Raspberry Pi 4 gateway image.
IMAGE_INSTALL:append:raspberrypi4-64 = " led-button led-button-overlay"

# Wic consumes IMAGE_BOOT_FILES; the legacy rpi-sdimg class needs its own
# copy step because its boot file list only includes kernel Device Trees.
IMAGE_BOOT_FILES:append:raspberrypi4-64 = " led-button.dtbo;overlays/led-button.dtbo"
do_image_wic[depends] += "led-button-overlay:do_deploy"
do_image_rpi_sdimg[depends] += "led-button-overlay:do_deploy"

do_image_rpi_sdimg:append:raspberrypi4-64() {
    mcopy -o -i ${WORKDIR}/boot.img ${DEPLOY_DIR_IMAGE}/led-button.dtbo ::overlays/led-button.dtbo
    dd if=${WORKDIR}/boot.img of=${SDIMG} conv=notrunc seek=1 bs=$(expr ${IMAGE_ROOTFS_ALIGNMENT} \* 1024)
    if [ "${SDIMG_VFAT_DEPLOY}" = "1" ]; then
        cp ${WORKDIR}/boot.img ${IMGDEPLOYDIR}/${SDIMG_VFAT}
    fi
}
