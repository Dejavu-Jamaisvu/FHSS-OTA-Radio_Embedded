SUMMARY = "CC1101 Raspberry Pi Device Tree overlay"
LICENSE = "GPL-2.0-only"

LIC_FILES_CHKSUM = "file://cc1101-overlay.dts;beginline=1;endline=1;md5=716d678de39d200d62496d72ab1104b2"

SRC_URI = "file://cc1101-overlay.dts"

S = "${WORKDIR}"

DEPENDS += "dtc-native virtual/kernel"

do_compile[depends] += "virtual/kernel:do_shared_workdir"

inherit deploy

do_compile() {
    install -d ${B}

    ${CPP} -nostdinc -undef -D__DTS__ \
        -x assembler-with-cpp \
        -I${STAGING_KERNEL_DIR}/include \
        ${WORKDIR}/cc1101-overlay.dts \
        > ${B}/cc1101-overlay.pp.dts

    ${STAGING_BINDIR_NATIVE}/dtc \
        -@ -I dts -O dtb \
        -o ${B}/cc1101.dtbo \
        ${B}/cc1101-overlay.pp.dts
}

do_install() {
    install -d ${D}${datadir}/cc1101/overlays

    install -m 0644 ${B}/cc1101.dtbo \
        ${D}${datadir}/cc1101/overlays/cc1101.dtbo
}

FILES:${PN} += "${datadir}/cc1101/overlays/cc1101.dtbo"

do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${B}/cc1101.dtbo \
        ${DEPLOYDIR}/cc1101.dtbo
}

addtask deploy after do_compile before do_build
