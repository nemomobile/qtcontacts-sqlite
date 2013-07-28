include(../../config.pri)
include(../../aggregate.pri)

TEMPLATE = lib
TARGET = qtcontacts_sqlite

QT += sql dbus

CONFIG += plugin hide_symbols
PLUGIN_TYPE=contacts

# we hardcode this for Qt4 as there's no GenericDataLocation offered by QDesktopServices
DEFINES += 'QTCONTACTS_SQLITE_PRIVILEGED_DATABASE_DIR=\'\"/home/nemo/.privileged/\"\''
DEFINES += 'QTCONTACTS_SQLITE_DATABASE_DIR=\'\"/home/nemo/.local/share/data/\"\''
DEFINES += 'QTCONTACTS_SQLITE_DATABASE_NAME=\'\"contacts.db\"\''

INCLUDEPATH += \
        ../extensions

HEADERS += \
        semaphore_p.h \
        constants_p.h \
        conversion_p.h \
        contactid_p.h \
        contactsdatabase.h \
        contactsengine.h \
        contactnotifier.h \
        contactreader.h \
        contactwriter.h

SOURCES += \
        semaphore_p.cpp \
        conversion.cpp \
        contactid.cpp \
        contactsdatabase.cpp \
        contactsengine.cpp \
        contactsplugin.cpp \
        contactnotifier.cpp \
        contactreader.cpp \
        contactwriter.cpp

target.path = $$[QT_INSTALL_PLUGINS]/contacts
INSTALLS += target

headers.path = $${PREFIX}/include/qtcontacts-sqlite-extensions
headers.files = ../extensions/*
headers.depends = ../extensions/*
INSTALLS += headers

pkgconfig.path = $${PREFIX}/lib/pkgconfig
equals(QT_MAJOR_VERSION, 4): pkgconfig.files = ../qtcontacts-sqlite-extensions.pc
equals(QT_MAJOR_VERSION, 5): pkgconfig.files = ../qtcontacts-sqlite-qt5-extensions.pc
INSTALLS += pkgconfig

equals(QT_MAJOR_VERSION, 5): OTHER_FILES += plugin.json

