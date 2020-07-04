#ifndef STLJOBJECTMEMORY_H
#define STLJOBJECTMEMORY_H

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

#include <QObject>

class QIODevice;
typedef struct lua_State lua_State;

namespace Lua
{
    class Engine2;
}

namespace St
{
    class LjObjectMemory : public QObject
    {
    public:
        enum KnownObjects {
            // small integers
            objectMinusOne = 65535,
            objectZero = 1,
            objectOne = 3,
            objectTwo = 5,

            // undefined, boolean
            objectNil = 0x02,
            objectFalse = 0x04,
            objectTrue = 0x06,

            // root
            processor = 0x08, // an Association whose value field is Processor
            smalltalk = 0x12, // an Association whose value field is SystemDirectory

            // classes
            classSmallInteger = 0x0c,
            classString = 0x0e,
            classArray = 0x10,
            classFloat = 0x14,
            classMethodContext = 0x16,
            classBlockContext = 0x18,
            classPoint = 0x1a,
            classLargePositiveInteger = 0x1c,
            classDisplayBitmap = 0x1e,
            classMessage = 0x20,
            classCompiledMethod = 0x22,
            classSemaphore = 0x26,
            classCharacter = 0x28,

            // symbols
            symbolTable = 0x0a, // symbol class variable USTable
            symbolDoesNotUnderstand = 0x2a,
            symbolCannotReturn = 0x2c,
            symbolMonitor = 0x2e,
            symbolUnusedOop18 = 0x24,
            symbolMustBeBoolean = 0x34,

            // selectors
            specialSelectors = 0x30, // SystemDictionary class variable, the array of selectors for bytecodes 260-317 octal
            characterTable = 0x32, // Character class variable, table of characters

            // extra knowns
            classSymbol = 0x38,
            classMethodDictionary = 0x4c,
        };

        static const char* s_bytecode; // bytecode of CompiledMethod
        static const char* s_header; // header of CompiledMethod
        static const char* s_count; // number of pointer elements (or literals in case of method)
        static const char* s_oop; // original oop for the initial objects to make hashing compatible with image
        static const char* s_data; // actual data in case of Word or Byte arrays (!isPointers)

        explicit LjObjectMemory(Lua::Engine2*, QObject *parent = 0);
        bool readFrom( QIODevice* );

    protected:

    private:
        Lua::Engine2* d_lua;
    };
}

#endif // STLJOBJECTMEMORY_H
