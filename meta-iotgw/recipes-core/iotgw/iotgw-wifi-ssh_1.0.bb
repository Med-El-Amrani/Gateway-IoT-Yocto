SUMMARY = "Provision SSH + WiFi (wlan0) via systemd-networkd/wpa_supplicant"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit allarch

SRC_URI = " \
    file://wpa_supplicant-wlan0.conf \
    file://80-wlan0.network \
"

# Pas de dépendances fragiles sur networkd/resolved (packaging varie selon branche) :
RDEPENDS:${PN} += " openssh-sshd wpa-supplicant systemd "

do_install() {
    # Wi-Fi
    install -Dm600 ${WORKDIR}/wpa_supplicant-wlan0.conf ${D}/etc/wpa_supplicant/wpa_supplicant-wlan0.conf

    # networkd (DHCP sur wlan0)
    install -Dm644 ${WORKDIR}/80-wlan0.network ${D}/etc/systemd/network/80-wlan0.network

    # Activer services (unit names stables, qu'ils soient splités ou intégrés à 'systemd')
    install -d ${D}/etc/systemd/system/multi-user.target.wants
    ln -sf /lib/systemd/system/sshd.service ${D}/etc/systemd/system/multi-user.target.wants/sshd.service
    ln -sf /lib/systemd/system/systemd-networkd.service ${D}/etc/systemd/system/multi-user.target.wants/systemd-networkd.service
    ln -sf /lib/systemd/system/wpa_supplicant@.service ${D}/etc/systemd/system/multi-user.target.wants/wpa_supplicant@wlan0.service

    # DNS via systemd-resolved (compilé dans 'systemd' chez toi) ; fallback silencieux si absent
    install -d ${D}/etc
    ln -sf /run/systemd/resolve/resolv.conf ${D}/etc/resolv.conf || true
}

FILES:${PN} += " \
  /etc/wpa_supplicant/wpa_supplicant-wlan0.conf \
  /etc/systemd/network/80-wlan0.network \
  /etc/systemd/system/multi-user.target.wants/* \
  /etc/resolv.conf \
"