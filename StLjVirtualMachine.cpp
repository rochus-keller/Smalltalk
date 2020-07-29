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
#include <LuaJIT/src/lua.hpp>
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QFormLayout>
#include <QtDebug>
#include <lua.hpp>
#include <QPainter>
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

static int getfilesofdir(lua_State * L)
{
    QString indir;
    if( lua_gettop(L) > 0 )
        indir = luaL_checkstring(L,1);
    QDir dir;
    if( !indir.isEmpty() )
    {
        QFileInfo info(indir);
        if( info.isRelative() )
        {
            lua_getglobal( L,"VirtualImage" );
            QDir root = QFileInfo( luaL_checkstring(L,-1) ).absoluteDir();
            lua_pop(L,1);
            dir = root.absoluteFilePath(indir);
        }else
            dir = indir;
    }else
    {
        lua_getglobal( L,"VirtualImage" );
        dir = QFileInfo( luaL_checkstring(L,-1) ).absoluteDir();
        lua_pop(L,1);
    }
    QStringList files = dir.entryList(QDir::Files);
    foreach( const QString& f, files )
        lua_pushstring(L, f.toUtf8().constData() );
    return files.size();
}


#if LUAJIT_VERSION_NUM >= 20100
#define ST_USE_MONITOR
#endif

#ifdef ST_USE_MONITOR
#ifdef ST_USE_MONITOR_GUI
class JitMonitor : public QWidget
{
public:
    class Gauge : public QWidget
    {
    public:
        Gauge( QWidget* p):QWidget(p),d_val(0){ setMinimumWidth(100);}
        quint32 d_val;
        void paintEvent(QPaintEvent *)
        {
            QPainter p(this);
            p.fillRect(0,0, width() / 100 * d_val, height(),Qt::red);
            p.drawText(rect(), Qt::AlignCenter, QString("%1%").arg(d_val));
        }
    };

    enum Bar { Compiled, Interpreted, C, GC, Compiler, MAX };
    quint32 d_state[MAX];
    quint32 d_count;
    Gauge* d_bars[MAX];
    JitMonitor():d_count(1)
    {
        QFormLayout* vb = new QFormLayout(this);
        static const char* names[] = {
            "Compiled:", "Interpreted:", "C Code:", "Garbage Collector:", "JIT Compiler:"
        };
        for( int i = 0; i < MAX; i++ )
        {
            d_bars[i] = new Gauge(this);
            d_state[i] = 0;
            vb->addRow(names[i],d_bars[i]);
        }
    }
};
static JitMonitor* s_jitMonitor = 0;

static void profile_callback(void *data, lua_State *L, int samples, int vmstate)
{
    if( s_jitMonitor == 0 )
        s_jitMonitor = new JitMonitor();
    switch( vmstate )
    {
    case 'N':
        vmstate = JitMonitor::Compiled;
        break;
    case 'I':
        vmstate = JitMonitor::Interpreted;
        break;
    case 'C':
        vmstate = JitMonitor::C;
        break;
    case 'G':
        vmstate = JitMonitor::GC;
        break;
    case 'J':
        vmstate = JitMonitor::Compiler;
        break;
    default:
        qCritical() << "profile_callback: unknown vmstate" << vmstate;
        return;
    }
    JitMonitor* m = s_jitMonitor;
    m->d_count += samples;
    m->d_state[vmstate] += samples;
    m->d_bars[vmstate]->d_val = m->d_state[vmstate] / double(m->d_count) * 100.0 + 0.5;
    m->d_bars[vmstate]->update();
    m->show();
    // qDebug() << "profile_callback" << Display::inst()->getTicks() << samples << vmstate << (char)vmstate;
}
#else

static inline int percent( double a, double b )
{
    return a / b * 100 + 0.5;
}

static void profile_callback(void *data, lua_State *L, int samples, int vmstate)
{
    static quint32 compiled = 0, interpreted = 0, ccode = 0, gc = 0, compiler = 0, count = 1, lag = 0;
    count += samples;
    switch( vmstate )
    {
    case 'N':
        compiled += samples;
        break;
    case 'I':
        interpreted += samples;
        break;
    case 'C':
        ccode += samples;
        break;
    case 'G':
        gc += samples;
        break;
    case 'J':
        compiler += samples; // never seen so far
        break;
    default:
        qCritical() << "profile_callback: unknown vmstate" << vmstate;
        return;
    }
    lag++;
    if( lag >= 0 ) // adjust to luaJIT_profile_start i parameter
    {
        lag = 0;

        qDebug() << "profile_callback" << percent(compiled,count) << percent(interpreted,count) <<
                    percent(ccode,count) << percent(gc,count) <<
                    percent(compiler,count) << "time:" << St::Display::inst()->getTicks();
    }
}
#endif
#endif

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

#ifdef ST_SET_JIT_PARAMS_BY_LUA
    // works in principle, but the JIT runs about 5% slower than when directly set via lj_jit.h
    QByteArray cmd = "jit.opt.start(";
    cmd += "\"maxtrace=100000\",";
    cmd += "\"maxrecord=40000\",";
    cmd += "\"maxside=1000\",";
    cmd += "\"sizemcode=64\",";
    cmd += "\"maxmcode=4096\")";
    if( !d_lua->executeCmd(cmd) )
        qCritical() << "error initializing JIT:" << d_lua->getLastError();
#endif

    lua_pushcfunction( d_lua->getCtx(), toaddress );
    lua_setglobal( d_lua->getCtx(), "toaddress" );

    lua_pushcfunction( d_lua->getCtx(), getfilesofdir );
    lua_setglobal( d_lua->getCtx(), "getfilesofdir" );
}

bool LjVirtualMachine::load(const QString& path)
{    
    lua_pushstring( d_lua->getCtx(), path.toUtf8().constData() );
    lua_setglobal( d_lua->getCtx(),"VirtualImage" );
    QDir::setCurrent("");
    return true;
}

void LjVirtualMachine::run(bool useJit, bool useProfiler)
{
#ifdef ST_USE_MONITOR
    if( useProfiler )
        luaJIT_profile_start( d_lua->getCtx(), "i1000", profile_callback, 0);
#endif
    if( !useJit )
        luaJIT_setmode( d_lua->getCtx(), 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF );

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
    a.setApplicationVersion("0.6.3");
    a.setStyle("Fusion");

    QString imagePath;
    QString proFile;
    bool ide = false;
    bool useProfiler = false;
    bool useJit = true;
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
            out << "  -nojit    switch off JIT" << endl;
            out << "  -stats    use LuaJIT profiler (if present)" << endl;
            out << "  -h        display this information" << endl;
            return 0;
        }else if( args[i] == "-ide" )
            ide = true;
        else if( args[i] == "-nojit" )
                    useJit = false;
        else if( args[i] == "-stats" )
                    useProfiler = true;
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
        win.getProject()->addBuiltIn("getfilesofdir");
        win.getProject()->addBuiltIn("VirtualImage");
        if( !proFile.isEmpty() )
            win.loadFile(proFile);
        a.exec();
    }else
    {
        vm.run(useJit,useProfiler);
        return 0;
    }
}
