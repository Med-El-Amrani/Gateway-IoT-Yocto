SUMMARY = "GPIO17 LED platform driver (LED/button tutorial stage one)"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://led_button.c;beginline=1;endline=1;md5=fcab174c20ea2e2bc0be64b493708266"

SRC_URI = "file://led_button.c file://Makefile"
S = "${WORKDIR}"

inherit module

KERNEL_MODULE_AUTOLOAD += "led_button"

COMPATIBLE_MACHINE = "^raspberrypi4-64$"
