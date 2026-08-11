SUMMARY = "CC1101 SPI kernel module"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://cc1101_main.c;beginline=1;endline=1;md5=716d678de39d200d62496d72ab1104b2"
inherit module

SRC_URI = " \
    file://Makefile \
    file://cc1101_core.c \
    file://cc1101_main.c \
    file://cc1101.h \
    file://cc1101_ioctl.h \
"

S = "${WORKDIR}"

EXTRA_OEMAKE += "KDIR=${STAGING_KERNEL_DIR}"
ALLOW_EMPTY:${PN} = "1"
RDEPENDS:${PN} += "kernel-module-cc1101-${KERNEL_VERSION}"
KERNEL_MODULE_AUTOLOAD += "cc1101"
