SUMMARY = "USB ACM and HID gadget setup"
LICENSE = "CLOSED"

SRC_URI = " \
    file://usb-gadget.sh \
    file://usb-gadget \
"

S = "${WORKDIR}"

inherit update-rc.d

INITSCRIPT_NAME = "usb-gadget"
INITSCRIPT_PARAMS = "start 20 S ."

do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${WORKDIR}/usb-gadget.sh \
        ${D}${sbindir}/usb-gadget.sh

    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/usb-gadget \
        ${D}${sysconfdir}/init.d/usb-gadget
}
