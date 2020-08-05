#/*
#* Copyright 2020 Rochus Keller <mailto:me@rochus-keller.ch>
#*
#* This file is part of the Smalltalk Interpreter application.
#*
#* The following is the license that applies to this copy of the
#* application. For a license to use the application under conditions
#* other than those described here, please email to me@rochus-keller.ch.
#*
#* GNU General Public License Usage
#* This file may be used under the terms of the GNU General Public
#* License (GPL) versions 2.0 or 3.0 as published by the Free Software
#* Foundation and appearing in the file LICENSE.GPL included in
#* the packaging of this file. Please review the following information
#* to ensure GNU General Public Licensing requirements will be met:
#* http://www.fsf.org/licensing/licenses/info/GPLv2.html and
#* http://www.gnu.org/copyleft/gpl.html.
#*/

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = St80VirtualMachine
TEMPLATE = app

INCLUDEPATH += ..

DEFINES += ST_IMG_VIEWER_EMBEDDED ST_OBJECT_MEMORY=ObjectMemory2


SOURCES +=\
    StInterpreter.cpp \
    StObjectMemory2.cpp \
    StVirtualMachine.cpp \
    StDisplay.cpp \
    StImageViewer.cpp

HEADERS  += \
    StInterpreter.h \
    StObjectMemory2.h \
    StVirtualMachine.h \
    StDisplay.h \
    StImageViewer.h


CONFIG(debug, debug|release) {
        DEFINES += _DEBUG
}

!win32 {
    QMAKE_CXXFLAGS += -Wno-reorder -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable
}

RESOURCES += \
    StVirtualMachine.qrc
