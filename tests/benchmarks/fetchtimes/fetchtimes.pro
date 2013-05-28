include(../../../config.pri)

TEMPLATE = app
TARGET = fetchtimes

SOURCES = main.cpp

equals(QT_MAJOR_VERSION, 4): target.path = /opt/tests/qtcontacts-sqlite
equals(QT_MAJOR_VERSION, 5): target.path = /opt/tests/qtcontacts-sqlite-qt5
INSTALLS += target
