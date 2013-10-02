TEMPLATE = subdirs
SUBDIRS = auto benchmarks

tests_xml.path = /opt/tests/qtcontacts-sqlite-qt5/
tests_xml.files = tests.xml
equals(QT_MAJOR_VERSION, 5): INSTALLS += tests_xml
