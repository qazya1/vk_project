########################################
# sqlite.pri
#
# Vendored SQLite amalgamation (public domain, sqlite.org), taken from
# https://github.com/azadkuh/sqlite-amalgamation
#
# Needed so the app can call the raw sqlite3 C API (sqlite3_create_function,
# etc.) on the native handle exposed by Qt's QSQLITE driver.
########################################

INCLUDEPATH += $$PWD

SOURCES += $$PWD/sqlite3.c
HEADERS += $$PWD/sqlite3.h
