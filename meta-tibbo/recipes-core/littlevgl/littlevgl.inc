DESCRIPTION = "Small embedded GUI tests"
HOMEPAGE = "https://littlevgl.com/"
MAINTAINER = "Dmitry Dvorkin <dvorkin@tibbo.com>"
LICENSE = "MIT"
SECTION = "libs"
PACKAGES = "${PN}-dbg ${PN}-doc ${PN}-dev ${PN}-staticdev ${PN}"
SRCREV="${AUTOREV}"

#inherit autotools

S = "${WORKDIR}/git"

SRC_URI  = "gitsm://github.com/littlevgl/lv_port_linux_frame_buffer.git;branch=master;protocol=git;"
SRC_URI += "file://display.mk.patch"

FILES_${PN}  = "demo"

do_install() {
 install -m 0755 ${S}/demo ${D}/
}

FILES_${PN}  = "/demo"

#RDEPENDS_${PN} += "tps-capi-udev"
#RDEPENDS_${PN} += "libstdc++ "

LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
