QT += core gui qml quick quickcontrols2 dbus pdf

CONFIG += c++17 release
TARGET = plumb
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/exportjob.h \
    src/filepicker.h \
    src/portalfilepicker.h \
    src/sheetimageprovider.h \
    src/takeoff.h \
    src/takeofflayer.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/exportjob.cpp \
    src/portalfilepicker.cpp \
    src/sheetimageprovider.cpp \
    src/takeoff.cpp \
    src/takeofflayer.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
