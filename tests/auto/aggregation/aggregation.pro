TARGET = tst_aggregation
include(../../common.pri)

QT += contacts-private

INCLUDEPATH += \
    ../../../src/engine/

HEADERS += \
    ../../../src/engine/contactid_p.h \
    ../../../src/extensions/contactmanagerengine.h \
    ../../util.h
SOURCES += \
    ../../../src/engine/contactid.cpp \
    tst_aggregation.cpp

