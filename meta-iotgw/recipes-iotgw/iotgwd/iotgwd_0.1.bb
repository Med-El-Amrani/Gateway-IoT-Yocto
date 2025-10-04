SUMMARY = "IoT Gateway daemon (skeleton)"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit cmake systemd

# Allow BitBake to find sources placed directly under this recipe directory
FILESEXTRAPATHS:prepend := "${THISDIR}:"

SRC_URI = " \
    file://iotgwd/ \
    file://files/config.example.yaml \
    file://files/protocols/ \
    file://files/schemas/ \
"

S = "${WORKDIR}/iotgwd"

# Build-time deps (headers in sysroot)
#  - yaml.h         → recipe: libyaml
#  - mosquitto.h    → recipe: mosquitto
#  - microhttpd.h   → recipe: libmicrohttpd
#  - sd-notify      → recipe: systemd
#  - pkg-config at build → pkgconfig-native
DEPENDS += "libyaml mosquitto libmicrohttpd systemd pkgconfig-native"

SYSTEMD_PACKAGES = "${PN}"

SYSTEMD_SERVICE:${PN} = "iotgwd.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"



do_install:append() {
    # Systemd unit
    install -Dm0644 ${S}/systemd/iotgwd.service ${D}${systemd_system_unitdir}/iotgwd.service

    # Default main config
    install -Dm0644 ${WORKDIR}/files/config.example.yaml ${D}${sysconfdir}/iotgw.yaml

    # Ship reference templates & schemas under /usr/share (no -a!)
    install -d ${D}${datadir}/iotgwd/protocols
    for f in ${WORKDIR}/files/protocols/*.yaml; do
        [ -f "$f" ] && install -m0644 "$f" ${D}${datadir}/iotgwd/protocols/
    done

    install -d ${D}${datadir}/iotgwd/schemas
    for f in ${WORKDIR}/files/schemas/*.json; do
        [ -f "$f" ] && install -m0644 "$f" ${D}${datadir}/iotgwd/schemas/
    done

    # Config dir for runtime fragments
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
