include(../config.pri)

QT += testlib
TEMPLATE = app
CONFIG -= app_bundle

INCLUDEPATH += $$PWD/../src/extensions

target.path = /opt/tests/qtcontacts-sqlite-qt5
INSTALLS += target
