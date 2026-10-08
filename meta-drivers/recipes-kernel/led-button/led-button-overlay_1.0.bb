SUMMARY = "GPIO17 LED Device Tree overlay for Raspberry Pi 4"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://led-button-overlay.dts;beginline=1;endline=1;md5=fcab174c20ea2e2bc0be64b493708266"

SRC_URI = "file://led-button-overlay.dts"
S = "${WORKDIR}"
DEPENDS = "dtc-native"

inherit deploy

COMPATIBLE_MACHINE = "^raspberrypi4-64$"
PACKAGE_ARCH = "${MACHINE_ARCH}"

do_compile() {
    dtc -@ -I dts -O dtb -o led-button.dtbo led-button-overlay.dts
}

do_install() {
    install -Dm0644 ${B}/led-button.dtbo ${D}${nonarch_base_libdir}/firmware/overlays/led-button.dtbo
}

FILES:${PN} = "${nonarch_base_libdir}/firmware/overlays/led-button.dtbo"

do_deploy() {
    install -Dm0644 ${B}/led-button.dtbo ${DEPLOYDIR}/led-button.dtbo
}
addtask deploy after do_compile before do_build
