#ifndef ST_OBJECT_MEMORY_2_H
#define ST_OBJECT_MEMORY_2_H

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
#include <QSet>
#include <QVector>
#include <bitset>

class QIODevice;

namespace St
{
    class ObjectMemory2 : public QObject
    {
    public:
        typedef quint16 OOP;

        // Objects known to the interpreter
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

        enum CompiledMethodFlags {
            ZeroArguments = 0,
            OneArgument = 1,
            TwoArguments = 2,
            ThreeArguments = 3,
            FourArguments = 4,
            ZeroArgPrimitiveReturnSelf = 5, // no bytecode
            ZeroArgPrimitiveReturnVar = 6, // methodTemporaryCount returns index of the instance var to return, no bytecode
            HeaderExtension = 7
        };

        struct ByteString
        {
            const quint8* d_bytes;
            quint16 d_len;
            ByteString(const quint8* b, quint16 l):d_bytes(b),d_len(l){}
        };

        ObjectMemory2(QObject* p = 0);
        bool readFrom( QIODevice* );
        void collectGarbage();

        QList<quint16> getAllValidOop() const;
        const QSet<quint16>& getObjects() const {return d_objects; }
        const QSet<quint16>& getClasses() const {return d_classes; }
        const QSet<quint16>& getMetaClasses() const {return d_metaClasses; }
        typedef QHash<quint16, QList<quint16> > Xref;
        const Xref& getXref() const { return d_xref; }
        void setRegister( quint8 index, quint16 value );
        quint16 getRegister( quint8 index ) const;
        void addTemp(OOP oop);
        void removeTemp(OOP oop);

        // oop 0 is reserved as an invalid object pointer!

        bool hasPointerMembers( OOP objectPointer ) const;
        OOP fetchPointerOfObject( quint16 fieldIndex, OOP objectPointer ) const;
        void storePointerOfObject( quint16 fieldIndex, OOP objectPointer, OOP withValue );
        quint16 fetchWordOfObject( quint16 fieldIndex, OOP objectPointer ) const;
        void storeWordOfObject( quint16 fieldIndex, OOP objectPointer, quint16 withValue );
        quint8 fetchByteOfObject( quint16 byteIndex, OOP objectPointer ) const;
        void storeByteOfObject( quint16 byteIndex, OOP objectPointer, quint8 withValue );
        OOP fetchClassOf( OOP objectPointer ) const;
        quint16 fetchByteLenghtOf( OOP objectPointer ) const;
        quint16 fetchWordLenghtOf( OOP objectPointer ) const;
        ByteString fetchByteString( OOP objectPointer ) const;

        OOP instantiateClassWithPointers( OOP classPointer, quint16 instanceSize );
        OOP instantiateClassWithWords( OOP classPointer, quint16 instanceSize );
        OOP instantiateClassWithBytes( OOP classPointer, quint16 instanceByteSize );
        QByteArray fetchClassName( OOP classPointer ) const;

        quint8 methodTemporaryCount( OOP methodPointer ) const; // including args
        CompiledMethodFlags methodFlags( OOP methodPointer ) const;
        bool methodLargeContext( OOP methodPointer ) const;
        quint8 methodLiteralCount( OOP methodPointer ) const;
        ByteString methodBytecodes( OOP methodPointer ) const;
        quint8 methodArgumentCount(OOP methodPointer ) const;
        quint8 methodPrimitiveIndex(OOP methodPointer ) const;
        OOP methodLiteral(quint8 index, OOP methodPointer ) const;
        quint32 methodInitialInstructionPointer( OOP methodPointer ) const;
        OOP methodClassOf( OOP methodPointer ) const;
        quint8 methodFieldIndex( OOP methodPointer ) const { return methodTemporaryCount(methodPointer); }
        // last literal contains Association to superclass in case of super call

        static bool isPointer(OOP);
        static qint16 toInt(OOP objectPointer );
        static OOP toPtr(qint16 value );

    protected:
        int findFreeSlot();
        OOP instantiateClass( OOP cls, quint16 wordLen, bool isPtr );
        void mark(OOP);

    private:
        struct Object
        {
            enum Flags { Marked };
            std::bitset<8> d_flags;
            quint8 d_data[1]; // variable length
        };

        struct OtSlot
        {
            quint32 d_size : 15; // number of words (16 bit per word)
            quint32 d_isOdd : 1;
            quint32 d_class : 15;
            quint32 d_isPtr : 1;
            Object* d_obj;
            OtSlot():d_obj(0),d_size(0),d_isOdd(0),d_class(0),d_isPtr(0) {}
            bool isFree() const { return d_obj == 0; }
            OOP getClass() const { return d_class << 1; }
            quint32 byteLen() const { return d_size << 1; }
        };

        struct ObjectTable
        {
            QVector<OtSlot> d_slots;
            ObjectTable():d_slots( 0xffff >> 1 ) {}
            Object* allocate(quint16 slot, quint16 numOfBytes, OOP cls, bool isPtr );
            void free( quint16 slot );
        };

        ObjectTable d_ot;
        const OtSlot& getSlot( OOP oop ) const;
        QSet<quint16> d_objects, d_classes, d_metaClasses;
        QVector<quint16> d_registers;
        QSet<quint16> d_temps;
        Xref d_xref;
    };
}

#endif // ST_OBJECT_MEMORY_2_H
