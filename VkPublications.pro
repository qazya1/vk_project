QT       += core gui
QT       += sql
QT += network
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11
# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

include(./QXlsx/QXlsx.pri);
include(./sqlite/sqlite.pri);
# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    adduserdialog.cpp \
    customtabwidget.cpp \
    dateedit.cpp \
    groupwidget.cpp \
    main.cpp \
    mainwindow.cpp \
    announcementstabcontroller.cpp \
    vkoauth2.cpp \
    xmlimporttabcontroller.cpp \
    groupstabcontroller.cpp \
    pagebuttonclass.cpp \
    announcementservice.cpp \
    xmlimportservice.cpp \
    backgroundjobservice.cpp \
    databasemanager.cpp \
    groupservice.cpp \
    publishprocessmanager.cpp \
    table_ui_utils.cpp \
    tabledelegates.cpp \
    announcementtableformatter.cpp \
    basetablecontroller.cpp \
    announcementtablecontroller.cpp \
    tableconfigurator.cpp \
    pugixml/pugixml.cpp \
    group.cpp \
    grouprepository.cpp

HEADERS += \
    adduserdialog.h \
    customtabwidget.h \
    dateedit.h \
    groupwidget.h \
    group_types.h \
    announcementservice.h \
    vkoauth2.h \
    xmlimportservice.h \
    backgroundjobservice.h \
    databasemanager.h \
    groupservice.h \
    mainwindow.h \
    announcementstabcontroller.h \
    xmlimporttabcontroller.h \
    groupstabcontroller.h \
    pagebuttonclass.h \
    publishprocessmanager.h \
    table_ui_utils.h \
    tabledelegates.h \
    announcementtableformatter.h \
    basetablecontroller.h \
    announcementtablecontroller.h \
    tabletypes.h \
    tableconfigurator.h \
    pugixml/pugiconfig.hpp \
    pugixml/pugixml.hpp \
    group.h \
    grouprepository.h

FORMS += \
    adduserdialog.ui \
    groupwidget.ui \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    resources/resources.qrc
