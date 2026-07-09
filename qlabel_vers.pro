QT += widgets core gui network multimedia

CONFIG += c++17


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

win32 {
    OPENCV_DIR = C:/opencv/build
    INCLUDEPATH += $$OPENCV_DIR/include

    CONFIG(debug, debug|release) {
        LIBS += -L$$OPENCV_DIR/x64/vc16/lib -lopencv_world4120d
    } else {
        LIBS += -L$$OPENCV_DIR/x64/vc16/lib -lopencv_world4120
    }
}

unix {
    CONFIG += link_pkgconfig
    PKGCONFIG += opencv4
}

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
