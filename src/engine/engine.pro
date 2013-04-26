include(../../aggregate.pri)
TEMPLATE = lib

QT += sql dbus

TARGET = qtcontacts_sqlite

lessThan(QT_MAJOR_VERSION, 5) {
    CONFIG += mobility plugin
    MOBILITY += contacts
    PLUGIN_TYPE=contacts
} else {
    QT += contacts
    PLUGIN_TYPE=contacts
}

HEADERS += \
        contactidimpl.h \
        contactsdatabase.h \
        contactsengine.h \
        contactnotifier.h \
        contactreader.h \
        contactwriter.h

SOURCES += \
        contactidimpl.cpp \
        contactsdatabase.cpp \
        contactsengine.cpp \
        contactsplugin.cpp \
        contactnotifier.cpp \
        contactreader.cpp \
        contactwriter.cpp

target.path = $$[QT_INSTALL_PLUGINS]/contacts
INSTALLS += target
