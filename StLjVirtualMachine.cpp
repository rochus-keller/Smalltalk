/*
* Copyright 2020 Rochus Keller <mailto:me@rochus-keller.ch>
*
* This file is part of the Smalltalk parser/compiler library.
*
* The following is the license that applies to this copy of the
* library. For a license to use the library under conditions
* other than those described here, please email to me@rochus-keller.ch.
*
* GNU General Public License Usage
* This file may be used under the terms of the GNU General Public
* License (GPL) versions 2.0 or 3.0 as published by the Free Software
* Foundation and appearing in the file LICENSE.GPL included in
* the packaging of this file. Please review the following information
* to ensure GNU General Public Licensing requirements will be met:
* http://www.fsf.org/licensing/licenses/info/GPLv2.html and
* http://www.gnu.org/copyleft/gpl.html.
*/

#include "StLjVirtualMachine.h"
#include "StLjObjectMemory.h"
#include "StDisplay.h"
#include <LjTools/Engine2.h>
#include <LjTools/LuaIde.h>
#include <LjTools/LuaProject.h>
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QtDebug>
#include <lua.hpp>
using namespace St;
using namespace Lua;

static void loadLuaLib( Lua::Engine2* lua, const QByteArray& name )
{
    QFile lib( QString(":/%1.lua").arg(name.constData()) );
    lib.open(QIODevice::ReadOnly);
    if( !lua->addSourceLib( lib.readAll(), name ) )
        qCritical() << "compiling" << name << ":" << lua->getLastError();
}

static int toaddress(lua_State * L)
{
    lua_pushinteger( L, (lua_Integer)lua_topointer( L, 1 ) );
    return 1;
}

LjVirtualMachine::LjVirtualMachine(QObject* parent) : QObject(parent)
{
    d_lua = new Engine2(this);
    Engine2::setInst(d_lua);
    connect( d_lua,SIGNAL(onNotify(int,QByteArray,int)), this, SLOT(onNotify(int,QByteArray,int)) );
    d_lua->addStdLibs();
    d_lua->addLibrary(Engine2::PACKAGE);
    d_lua->addLibrary(Engine2::IO);
    d_lua->addLibrary(Engine2::BIT);
    d_lua->addLibrary(Engine2::JIT);
    d_lua->addLibrary(Engine2::FFI);
    d_lua->addLibrary(Engine2::OS);

    lua_pushcfunction( d_lua->getCtx(), toaddress );
    lua_setglobal( d_lua->getCtx(), "toaddress" );

}

bool LjVirtualMachine::load(const QString& path)
{    
    lua_pushstring( d_lua->getCtx(), path.toUtf8().constData() );
    lua_setglobal( d_lua->getCtx(),"VirtualImage" );
    return true;
}

void LjVirtualMachine::run()
{
    loadLuaLib( d_lua, "ObjectMemory");
    loadLuaLib( d_lua, "Interpreter");

    lua_getglobal( d_lua->getCtx(), "runInterpreter" );
    if( !d_lua->runFunction() )
    {
        qCritical() << d_lua->getLastError().constData();
        QMessageBox::critical( Display::inst(), tr("Lua Error"), d_lua->getLastError() );
    }
}

void LjVirtualMachine::onNotify(int messageType, QByteArray val1, int val2)
{
    switch(messageType)
    {
    case Lua::Engine2::Print:
    case Lua::Engine2::Cout:
        qDebug() << val1.trimmed().constData();
        break;
    case Lua::Engine2::Error:
    case Lua::Engine2::Cerr:
        qCritical() << "ERR" << val1.trimmed().constData();
        break;
    }
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationName("me@rochus-keller.ch");
    a.setOrganizationDomain("github.com/rochus-keller/Smalltalk");
    a.setApplicationName("Smalltalk-80 on LuaJIT");
    a.setApplicationVersion("0.6.0");
    a.setStyle("Fusion");

    QString imagePath;
    QString proFile;
    bool ide = false;
    const QStringList args = QCoreApplication::arguments();
    for( int i = 1; i < args.size(); i++ ) // arg 0 enthaelt Anwendungspfad
    {
        if( args[i] == "-h" || args.size() == 1 )
        {
            QTextStream out(stdout);
            out << a.applicationName() << " version: " << a.applicationVersion() <<
                         " author: me@rochus-keller.ch  license: GPL" << endl;
            out << "usage: [options] image_file" << endl;
            out << "options:" << endl;
            out << "  -ide      start Lua IDE" << endl;
            out << "  -pro file open given project in LuaIDE" << endl;
            out << "  -h        display this information" << endl;
            return 0;
        }else if( args[i] == "-ide" )
            ide = true;
        else if( args[i] == "-pro" )
        {
            ide = true;
            if( i+1 >= args.size() )
            {
                qCritical() << "error: invalid -pro option" << endl;
                return -1;
            }else
            {
                proFile = args[i+1];
                i++;
            }
        }else if( !args[ i ].startsWith( '-' ) )
        {
            if( !imagePath.isEmpty() )
            {
                qCritical() << "error: can only load one image file" << endl;
                return -1;
            }
            imagePath = args[ i ];
        }else
        {
            qCritical() << "error: invalid command line option " << args[i] << endl;
            return -1;
        }
    }

    LjVirtualMachine vm;

    if( imagePath.isEmpty() )
    {
        imagePath = QFileDialog::getOpenFileName(Display::inst(),LjVirtualMachine::tr("Open Smalltalk-80 Image File"),
                                                          QString(), "VirtualImage *.image *.im" );
        if( imagePath.isEmpty() )
            return 0;
    }
    if( !vm.load(imagePath) )
        return -1;

    if( ide )
    {
        LuaIde win( vm.getLua() );
        win.getProject()->addBuiltIn("toaddress");
        win.getProject()->addBuiltIn("VirtualImage");
        if( !proFile.isEmpty() )
            win.loadFile(proFile);
        a.exec();
    }else
    {
        vm.run();
        return 0;
    }
}
