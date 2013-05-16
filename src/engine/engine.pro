include(../../aggregate.pri)
TEMPLATE = lib

QT += sql dbus

TARGET = qtcontacts_sqlite

CONFIG += mobility plugin
MOBILITY += contacts
PLUGIN_TYPE=contacts

# we hardcode this for Qt4 as there's no GenericDataLocation offered by QDesktopServices
DEFINES += 'QTCONTACTS_SQLITE_DATABASE_DIR=\'\"/home/nemo/.local/share/data/qtcontacts-sqlite/\"\''
DEFINES += 'QTCONTACTS_SQLITE_DATABASE_NAME=\'\"contacts.db\"\''

HEADERS += \
        semaphore_p.h \
        contactsdatabase.h \
        contactsengine.h \
        contactnotifier.h \
        contactreader.h \
        contactwriter.h

SOURCES += \
        semaphore_p.cpp \
        contactsdatabase.cpp \
        contactsengine.cpp \
        contactsplugin.cpp \
        contactnotifier.cpp \
        contactreader.cpp \
        contactwriter.cpp

target.path = $$[QT_INSTALL_PLUGINS]/contacts
INSTALLS += target
