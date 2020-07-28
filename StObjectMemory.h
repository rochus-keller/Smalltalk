#ifndef ST_OBJECT_MEMORY_H
#define ST_OBJECT_MEMORY_H

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

class QIODevice;

namespace St
{
    class ObjectMemory : public QObject
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
            classLargeNegativeInteger = 0x1da0,
            classProcess = 0x7a4,
            classAssociation = 0x84,
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

        enum ClassIndizes {
            CI_superClass = 0,
            CI_messageDict = 1, // points to a MessageDictionary
            CI_instanceSpec = 2,
        };

        struct ByteString
        {
            const quint8* d_bytes;
            quint16 d_byteLen;
            ByteString(const quint8* b, quint16 l):d_bytes(b),d_byteLen(l){}
        };

        ObjectMemory(QObject* p = 0);
        bool readFrom( QIODevice* );
        void collectGarbage();
        void updateRefs();
        bool isBigEndian() const { return d_bigEndian; }

        QList<quint16> getAllValidOop() const;
        const QSet<quint16>& getObjects() const {return d_objects; }
        const QSet<quint16>& getClasses() const {return d_classes; }
        const QSet<quint16>& getMetaClasses() const {return d_metaClasses; }
        typedef QHash<quint16, QList<quint16> > Xref;
        const Xref& getXref() const { return d_xref; }
        QByteArray prettyValue( quint16 oop ) const;

        // oop 0 is reserved as an invalid object pointer!

        bool hasPointerMembers( quint16 objectPointer ) const;
        quint16 fetchPointerOfObject( quint16 fieldIndex, quint16 objectPointer ) const;
        void storePointerOfObject( quint16 fieldIndex, quint16 objectPointer, quint16 withValue );
        quint16 fetchWordOfObject( quint16 fieldIndex, quint16 objectPointer ) const;
        void storeWordOfObject( quint16 fieldIndex, quint16 objectPointer, quint16 withValue );
        quint8 fetchByteOfObject( quint16 byteIndex, quint16 objectPointer ) const;
        void storeByteOfObject( quint16 byteIndex, quint16 objectPointer, quint8 withValue );
        quint16 fetchClassOf( quint16 objectPointer ) const;
        quint16 fetchByteLenghtOf( quint16 objectPointer ) const;
        quint16 fetchWordLenghtOf( quint16 objectPointer ) const;
        ByteString fetchByteString( quint16 objectPointer ) const;
        QByteArray fetchByteArray(OOP objectPointer , bool rawData = false) const;
        float fetchFloat( quint16 objectPointer ) const;

        quint16 instantiateClassWithPointers( quint16 classPointer, quint16 instanceSize );
        quint16 instantiateClassWithWords( quint16 classPointer, quint16 instanceSize );
        quint16 instantiateClassWithBytes( quint16 classPointer, quint16 instanceByteSize );
        QByteArray fetchClassName( quint16 classPointer ) const;

        quint8 temporaryCountOf( quint16 methodPointer ) const; // including args
        CompiledMethodFlags flagValueOf( quint16 methodPointer ) const;
        bool largeContextFlagOf( quint16 methodPointer ) const;
        quint8 literalCountOf( quint16 methodPointer ) const;
        ByteString methodBytecodes( quint16 methodPointer, int* startPc = 0 ) const;
        quint8 argumentCountOf(quint16 methodPointer ) const;
        quint8 primitiveIndexOf(quint16 methodPointer ) const;
        quint16 literalOfMethod(quint8 index, quint16 methodPointer ) const;
        // last literal contains Association to superclass in case of super call

        static bool isPointer(quint16);
        static bool isIntegerObject(quint16 objectPointer);
        static qint16 integerValueOf(quint16 objectPointer );
        int largeIntegerValueOf(quint16 integerPointer) const;

    protected:
        void printObjectTable();
        void printObjectSpace();
        quint32 getSpaceAddr(quint16 oop , bool* odd = 0, bool* ptr = 0) const;
        quint16 getClassOf( quint16 oop ) const;
        struct Data
        {
            quint32 d_pos;
            quint32 d_len : 30;
            quint32 d_isPtr : 1;
            quint32 d_isOdd : 1;
            Data():d_pos(0),d_len(0),d_isPtr(false),d_isOdd(false){}
            quint32 getLen() const { return d_len - ( d_isOdd ? 1 : 0 ); }
        };
        Data getDataOf(quint16 oop , bool noHeader = true ) const;
        qint32 findNextFree();
        quint16 createInstance(quint16 classPtr, quint16 byteLen, bool isPtr );

    private:
        QByteArray d_objectSpace; // first word is oop_2; contains no unused/free space
        QByteArray d_objectTable; // first word is oop_0; entries are oop or unused (freeEntry bit set)
        // objects are stored continuously, no segment boundaries
        // all objectTable entries are the same size
        QSet<quint16> d_objects, d_classes, d_metaClasses;
        Xref d_xref;
        bool d_bigEndian;
    };
}

#endif // ST_OBJECT_MEMORY_H
