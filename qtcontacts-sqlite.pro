TEMPLATE = subdirs
SUBDIRS = \
        src \
        tests
OTHER_FILES += rpm/qtcontacts-sqlite-qt5.spec

tests.depends = src
