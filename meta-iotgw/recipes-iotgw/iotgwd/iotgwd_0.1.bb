SUMMARY = "IoT Gateway daemon (skeleton)"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit cmake systemd

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI = " \
    file://CMakeLists.txt \
    file://src/main.c \
    file://systemd/iotgwd.service \
    file://config.example.yaml
"

S = "${WORKDIR}"

SYSTEMD_SERVICE:${PN} = "iotgwd.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install:append() {
    install -Dm0644 ${WORKDIR}/systemd/iotgwd.service ${D}${systemd_system_unitdir}/iotgwd.service
    # /etc/iotgw.yaml sera copié dans le rootfs
    install -Dm644 ${WORKDIR}/config.example.yaml ${D}${sysconfdir}/iotgw.yaml
}

# Marqué « config » ⇒ pas écrasé lors d’une mise-à-jour OTA
CONFFILES:${PN} += "${sysconfdir}/iotgw.yaml"

FILES:${PN} += " ${systemd_system_unitdir}/iotgwd.service "
