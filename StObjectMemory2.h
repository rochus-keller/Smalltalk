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
#include <QQueue>

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
            processor = 0x08, // an Association whose value field is Processor, SchedulerAssociationPointer
            smalltalk = 0x12, // an Association whose value field is SystemDirectory

            // classes
            classSmallInteger = 0x0c, // max 16383 (16384), min -16384 (-16385), bits 14, bytes 2
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
            classLargeNegativeInteger = 0x1da0,
            classProcess = 0x7a4,
            classAssociation = 0x84,
            currentSelection = 0x2392, // ParagraphEditor.CurrentSelection
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
            quint32 d_byteLen;
            ByteString(const quint8* b, quint32 l):d_bytes(b),d_byteLen(l){}
            quint16 getWordLen() const { return ( d_byteLen + 1 ) / 2; }
        };

        ObjectMemory2(QObject* p = 0);
        bool readFrom( QIODevice* );
        void collectGarbage();
        void updateRefs();

        QList<quint16> getAllValidOop() const;
        const QSet<quint16>& getObjects() const {return d_objects; }
        const QSet<quint16>& getClasses() const {return d_classes; }
        const QSet<quint16>& getMetaClasses() const {return d_metaClasses; }
        int getOopsLeft() const;
        typedef QHash<quint16, QList<quint16> > Xref;
        const Xref& getXref() const { return d_xref; }
        void setRegister( quint8 index, quint16 value );
        inline quint16 getRegister( quint8 index ) const;
        void addTemp(OOP oop);
        void removeTemp(OOP oop);
        OOP getNextInstance( OOP cls, OOP cur = 0 ) const;
        QByteArray prettyValue( OOP oop ) const;

        // oop 0 is reserved as an invalid object pointer!

        bool hasPointerMembers( OOP objectPointer ) const;
        inline OOP fetchPointerOfObject( quint16 fieldIndex, OOP objectPointer ) const;
        inline void storePointerOfObject( quint16 fieldIndex, OOP objectPointer, OOP withValue );
        quint16 fetchWordOfObject( quint16 fieldIndex, OOP objectPointer ) const;
        void storeWordOfObject( quint16 fieldIndex, OOP objectPointer, quint16 withValue );
        quint8 fetchByteOfObject( quint16 byteIndex, OOP objectPointer ) const;
        void storeByteOfObject( quint16 byteIndex, OOP objectPointer, quint8 withValue );
        OOP fetchClassOf( OOP objectPointer ) const;
        quint16 fetchByteLenghtOf( OOP objectPointer ) const;
        quint16 fetchWordLenghtOf( OOP objectPointer ) const;
        ByteString fetchByteString( OOP objectPointer ) const;
        QByteArray fetchByteArray(OOP objectPointer , bool rawData = false) const;
        float fetchFloat( OOP objectPointer ) const;
        void storeFloat( OOP objectPointer, float v );
        void swapPointersOf( OOP firstPointer, OOP secondPointer );
        bool hasObject(OOP) const;
        QByteArrayList allInstVarNames(OOP cls, bool recursive = true );

        OOP instantiateClassWithPointers( OOP classPointer, quint16 instanceSize );
        OOP instantiateClassWithWords( OOP classPointer, quint16 instanceSize );
        OOP instantiateClassWithBytes( OOP classPointer, quint16 instanceByteSize );
        QByteArray fetchClassName( OOP classPointer ) const;

        inline quint16 headerOf( OOP methodPointer ) const;
        quint8 temporaryCountOf( OOP methodPointer ) const; // including args
        CompiledMethodFlags flagValueOf( OOP methodPointer ) const;
        bool largeContextFlagOf( OOP methodPointer ) const;
        quint8 literalCountOf( OOP methodPointer ) const;
        ByteString methodBytecodes(OOP methodPointer , int* startPc = 0) const;
        quint8 argumentCountOf(OOP methodPointer ) const;
        quint8 primitiveIndexOf(OOP methodPointer ) const;
        OOP literalOfMethod(quint8 index, OOP methodPointer ) const;
        quint32 initialInstructionPointerOfMethod( OOP methodPointer ) const;
        OOP methodClassOf( OOP methodPointer ) const;
        quint8 fieldIndexOf( OOP methodPointer ) const { return temporaryCountOf(methodPointer); }
        quint16 objectPointerCountOf( OOP methodPointer ) const { return literalCountOf(methodPointer) + 1; }
        // last literal contains Association to superclass in case of super call

        static bool isPointer(OOP);
        static bool isIntegerObject(OOP objectPointer);
        static qint16 integerValueOf(OOP objectPointer , bool doAssert = false);
        static OOP integerObjectOf(qint16 value );
        static bool isIntegerValue(int);
        int largeIntegerValueOf(OOP integerPointer) const;
        static inline qint16 bitShift( qint16 wordToShift, qint16 offset );

    protected:
        int findFreeSlot();
        OOP instantiateClass(OOP cls, quint32 byteLen, bool isPtr );
        void mark(OOP);

        static inline quint16 readU16( const QByteArray& data, int off )
        {
            Q_ASSERT( off + 1 < data.size() );
            return ( quint8(data[off]) << 8 ) + quint8(data[off+1] );
        }

        static inline quint16 readU16( const quint8* data, int off )
        {
            return ( quint8(data[off]) << 8 ) + quint8(data[off+1] );
        }

        static inline void writeU16( quint8* data, int off, quint16 val )
        {
            data[off] = ( val >> 8 ) & 0xff;
            data[off+1] = val & 0xff;
        }

    private:
        struct Object
        {
            enum Flags { Marked };
            std::bitset<8> d_flags;
            quint8 d_data[1]; // variable length
        };

        struct OtSlot
        {
            quint16 d_size; // number of words (16 bit per word); we need full 16 bit here!
            quint16 d_class;    // NOTE: this is an index, not an OOP!
            quint8 d_isOdd : 1;
            quint8 d_isPtr : 1;
            Object* d_obj;
            OtSlot():d_obj(0),d_size(0),d_isOdd(0),d_class(0),d_isPtr(0) {}
            bool isFree() const { return d_obj == 0; }
            OOP getClass() const { return d_class << 1; }
            quint32 byteLen() const { return ( d_size << 1 ) - ( d_isOdd ? 1 : 0 ); }
        };

        struct ObjectTable
        {
            QVector<OtSlot> d_slots;
            ObjectTable():d_slots( 0xffff >> 1 ) {}
            OtSlot* allocate(quint16 slot, quint32 numOfBytes, OOP cls, bool isPtr );
            void free( quint16 slot );
        };

        ObjectTable d_ot;
        inline const OtSlot& getSlot( OOP oop ) const;
        QSet<quint16> d_objects, d_classes, d_metaClasses;
        QVector<quint16> d_registers;
        QSet<quint16> d_temps;
        QQueue<quint16> d_freeSlots;
        Xref d_xref;
    };

    const ObjectMemory2::OtSlot& ObjectMemory2::getSlot(ObjectMemory2::OOP oop) const
    {
        const int i = oop >> 1;
        Q_ASSERT( i < d_ot.d_slots.size() );
        return d_ot.d_slots[i];
    }

    ObjectMemory2::OOP ObjectMemory2::fetchPointerOfObject(quint16 fieldIndex, OOP objectPointer) const
    {
        if( objectPointer == objectNil )
            return objectNil;

        const OtSlot& s = getSlot(objectPointer);
        const quint32 off = fieldIndex * 2;

//        OOP spec = fetchPointerOfObject2(2,s.getClass());
//        if( ( spec & 0x8000 ) == 0 && s.getClass() != ObjectMemory2::classCompiledMethod )
//            QByteArray("WARNING: accessing word or byte object by pointer"); // never happened so far; happens in method literals by primitiveObjectAt

        Q_ASSERT( fieldIndex < s.d_size );
        const OOP oop = readU16( s.d_obj->d_data, off );
        if( oop == 0 ) // BB (implicitly?) assumes that unused members are nil
            return objectNil;
        else
            return oop;
    }

    quint16 ObjectMemory2::getRegister(quint8 index) const
    {
        if( index < d_registers.size() )
            return d_registers[index];
        else
            return 0;
    }

    qint16 ObjectMemory2::bitShift(qint16 wordToShift, qint16 offset)
    {
        if( offset >= 0 )
            return wordToShift << offset;
        else
        {
            offset = -offset;
            if( wordToShift < 0 )
            {
                // BB: the sign bit is extended in right shifts
                // see also https://stackoverflow.com/questions/1857928/right-shifting-negative-numbers-in-c
                // and https://stackoverflow.com/questions/31879878/how-can-i-perform-arithmetic-right-shift-in-c-in-a-portable-way
                quint16 tmp = wordToShift;
#if 0
                for( int i = 0; i < offset; i++ )
                {
                    tmp >>= 1;
                    tmp |= 0x8000;
                }
#else
                const quint16 tmp2 = -((wordToShift & (1u << 15)) >> offset);
                tmp = tmp >> offset | tmp2;
//              qDebug() << "before" << QByteArray::number(quint16(wordToShift),2)
//                     << "after" << QByteArray::number(tmp,2)
//                     << "tmp2" << QByteArray::number(tmp2,2);
#endif
                return tmp;
            }else
                return wordToShift >> offset;
        }
    }

    void ObjectMemory2::storePointerOfObject(quint16 fieldIndex, OOP objectPointer, OOP withValue)
    {
        Q_ASSERT( objectPointer != 0 );
        if( objectPointer == objectNil )
            return;
        const OtSlot& s = getSlot(objectPointer);
        const quint32 off = fieldIndex * 2;
        Q_ASSERT( fieldIndex < s.d_size );
        writeU16( s.d_obj->d_data, off, withValue );
    }

    quint16 ObjectMemory2::headerOf(ObjectMemory2::OOP methodPointer) const
    {
        return fetchPointerOfObject(0,methodPointer); // HeaderIndex
    }

}

#endif // ST_OBJECT_MEMORY_2_H
