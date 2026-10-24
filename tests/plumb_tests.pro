QT += core gui quick testlib dbus pdf
CONFIG += c++17 testcase
TARGET = plumb_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/cli.h ../src/exportjob.h ../src/filepicker.h ../src/takeoff.h ../src/theme.h ../src/translator.h
SOURCES += plumb_tests.cpp ../src/backend.cpp ../src/cli.cpp ../src/exportjob.cpp ../src/takeoff.cpp ../src/theme.cpp ../src/translator.cpp
RESOURCES += ../src/resources.qrc
