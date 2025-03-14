SUMMARY = "GStreamer based multi view player"
DESCRIPTION = "GStreamer based multi view player"
LICENSE = "CLOSED"

SRC_URI = "file://gst-multiplayer.c"

S = "${WORKDIR}"

DEPENDS = "gstreamer1.0"

inherit pkgconfig

#CC = "${CC}"
CFLAGS = "-Wall"
LDFLAGS = "-lgstreamer-1.0"

do_compile() {
    ${CC} ${CFLAGS} -o gst-multiplayer ${S}/gst-multiplayer.c \
    $(pkg-config --cflags --libs gstreamer-1.0)
}

do_install() {
    install -d ${D}${bindir}/gst-multiplayer
    install -m 0755 gst-multiplayer ${D}${bindir}/gst-multiplayer
}
