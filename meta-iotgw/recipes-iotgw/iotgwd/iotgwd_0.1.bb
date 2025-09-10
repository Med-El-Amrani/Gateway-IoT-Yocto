SUMMARY = "IoT Gateway daemon (skeleton)"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit cmake systemd

SRC_URI = " \
    file://CMakeLists.txt \
    file://src/main.c \
    file://systemd/iotgwd.service \
"

S = "${WORKDIR}"

SYSTEMD_SERVICE:${PN} = "iotgwd.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install:append() {
    install -Dm0644 ${WORKDIR}/systemd/iotgwd.service ${D}${systemd_system_unitdir}/iotgwd.service
}

FILES:${PN} += " ${systemd_system_unitdir}/iotgwd.service "
