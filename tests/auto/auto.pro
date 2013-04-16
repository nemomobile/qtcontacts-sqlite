include(../../aggregate.pri)
TEMPLATE = subdirs

SUBDIRS = qcontactmanager

contains(DEFINES, QTCONTACTS_SQLITE_PERFORM_AGGREGATION) {
    SUBDIRS += aggregation
} else {
    SUBDIRS += qcontactmanagerfiltering
}
