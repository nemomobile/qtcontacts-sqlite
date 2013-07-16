include(../config.pri)

QT += testlib
TEMPLATE = app
CONFIG -= app_bundle

INCLUDEPATH += $$PWD/../src/extensions

equals(QT_MAJOR_VERSION, 4): target.path = /opt/tests/qtcontacts-sqlite
equals(QT_MAJOR_VERSION, 5): target.path = /opt/tests/qtcontacts-sqlite-qt5
INSTALLS += target
