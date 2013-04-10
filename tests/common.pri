QT += testlib
TEMPLATE = app
CONFIG -= app_bundle

CONFIG += mobility
MOBILITY += contacts

target.path = /opt/tests/qtcontacts-sqlite
INSTALLS += target
