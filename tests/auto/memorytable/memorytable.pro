include(../../common.pri)

TARGET = tst_memorytable

QT += contacts-private

HEADERS += \
    ../../util.h
SOURCES += \
    tst_memorytable.cpp \
    ../../../src/engine/memorytable.cpp
