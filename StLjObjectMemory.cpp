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

#include "StLjObjectMemory.h"
#include "StObjectMemory.h"
#include <QtDebug>
#include <QIODevice>
#include <LjTools/Engine2.h>
#include <lua.hpp>
using namespace St;

/*
    Mapping to LuaJIT:

    objectNil -> lua nil
    objectTrue, objectFalse -> lua true, false

    SmallInteger -> lua number

    All other objects are represented by Lua tables.
    For now we stick with the ST80 objects for LargePositive/NegativeInteger; will represent these with values later.
    Floats are tables as well so we have no overlap with lua number; the number is in the 0 element.
    CompiledMethods are tables whereas literals are array elements of the table and there is a header and bytecode element.
    Word and byte arrays are also tables and there is a data element pointing to a cdata object carrying the array.
        Otherwise primitiveBecome would not be possible because len of cdata cannot be changed
    All indices are zero based!!!
*/

#define ST_BYTEARRAY_METANAME "StByteArray"
#define ST_WORDARRAY_METANAME "StWordArray"

const char* LjObjectMemory::s_bytecode = "bytecode";
const char* LjObjectMemory::s_header = "header";
const char* LjObjectMemory::s_count = "count";
const char* LjObjectMemory::s_oop = "oop";
const char* LjObjectMemory::s_data = "data";

static inline quint16 instanceSpecificationOf(const ObjectMemory& om, quint16 classPointer)
{
    return om.fetchWordOfObject(2,classPointer);
}

static inline bool isPointers(quint16 ispec )
{
    return ispec & 0x8000;
}

static inline bool isWords(quint16 ispec)
{
    return ispec & 0x4000;
}

static inline int oopToLuaIndex( quint16 oop ) { return oop >> 1; } // { return ( oop >> 1 ) + 1; }

static inline quint16 luaIndexToOop( int i ) { return i << 1; } // { return ( i - 1 ) << 1; }

static inline void pushKey( lua_State* L, const char* key )
{
    // lua_pushlightuserdata( L, const_cast<char*>(key) );
    lua_pushstring( L, key );
}

static inline bool pushValue( lua_State* L, quint16 oop, const ObjectMemory& om )
{
    if( oop == LjObjectMemory::objectNil )
        lua_pushnil( L );
    else if( oop == LjObjectMemory::objectTrue || oop == LjObjectMemory::objectFalse )
        lua_pushboolean( L, oop == LjObjectMemory::objectTrue );
    else if( !ObjectMemory::isPointer(oop) )
        lua_pushnumber( L, ObjectMemory::integerValueOf(oop) ); // SmallInteger
    else
        return false;
    return true;
}

LjObjectMemory::LjObjectMemory(Lua::Engine2* lua, QObject *parent) : QObject(parent),
    d_lua(lua)
{
    Q_ASSERT(d_lua != 0 );
}

bool LjObjectMemory::readFrom(QIODevice* in)
{
    ObjectMemory om;
    if( !om.readFrom(in) )
        return false;

    QList<quint16> oops = om.getAllValidOop();

    lua_State* L = d_lua->getCtx();
    const int toptop = lua_gettop(L);

    lua_getglobal(L,"ObjectMemory");
    Q_ASSERT( !lua_isnil( L, -1 ) );
    const int objectMemory = lua_gettop(L);

    lua_getfield(L, objectMemory, "createArray" );
    Q_ASSERT( !lua_isnil( L, -1 ) );
    const int createArrayFunction = lua_gettop(L);

    lua_getfield(L, objectMemory, "allObjects" );
    Q_ASSERT( !lua_isnil( L, -1 ) );
    const int allObjects = lua_gettop(L);

    lua_getfield(L, objectMemory, "knownObjects" );
    Q_ASSERT( !lua_isnil( L, -1 ) );
    const int knownObjects = lua_gettop(L);

    lua_createtable( L, 0, 0 );
    const int objectTable = lua_gettop(L);

    // first create the lua values/tables for each valid objectTable entry
    for( int i = 0; i < oops.size(); i++ )
    {
        const quint16 oop = oops[i];

        if( oop == objectNil || oop == objectTrue || oop == objectFalse )
            continue;

        Q_ASSERT( ObjectMemory::isPointer(oop) );

        lua_createtable( L, 0, 0 );
        lua_rawseti( L, objectTable, oopToLuaIndex(oop) );
        Q_ASSERT( lua_gettop(L) == objectTable );
    }

    // and now transfer all pointers, classes and methods
    for( int i = 0; i < oops.size(); i++ )
    {
        const quint16 oop = oops[i];

        // skip the pre-defined objects
        if( oop == objectNil || oop == objectTrue || oop == objectFalse )
            continue;

        const quint16 cls = om.fetchClassOf(oop);
        const quint16 ispec = instanceSpecificationOf(om,cls);

        lua_rawgeti( L, objectTable, oopToLuaIndex(oop) );
        const int luaObject = lua_gettop(L);

        lua_pushnumber( L, oop );
        lua_setfield( L, luaObject, s_oop );

        lua_rawgeti( L, objectTable, oopToLuaIndex(cls) );
        lua_setmetatable( L, luaObject );

        lua_pushvalue(L,luaObject);
        lua_pushboolean(L,true); // dummy
        lua_rawset(L,allObjects); // object is used as key in the weak table; entry removed if object gc'ed

        if( oop >= processor && oop <= classSymbol )
        {
            lua_pushvalue(L,luaObject);
            lua_rawseti( L, knownObjects, oop );
        }

        if( cls == classCompiledMethod )
        {
            pushKey( L, s_header );
            quint16 header = om.fetchWordOfObject(0,oop);
            pushValue( L, header, om );
            lua_rawset( L, luaObject );

            const int count = om.literalCountOf(oop);
            lua_pushnumber( L, count );
            lua_setfield( L, luaObject, s_count );
            for( int j = 0; j < count; j++ )
            {
                quint16 value = om.literalOfMethod(j,oop);
                if( !pushValue( L, value, om ) )
                    lua_rawgeti( L, objectTable, oopToLuaIndex(value) );
                lua_rawseti( L, luaObject, j );
            }

            pushKey( L, s_bytecode );
            ObjectMemory::ByteString bs = om.methodBytecodes(oop);
            lua_pushvalue( L, createArrayFunction );
            lua_pushlightuserdata( L, (void*)bs.d_bytes );
            lua_pushnumber( L, bs.d_byteLen );
            lua_pushboolean( L, false );
            if( lua_pcall(L, 3, 1, 0 ) == 0 )
                lua_rawset( L, luaObject );
            else
                lua_pop(L,2); // key and error msg
        }else if( cls == classFloat )
        {
            // Float is word and indexable
            lua_pushnumber( L, 1 );
            lua_setfield( L, luaObject, s_count );
            lua_pushnumber( L, om.fetchFloat(oop) );
            lua_rawseti( L, luaObject, 0 );
        }else if( isPointers(ispec) )
        {
            const int count = om.fetchWordLenghtOf(oop);
            lua_pushnumber( L, count );
            lua_setfield( L, luaObject, s_count );
            for( int j = 0; j < count; j++ )
            {
                quint16 value = om.fetchPointerOfObject(j,oop);
                if( !pushValue( L, value, om ) ) // if oop is not nil, bool or smallint then push the table for the object
                    lua_rawgeti( L, objectTable, oopToLuaIndex(value) );
                lua_rawseti( L, luaObject, j ); // LuaJIT supports zero based indices
            }
        }else
        {
            // ST object with no pointer members
            const bool words = isWords(ispec);
            pushKey( L, s_data );
            ObjectMemory::ByteString bs = om.fetchByteString(oop);
            lua_pushvalue( L, createArrayFunction );
            lua_pushlightuserdata( L, (void*)bs.d_bytes );
            lua_pushnumber( L, bs.d_byteLen ); // byteLen
            lua_pushboolean( L, words );
            lua_pushboolean( L, om.isBigEndian() );
            if( lua_pcall(L, 4, 1, 0 ) == 0 )
                lua_rawset( L, luaObject );
            else
                lua_pop(L,2);
        }
        lua_pop(L,1); // luaObject
        Q_ASSERT( lua_gettop(L) == objectTable );
    }

    lua_pushnil(L);
    lua_rawgeti( L, objectTable, oopToLuaIndex(0x6480) ); // UndefinedObject
    Q_ASSERT( !lua_isnil( L, -1 ) );
    lua_setmetatable(L,-2);
    lua_pop(L,1);

    lua_pushboolean(L,true);
    lua_rawgeti( L, objectTable, oopToLuaIndex(0x63cc) ); // Boolean
    Q_ASSERT( !lua_isnil( L, -1 ) );
    lua_setmetatable(L,-2);
    lua_pop(L,1);

    lua_rawgeti( L, objectTable, oopToLuaIndex(0x643a) ); // True
    Q_ASSERT( !lua_isnil( L, -1 ) );
    lua_setfield( L, knownObjects, "True" );

    lua_rawgeti( L, objectTable, oopToLuaIndex(0x6404) ); // False
    Q_ASSERT( !lua_isnil( L, -1 ) );
    lua_setfield( L, knownObjects, "False" );

    lua_rawgeti( L, objectTable, oopToLuaIndex(0x2392) ); // ParagraphEditor.CurrentSelection
    Q_ASSERT( !lua_isnil( L, -1 ) );
    lua_setfield( L, knownObjects, "CurrentSelection" );

    lua_pushnumber(L,0.0);
    lua_rawgeti( L, objectTable, oopToLuaIndex(classSmallInteger) );
    Q_ASSERT( !lua_isnil( L, -1 ) );
    lua_setmetatable(L,-2);
    lua_pop(L,1);

    lua_pop(L,1); // objectTable, no longer used
    lua_pop(L,1); // allObjects
    lua_pop(L,1); // knownObjects
    lua_pop(L,1); // createArray
    lua_pop(L,1); // ObjectMemory
    Q_ASSERT( toptop == lua_gettop(L) );

    // the full OM is now in Lua memory
    return true;
}

