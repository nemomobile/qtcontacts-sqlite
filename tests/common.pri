include(../config.pri)

QT += testlib
TEMPLATE = app
CONFIG -= app_bundle

equals(QT_MAJOR_VERSION, 4): target.path = /opt/tests/qtcontacts-sqlite
equals(QT_MAJOR_VERSION, 5): target.path = /opt/tests/qtcontacts-sqlite-qt5
INSTALLS += target
