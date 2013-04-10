TEMPLATE = lib

QT += sql dbus

TARGET = qtcontacts_sqlite

CONFIG += mobility plugin
MOBILITY += contacts
PLUGIN_TYPE=contacts

HEADERS += \
        contactsdatabase.h \
        contactsengine.h \
        contactnotifier.h \
        contactreader.h \
        contactwriter.h

SOURCES += \
        contactsdatabase.cpp \
        contactsengine.cpp \
        contactsplugin.cpp \
        contactnotifier.cpp \
        contactreader.cpp \
        contactwriter.cpp

target.path = $$[QT_INSTALL_PLUGINS]/contacts
INSTALLS += target
