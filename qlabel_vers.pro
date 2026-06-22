QT += widgets core gui network multimedia

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    camerafeed.cpp \
    enrolldialog.cpp \
    faceengine.cpp \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    camerafeed.h \
    enrolldialog.h \
    faceengine.h \
    mainwindow.h

FORMS += \
    mainwindow.ui

# ─── OpenCV ──────────────────────────────────────────────────────────────────
# OpenCV 4.8+ gerekir (YuNet + SFace dahili). Kendi kurulumunuza göre düzenleyin.
#
# WINDOWS (MSVC):
#   - OPENCV_DIR'i kendi OpenCV "build" klasörünüze çevirin.
#   - Kütüphane adındaki SÜRÜM SONEKİ kurulu OpenCV'ye göre değişir:
#       4.8.0  -> opencv_world480     4.10.0 -> opencv_world4100
#       4.9.0  -> opencv_world490     4.11.0 -> opencv_world4110
#     (debug build'de sona 'd' eklenir, örn. opencv_world4100d)
#   - vc16 klasörü MSVC 2019/2022 içindir.
win32 {
    OPENCV_DIR = C:/opencv/build
    INCLUDEPATH += $$OPENCV_DIR/include

    CONFIG(debug, debug|release) {
        LIBS += -L$$OPENCV_DIR/x64/vc16/lib -lopencv_world4120d
    } else {
        LIBS += -L$$OPENCV_DIR/x64/vc16/lib -lopencv_world4120
    }
}

# LINUX / macOS — pkg-config (paket: opencv4)
unix {
    CONFIG += link_pkgconfig
    PKGCONFIG += opencv4
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
