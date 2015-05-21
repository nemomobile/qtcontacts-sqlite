include(../../../config.pri)

TEMPLATE = app
TARGET = twcsatimes

PKGCONFIG += qtcontacts-sqlite-qt5-extensions
INCLUDEPATH += ../../../src/engine/ ../../../src/extensions/
QT += contacts-private

SOURCES += main.cpp TestSyncAdapter.cpp
HEADERS += TestSyncAdapter.h

target.path = /opt/tests/qtcontacts-sqlite-qt5
INSTALLS += target
