SUMMARY = "CC1101 test application"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://cc1101_test.c;beginline=1;endline=1;md5=716d678de39d200d62496d72ab1104b2"
SRC_URI = " \
    file://cc1101_test.c \
    file://cc1101_ioctl.h \
"
S = "${WORKDIR}"
do_compile() {
    ${CC} ${CFLAGS} \
        -I${WORKDIR} \
        ${WORKDIR}/cc1101_test.c \
        ${LDFLAGS} \
        -o ${B}/cc1101_test
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/cc1101_test ${D}${bindir}/cc1101_test
}
