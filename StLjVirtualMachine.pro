#/*
#* Copyright 2020 Rochus Keller <mailto:me@rochus-keller.ch>
#*
#* This file is part of the Smalltalk VM application.
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
QT += printsupport

TARGET = St80LjVirtualMachine
TEMPLATE = app

INCLUDEPATH += .. ../LuaJIT/src

DEFINES += ST_DISPLAY_WORDARRY

SOURCES += \
    StLjVirtualMachine.cpp \
    StLjObjectMemory.cpp \
    StDisplay.cpp \
    StObjectMemory.cpp \
    StLjLibFfi.cpp

HEADERS  += \ 
    StLjVirtualMachine.h \
    StLjObjectMemory.h \
    StDisplay.h \
    StObjectMemory.h

DEFINES += LUAIDE_EMBEDDED
include( ../LjTools/LuaIde.pri )
include( ../LjTools/Lua.pri )
include( ../GuiTools/Menu.pri )

win32 {
    LIBS += -L../LuaJIT/src -llua51
}
linux {
    include( ../LuaJIT/src/LuaJit.pri ){
        LIBS += -ldl
    } else {
        LIBS += -lluajit
    }
    QMAKE_LFLAGS += -rdynamic -ldl
    #rdynamic is required so that the LjLibFfi functions are visible to LuaJIT FFI
}
macx {
    include( ../LuaJIT/src/LuaJit.pri )
    QMAKE_LFLAGS += -rdynamic -ldl -pagezero_size 10000 -image_base 100000000
}

CONFIG(debug, debug|release) {
        DEFINES += _DEBUG
}

!win32 {
    QMAKE_CXXFLAGS += -Wno-reorder -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable
}

RESOURCES += \
    StLjVirtualMachine.qrc
