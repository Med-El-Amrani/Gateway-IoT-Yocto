SUMMARY = "IoT Gateway daemon (skeleton)"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit cmake systemd

SRC_URI = " \
    file://iotgwd \
    file://files/config.example.yaml\
    file://files/protocols \
    file://files/schemas
"

S = "${WORKDIR}/iotgwd"

# Build-time dependency for libsystemd (sd_notify etc.)
DEPENDS += "systemd"

SYSTEMD_SERVICE:${PN} = "iotgwd.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install:append() {
    # Systemd unit (CMake also installs it, but this is explicit & safe)
    install -Dm0644 ${S}/systemd/iotgwd.service ${D}${systemd_system_unitdir}/iotgwd.service

    # Default main config (installed once; preserved by CONFFILES)
    install -Dm0644 ${WORKDIR}/files/config.example.yaml ${D}${sysconfdir}/iotgw.yaml

    # Optional: ship reference templates & schemas under /usr/share
    install -d ${D}${datadir}/iotgwd/protocols
    cp -a ${WORKDIR}/files/protocols/* ${D}${datadir}/iotgwd/protocols/ || true

    install -d ${D}${datadir}/iotgwd/schemas
    cp -a ${WORKDIR}/files/schemas/* ${D}${datadir}/iotgwd/schemas/ || true

    # a config dir for fragments:
    # (We still keep examples in /usr/share/iotgwd/protocols/, so users can copy what they need into /etc/iotgwd/.)
    install -d ${D}${sysconfdir}/iotgwd
}

# Mark as configuration so OTA won't overwrite local edits
CONFFILES:${PN} += " ${sysconfdir}/iotgw.yaml"

# Make sure the extra data lands in the package
FILES:${PN} += " \
    ${systemd_system_unitdir}/iotgwd.service \
    ${datadir}/iotgwd/protocols \
    ${datadir}/iotgwd/schemas \
    ${sysconfdir}/iotgwd \
"
