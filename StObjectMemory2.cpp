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

#include "StObjectMemory2.h"
#include <QIODevice>
#include <QtDebug>
#include <QtMath>
#include <limits.h>
using namespace St;

// According to "Smalltalk-80: Virtual Image Version 2", Xerox PARC, 1983
// see http://www.wolczko.com/st80/manual.pdf.gz
// and the Blue Book

//#define _ST_COUNT_INSTS_

#ifdef _ST_COUNT_INSTS_
static QHash<ObjectMemory2::OOP,int> s_countByClass;
#endif

ObjectMemory2::ObjectMemory2(QObject* p):QObject(p)
{

}

static inline bool isFree(quint8 flags ) { return flags & 0x20; }
static inline bool isPtr(quint8 flags ) { return flags & 0x40; }
static inline bool isOdd(quint8 flags ) { return flags & 0x80; }
static inline bool isInt(quint16 ptr ) { return ptr & 1; }

static quint32 readU32( QIODevice* in )
{
    const QByteArray buf = in->read(4);
    if( buf.size() != 4 )
        return 0;
    return ( quint8(buf[0]) << 24 ) + ( quint8(buf[1]) << 16 ) +
            ( quint8(buf[2]) << 8 ) + quint8(buf[3] );
}

static inline void writeU16( QByteArray& data, int off, quint16 val )
{
    Q_ASSERT( off + 1 < data.size() );
    data[off] = ( val >> 8 ) & 0xff;
    data[off+1] = val & 0xff;
}

static inline quint16 extractBits( quint8 from, quint8 to, quint16 of )
{
    Q_ASSERT( from <= to && to <= 15 );
    return ( of >> ( 15 - to ) ) & ( ( 1 << ( to - from + 1 ) ) - 1 );
}

static const int methHdrByteLen = 2;
static const int ValueIndex = 1;

static inline quint8 getLiteralByteCount( const quint8* space )
{
    return 2 * ( ( space[1] >> 1 ) & 0x3f );
}

static inline quint8 getMethodFlags( quint8 data )
{
    return ( data >> 5 ) & 0x7;
}

bool ObjectMemory2::readFrom(QIODevice* in)
{
    const quint32 objectSpaceLenWords = readU32(in);
    const quint32 objectSpaceLenBytes = objectSpaceLenWords * 2;
    const quint32 objectTableLenWords = readU32(in);
    const quint32 objectTableLenBytes = objectTableLenWords * 2;
    const QByteArray nineTen = in->read(2);

    if( nineTen.size() != 2 || nineTen[0] != 0x0 || nineTen[1] != 0x0 )
        return false; // not in interchange format

    in->seek( in->size() - 10 );
    const QByteArray last = in->read(10);
    if( last.size() != 10 || last[3] != 0x20 || last[6] != 0x01 ||
            quint8(last[7]) != 0x43 || quint8(last[8]) != 0xf3 || quint8(last[9]) != 0x3b )
        return false;

    in->seek( 512 );

    // qDebug() << "object space" << objectSpaceLenBytes << "bytes, object table" << objectTableLenBytes << "bytes";

    // Object Space format:
    // | xxxxxxxx xxxxxxxx | word length
    // | xxxxxxxx xxxxxxxx | class
    // | payload of word lenght - 2

    QByteArray objectSpace = in->read( objectSpaceLenBytes );
    if( objectSpace.size() != objectSpaceLenBytes )
        return false;

    const int numOfPages = objectSpaceLenBytes / 512;
    const int off = 512 + ( numOfPages + 1 ) * 512;
    in->seek( off );
    // Object Table format:
    // | xxxxxxxx | count, unused
    // | ffffssss | flags, segment
    // | llllllll llllllll | location
    QByteArray objectTable = in->read( objectTableLenBytes );
    if( objectTable.size() != objectTableLenBytes )
        return false;

    for( int i = 0; i < objectTable.size(); i += 4 )
    {        
        const quint8 flags = quint8(objectTable[i+1]);

        if( isFree(flags) )
            continue;
        const quint16 loc = readU16(objectTable,i+2);
        // loc is the word index, not byte index
        const quint8 seg = flags & 0xf; // segment number
        // in Bits of History page 49: each segment contains 64k words (not bytes)
        const quint32 addr = ( seg << 17 ) + ( loc << 1 );
        const quint16 wordLen = readU16( objectSpace, addr ) - 2; // without header
        Q_ASSERT( wordLen < ( 0xffff >> 1 ) );
        const quint16 byteLen = wordLen * 2;

        const quint16 cls = readU16(objectSpace, addr + 2 );

        const quint16 slotNr = i >> 2; // OOP are only even number, slotNr are also odd number, so OOP/2, i.e. i/4

        OtSlot* slot = d_ot.allocate( slotNr, byteLen, cls, isPtr(flags) );
        Q_ASSERT( slot != 0 );
        slot->d_isOdd = isOdd(flags);
        ::memcpy( slot->d_obj->d_data, objectSpace.constData() + addr + 4, byteLen ); // without header
    }

    updateRefs();

    return true;
}

QList<quint16> ObjectMemory2::getAllValidOop() const
{
    QList<quint16> res;
    for( int i = 0; i < d_ot.d_slots.size(); i++ )
    {
        if( d_ot.d_slots[i].isFree() )
            continue;
        const quint16 oop = ( i << 1 );
        res << oop;
    }
    return res;
}

int ObjectMemory2::getOopsLeft() const
{
    int count = 0;
    for( int i = 0; i < d_ot.d_slots.size(); i++ )
        if( d_ot.d_slots[i].isFree() )
            count++;
    return count;
}

void ObjectMemory2::setRegister(quint8 index, quint16 value)
{
    if( index >= d_registers.size() )
        d_registers.resize( d_registers.size() + 10 );
    d_registers[index] = value;
}

void ObjectMemory2::addTemp(OOP oop)
{
    d_temps.insert(oop);
}

void ObjectMemory2::removeTemp(ObjectMemory2::OOP oop)
{
    d_temps.remove(oop);
}

ObjectMemory2::OOP ObjectMemory2::getNextInstance(ObjectMemory2::OOP cls, ObjectMemory2::OOP cur) const
{
    int start = 0;
    if( cur )
        start = ( cur >> 1 ) + 1;
    for( int i = start; i < d_ot.d_slots.size(); i++ )
    {
        if( d_ot.d_slots[i].isFree() )
            continue;
        if( d_ot.d_slots[i].getClass() == cls )
            return i << 1;
    }
    return 0;
}

QByteArray ObjectMemory2::prettyValue(ObjectMemory2::OOP oop) const
{
    switch( oop )
    {
    case objectNil:
        return "nil";
    case objectFalse:
        return "false";
    case objectTrue:
        return "true";
    case processor:
        return "processor";
    case smalltalk:
        return "smalltalk";
    case symbolTable:
        return "symbolTable";
    case symbolDoesNotUnderstand:
        return "symbolDoesNotUnderstand";
    case symbolCannotReturn:
        return "symbolCannotReturn";
    case symbolMonitor:
        return "symbolMonitor";
    case symbolUnusedOop18:
        return "symbolUnusedOop18";
    case symbolMustBeBoolean:
        return "symbolMustBeBoolean";
    case specialSelectors:
        return "specialSelectors";
    case characterTable:
        return "characterTable";
    case 0:
        return "<invalid oop>";
    }
    OOP cls = fetchClassOf(oop);
    switch( cls )
    {
    case classSmallInteger:
        return QByteArray::number( integerValueOf(oop) );
    case classLargePositiveInteger:
        return QByteArray::number( largeIntegerValueOf(oop) ) + "L";
    case classLargeNegativeInteger:
        return QByteArray::number( -largeIntegerValueOf(oop) ) + "L";
    case classString:
        {
            const int maxlen = 40;
            QByteArray str = fetchByteArray(oop).simplified();
            if( str.size() > maxlen )
                return "\"" + str.left(maxlen) + "\"...";
            else
                return "\"" + str + "\"";
        }
    case classFloat:
        return QByteArray::number( fetchFloat(oop) );
    case classPoint:
        {
            OOP x = fetchPointerOfObject( 0, oop );
            OOP y = fetchPointerOfObject( 1, oop );
            return prettyValue(x) + "@" + prettyValue(y);
        }
    case classCharacter:
        {
            quint16 ch = fetchWordOfObject(0,oop);
            ch = ch >> 1;
            if( ::isprint(ch) )
                return "'" + QByteArray(1,ch) + "'";
            else
                return "0x" + QByteArray::number(ch,16);

        }
    case classSymbol:
        return "#" + fetchByteArray(oop);
    case classAssociation:
        return prettyValue( fetchPointerOfObject( 0, oop ) ) + " = " +
                    prettyValue( fetchPointerOfObject( 1, oop ) );
    case 0:
        return "<instance" + QByteArray::number(oop,16) + "with invalid class oop>";
    }
    return "<a " + fetchClassName(cls) + ">";
}

bool ObjectMemory2::hasPointerMembers(OOP objectPointer) const
{
    if( isInt(objectPointer) )
        return false;
    const OtSlot& s = getSlot(objectPointer);
    return s.d_isPtr;
}

quint16 ObjectMemory2::fetchWordOfObject(quint16 fieldIndex, OOP objectPointer) const
{
    if( objectPointer == objectNil )
        return 0;

    const OtSlot& s = getSlot(objectPointer);
    const quint32 off = fieldIndex * 2;

//    OOP spec = fetchPointerOfObject(2,s.getClass());
//    if( spec & 0x8000 || ( spec & 0x4000 ) == 0 )
//        qWarning() << "WARNING: accessing pointer or byte object by word"; // with recent fixes never happened so far

    Q_ASSERT( fieldIndex < s.d_size );
    return readU16( s.d_obj->d_data, off );
}

void ObjectMemory2::storeWordOfObject(quint16 fieldIndex, OOP objectPointer, quint16 withValue)
{
    const OtSlot& s = getSlot(objectPointer);
    const quint32 off = fieldIndex * 2;
    Q_ASSERT( fieldIndex < s.d_size );
    writeU16( s.d_obj->d_data, off, withValue );
}

quint8 ObjectMemory2::fetchByteOfObject(quint16 byteIndex, OOP objectPointer) const
{
    const OtSlot& s = getSlot(objectPointer);
    const quint32 off = byteIndex;

//    OOP spec = fetchPointerOfObject(2,s.getClass());
//    if( spec & 0x8000 || spec & 0x4000 )
//        qWarning() << "WARNING: accessing pointer or word object by bytes"; // never happened so far

    Q_ASSERT( !s.d_isPtr && off < s.byteLen() );
    return s.d_obj->d_data[off];
}

void ObjectMemory2::storeByteOfObject(quint16 byteIndex, OOP objectPointer, quint8 withValue)
{
    const OtSlot& s = getSlot(objectPointer);
    const quint32 off = byteIndex;
    Q_ASSERT( !s.d_isPtr && off < s.byteLen() );
    s.d_obj->d_data[off] = withValue;
}

ObjectMemory2::OOP ObjectMemory2::fetchClassOf(OOP objectPointer) const
{
    if( !isPointer(objectPointer) )
        return classSmallInteger;
    else
        return getSlot( objectPointer ).getClass();
}

quint16 ObjectMemory2::fetchByteLenghtOf(OOP objectPointer) const
{
    if( isInt(objectPointer) )
        return 0;
    const OtSlot& s = getSlot(objectPointer);
    return s.byteLen();
}

quint16 ObjectMemory2::fetchWordLenghtOf(OOP objectPointer) const
{
    quint16 len = fetchByteLenghtOf(objectPointer);
    if( len & 0x01 )
        len++;
    return len / 2;
}

ObjectMemory2::OOP ObjectMemory2::instantiateClassWithPointers(OOP classPointer, quint16 instanceSize)
{
    return instantiateClass( classPointer, instanceSize << 1, true );
}

ObjectMemory2::OOP ObjectMemory2::instantiateClassWithWords(OOP classPointer, quint16 instanceSize)
{
    return instantiateClass( classPointer, instanceSize << 1, false );
}

ObjectMemory2::OOP ObjectMemory2::instantiateClassWithBytes(OOP classPointer, quint16 instanceByteSize)
{
    return instantiateClass( classPointer, instanceByteSize, false );
}

ObjectMemory2::ByteString ObjectMemory2::fetchByteString(OOP objectPointer) const
{
    if( isInt(objectPointer) )
        return ByteString(0,0);
    const OtSlot& s = getSlot(objectPointer);
    return ByteString( s.d_obj->d_data, s.byteLen() );
}

QByteArray ObjectMemory2::fetchByteArray(ObjectMemory2::OOP objectPointer, bool rawData ) const
{
    ByteString bs = fetchByteString(objectPointer);
    if( bs.d_bytes )
    {
        if( rawData )
            return QByteArray::fromRawData( (const char*)bs.d_bytes, bs.d_byteLen );
        else
            return (const char*)bs.d_bytes;
    }else
        return QByteArray();
}

float ObjectMemory2::fetchFloat(ObjectMemory2::OOP objectPointer) const
{
    // Examples of Float instance bytes
    // 00000000 0.0
    // 3f800000 1.0
    // 3e999999 0.3
    // 40000000 2.0
    // 40c00000 6.0
    // 40800000 4.0
    // 4048f5c2 3.14
    // 3e6b851e 0.23

    Q_ASSERT( fetchByteLenghtOf(objectPointer) == 4 );
    union {
        float f;
        quint32 w;
    };
#ifdef _DEBUG_
    // The BB corresponds to the IEEE 754 32 bit floating point version
    w = 0x4048f5c2; // 3.14 rounded
    Q_ASSERT( qFabs( f - 3.1399998 ) < 0.0000001 );
    union {
        quint8 b[4];
        quint32 w2;
    };
    b[3] = 0x40; b[2] = 0x48; b[1] = 0xf5; b[0] = 0xc2;
    QByteArray buf = QByteArray::fromHex("4048f5c2");
    Q_ASSERT( buf[0] == b[3] && buf[1] == b[2] && buf[2] == b[1] && buf[3] == b[0] );
    Q_ASSERT( w2 == w );
#endif
    w = fetchWordOfObject(0,objectPointer) << 16 | fetchWordOfObject(1,objectPointer);
    return f;
}

void ObjectMemory2::storeFloat(ObjectMemory2::OOP objectPointer, float v)
{
    Q_ASSERT( fetchByteLenghtOf(objectPointer) == 4 );
    union {
        float f;
        quint32 w;
    };
    f = v;
    storeWordOfObject(0,objectPointer,( w >> 16 ) & 0xffff);
    storeWordOfObject(1,objectPointer, w & 0xffff );
}

void ObjectMemory2::swapPointersOf(OOP firstPointer, OOP secondPointer)
{
    const quint32 i1 = firstPointer >> 1;
    const quint32 i2 = secondPointer >> 1;
    Q_ASSERT( i1 < d_ot.d_slots.size() && i2 < d_ot.d_slots.size() );
    OtSlot tmp =  d_ot.d_slots[i1];
    d_ot.d_slots[i1] = d_ot.d_slots[i2];
    d_ot.d_slots[i2] = tmp;
}

bool ObjectMemory2::hasObject(OOP ptr) const
{
    const quint32 i = ptr >> 1;
    if( i < d_ot.d_slots.size() )
        return !d_ot.d_slots[i].isFree();
    else
        return false;
}

QByteArrayList ObjectMemory2::allInstVarNames(ObjectMemory2::OOP cls, bool recursive)
{
    QByteArrayList res;
    if( recursive )
    {
        OOP super = fetchPointerOfObject(0,cls);
        if( super && super != objectNil )
            res += allInstVarNames(super,recursive);
    }
    const quint16 vars = fetchPointerOfObject(4,cls);
    if( vars != objectNil )
    {
        const quint16 len = fetchWordLenghtOf(vars);
        for( int i = 0; i < len; i++ )
        {
            const quint16 str = fetchPointerOfObject(i,vars);
            res << (const char*) fetchByteString(str).d_bytes;
        }
    }
    return res;
}

QByteArray ObjectMemory2::fetchClassName(OOP classPointer) const
{
    if( d_classes.contains(classPointer) )
    {
        const quint16 sym = fetchPointerOfObject(6, classPointer);
        //Q_ASSERT( fetchClassOf(sym) == classSymbol );
        return (const char*)fetchByteString(sym).d_bytes;
    }else if( d_metaClasses.contains(classPointer) )
    {
        const quint16 nameId = fetchPointerOfObject(6, classPointer);
        //const quint16 nameCls = fetchClassOf(nameId);
        //Q_ASSERT( nameCls != classSymbol );
        //Q_ASSERT( nameCls == classPointer );
        const quint16 sym = fetchWordOfObject(6, nameId);
        //Q_ASSERT( fetchClassOf(sym) == classSymbol );
        const ByteString bs = fetchByteString(sym);
        return QByteArray((const char*)bs.d_bytes) + " class";
    }else if( d_objects.contains(classPointer) )
    {
        OOP cls = fetchClassOf(classPointer);
        qWarning() << "WARNING: fetchClassName oop" << QByteArray::number(classPointer,16).constData() <<
                      "is an instance of" << fetchClassName(cls);
        return "not a class";
    }
    return QByteArray();
}

quint8 ObjectMemory2::temporaryCountOf(OOP methodPointer) const
{
    Q_ASSERT(methodPointer);
#if 0
    const OtSlot& s = getSlot(methodPointer);
    Q_ASSERT( s.getClass() == ObjectMemory2::classCompiledMethod );
    return s.d_obj->d_data[0] & 0x1f;
#else
    const OOP header = headerOf(methodPointer);
    return extractBits(3,7,header);
#endif
}

ObjectMemory2::CompiledMethodFlags ObjectMemory2::flagValueOf(OOP methodPointer) const
{
    Q_ASSERT(methodPointer);
    const OtSlot& s = getSlot(methodPointer);
    Q_ASSERT( s.getClass() == ObjectMemory2::classCompiledMethod );
    return CompiledMethodFlags( getMethodFlags( s.d_obj->d_data[0] ) );
}

bool ObjectMemory2::largeContextFlagOf(OOP methodPointer) const
{
    Q_ASSERT(methodPointer);
#if 0
    const OtSlot& s = getSlot(methodPointer);
    Q_ASSERT( s.getClass() == ObjectMemory2::classCompiledMethod );
    return ( s.d_obj->d_data[1] & 0x80 );
#else
    const OOP header = headerOf(methodPointer);
    return extractBits(8,8,header);
#endif
}

quint8 ObjectMemory2::literalCountOf(OOP methodPointer) const
{
    Q_ASSERT(methodPointer);
#if 0
    const OtSlot& s = getSlot(methodPointer);
    Q_ASSERT( s.getClass() == ObjectMemory2::classCompiledMethod );
    return getLiteralByteCount( s.d_obj->d_data ) / 2;
#else
    const OOP header = headerOf(methodPointer);
    return extractBits(9,14,header);
#endif
}

ObjectMemory2::ByteString ObjectMemory2::methodBytecodes(OOP methodPointer, int* startPc) const
{
    const OtSlot& s = getSlot(methodPointer);
    Q_ASSERT( s.getClass() == ObjectMemory2::classCompiledMethod );
    const quint8 literalByteCount = getLiteralByteCount( s.d_obj->d_data );
    const int offset = methHdrByteLen + literalByteCount;
    if( startPc )
        *startPc = offset + 1;
    const quint8* bytes = s.d_obj->d_data + offset;
    const quint32 byteLen = s.byteLen() - offset;
    return ByteString( bytes, byteLen );
}

quint8 ObjectMemory2::argumentCountOf(OOP methodPointer) const
{
    const OtSlot& s = getSlot(methodPointer);
    Q_ASSERT( s.getClass() == ObjectMemory2::classCompiledMethod );
    const quint8 flags = getMethodFlags(s.d_obj->d_data[0]);
    if( flags <= FourArguments )
        return flags;
    else if( flags == ZeroArgPrimitiveReturnSelf || flags == ZeroArgPrimitiveReturnVar )
        return 0;
    Q_ASSERT( flags == HeaderExtension );
    const quint8 literalByteCount = getLiteralByteCount(s.d_obj->d_data);
    const quint16 extension = readU16( s.d_obj->d_data, methHdrByteLen
                                       + literalByteCount - 4 ); // next to the last literal
    return ( extension >> 9 ) & 0x1f;
}

quint8 ObjectMemory2::primitiveIndexOf(OOP methodPointer) const
{
    const OtSlot& s = getSlot(methodPointer);
    Q_ASSERT( s.getClass() == ObjectMemory2::classCompiledMethod );
    const quint8 flags = getMethodFlags( s.d_obj->d_data[0] );
    if( flags != HeaderExtension )
        return 0;
    Q_ASSERT( flags == HeaderExtension );
    const quint8 literalByteCount = getLiteralByteCount(s.d_obj->d_data);
    const quint16 extension = readU16( s.d_obj->d_data, methHdrByteLen
                                       + literalByteCount - 4 ); // next to the last literal
    return ( extension >> 1 ) & 0xff;
}

ObjectMemory2::OOP ObjectMemory2::literalOfMethod(quint8 index, OOP methodPointer) const
{
    const OtSlot& s = getSlot(methodPointer);
    Q_ASSERT( s.getClass() == ObjectMemory2::classCompiledMethod );
    const quint8 literalByteCount = getLiteralByteCount(s.d_obj->d_data);
    const quint16 byteIndex = 2 * index;
    Q_ASSERT( byteIndex < literalByteCount );
    return readU16( s.d_obj->d_data, methHdrByteLen + byteIndex );
}

quint32 ObjectMemory2::initialInstructionPointerOfMethod(ObjectMemory2::OOP methodPointer) const
{
    return ( literalCountOf(methodPointer) + ValueIndex ) * 2 + 1;
}

ObjectMemory2::OOP ObjectMemory2::methodClassOf(ObjectMemory2::OOP methodPointer) const
{
    const quint16 literalCount = literalCountOf(methodPointer);
    OOP association = literalOfMethod(literalCount-1, methodPointer);
    return fetchPointerOfObject(ValueIndex,association);
}

bool ObjectMemory2::isPointer(OOP ptr)
{
    return !isInt(ptr);
}

bool ObjectMemory2::isIntegerObject(ObjectMemory2::OOP objectPointer)
{
    return isInt(objectPointer);
}

qint16 ObjectMemory2::integerValueOf(OOP objectPointer, bool doAssert)
{
    if( isInt(objectPointer) )
    {
        quint16 tcomp = ( objectPointer >> 1 );
        if( tcomp & 0x4000 )
        {
            qint16 res = -( ~tcomp & 0x7fff ) - 1;
            return res;
        }else
            return tcomp;
    }else if( doAssert )
        Q_ASSERT( false );
    // else
        return 0;
}

ObjectMemory2::OOP ObjectMemory2::integerObjectOf(qint16 value)
{
    OOP res = 0;
    if( value >= 0 )
        res = ( value << 1 ) | 1;
    else
    {
        res = ~::abs(value) + 1;
        res = ( res << 1 ) | 1;
        // Q_ASSERT( integerValueOf(res) == value );
    }
    return res;
}

bool ObjectMemory2::isIntegerValue(int valueWord)
{
    // BB description: Return true if value can be represented as an instance of SmallInteger, false if not
    // BB states "valueWord <= -16384 && valueWord > 16834;" which contradicts with the description
    // Note the additional typo error in "16834" which should state "16384"!
    return valueWord >= -16384 && valueWord <= 16383;
}

int ObjectMemory2::largeIntegerValueOf(OOP integerPointer) const
{
    // TODO: LargePositiveInteger can have 4 bytes
    // Examples in VirtualImage:
    // z.B. SecondsInDay ← 24 * 60 * 60 = 86'400 -> 0x80510100
    // z.B. ExternalStream.nextSignedInteger 2fa4 0x00000100 -> 65'536
    // also 6532, 65a0
    // z.B. Benchmark.testLargeIntArith 67c8 0x80380100 -> 80'000
    // z.B. WordsLeftLimit 923e 0x86730200
    // BB: this is an undocumented feature of LargePositiveInteger
    // see also Object.asOop and LargePositiveInteger.asObject

    if( isIntegerObject(integerPointer) )
        return integerValueOf(integerPointer);
    // Q_ASSERT( fetchClassOf(integerPointer) == classLargePositiveInteger );
    int value = 0;
    int len = fetchByteLenghtOf(integerPointer);
    if( len == 2 )
    {
        value = fetchByteOfObject(1, integerPointer) << 8;
        value += fetchByteOfObject(0, integerPointer);
    }else if( len == 3 )
    {
        // empirically found from examples
        value = fetchByteOfObject(0, integerPointer);
        value += fetchByteOfObject(1, integerPointer) << 8;
        value += fetchByteOfObject(2, integerPointer) * 65536;
    }else if( len == 4 )
    {
        // happens in Delay preSnapshot postSnapshot millisecondClockValue
        // see Time millisecondClockInto and primitive99 for format description
        // Milliseconds 1.1.1901 to 9.6.2020: 3'769'154'954
        // e.g. ac9fbf66 = 1723834284L 1'723'834'284
        value = fetchByteOfObject(0, integerPointer) +
                ( fetchByteOfObject(1, integerPointer) << 8 ) +
                ( fetchByteOfObject(2, integerPointer) << 16 ) +
                ( fetchByteOfObject(3, integerPointer) << 24 );
    }else if( len == 1 )
    {
        // TODO
        value = fetchByteOfObject(0,integerPointer);
    }else if( len == 0 )
        value = 0; // this obviously can happen
    else
    {
        qWarning() << "WARNING: large integer with" << len << "bytes not supported";
        Q_ASSERT( false );
        value = 0xffffffff;
    }
    return value;
}

int ObjectMemory2::findFreeSlot()
{
    // this is not the OOP but directly the d_slots index
    if( !d_freeSlots.isEmpty() )
        return d_freeSlots.dequeue();
    return -1;
}

ObjectMemory2::OOP ObjectMemory2::instantiateClass(ObjectMemory2::OOP cls, quint32 byteLen, bool isPtr)
{
#ifdef _ST_COUNT_INSTS_
    s_countByClass[cls]++;
    qDebug() << "instantiate" << prettyValue(cls) << s_countByClass[cls]; // TEST
#endif

    int slot = findFreeSlot();
    if( slot < 0 )
    {
        collectGarbage();
        slot = findFreeSlot();
    }
    if( slot < 0 )
    {
        qCritical() << "ERROR: cannot allocate object, no free object table slots";
        return 0;
    }
    if( d_ot.allocate( slot, byteLen, cls, isPtr ) == 0 )
    {
        qCritical() << "ERROR: cannot allocate object, no free memory";
        return 0;
    }
    return slot << 1;
}

void ObjectMemory2::collectGarbage()
{
#if 0 // not necessary
    for( int i = 0; i < d_ot.d_slots.size(); i++ )
    {
        const OtSlot& s = d_ot.d_slots[i];
        if( s.isFree() )
            continue;
        s.d_obj->d_flags.set(Object::Marked, false);
    }
#endif

    d_freeSlots.clear();

    // mark
    foreach( quint16 reg, d_registers )
        mark(reg);
    foreach( quint16 reg, d_temps )
        mark(reg);
    for( int oop = 0; oop <= classSymbol; oop += 2 )
    {
        mark( oop );
    }

    int count = 0;
    // sweep
    for( int i = 0; i < d_ot.d_slots.size(); i++ )
    {
        const OtSlot& s = d_ot.d_slots[i];
        if( s.isFree() )
            continue;
        if( !s.d_obj->d_flags.test(Object::Marked) )
        {
#ifdef _ST_COUNT_INSTS_
            s_countByClass[ d_ot.d_slots[i].getClass() ]--;
#endif
            d_ot.free(i);
            d_freeSlots.enqueue(i);
            count++;
        }else
            s.d_obj->d_flags.set(Object::Marked, false);
    }

    const int percent = count * 100 / d_ot.d_slots.size();
    if( percent < 40 )
    {
        qDebug() << "INFO: collectGarbage" << count << "oop available" << percent << "%";
//      exit(-1); // TEST
    }
}

void ObjectMemory2::mark(OOP oop)
{
    if( !isPointer(oop) )
        return;
    const OtSlot& s = getSlot(oop);
    if( s.isFree() )
        return;

    if( s.d_obj->d_flags.test(Object::Marked) )
        return; // already visited

    s.d_obj->d_flags.set(Object::Marked, true);

    if( s.d_isPtr )
    {
        for( int i = 0; i < s.d_size; i++ )
        {
            quint16 sub = fetchPointerOfObject(i, oop);
            if( isPointer(sub) )
                mark( sub );
        }
    }else if( s.getClass() == classCompiledMethod )
    {
        const quint16 len = literalCountOf(oop);
        for( int i = 0; i < len; i++ )
        {
            quint16 sub = literalOfMethod(i, oop);
            if( isPointer(sub) )
                mark( sub );
        }
    }

    mark( s.getClass() );
}

void ObjectMemory2::updateRefs()
{
    d_xref.clear();
    d_objects.clear();
    d_classes.clear();
    d_metaClasses.clear();
    d_freeSlots.clear();

    for( int i = 0; i < d_ot.d_slots.size(); i++ )
    {
        const OtSlot& slot = d_ot.d_slots[i];
        if( slot.isFree() )
        {
            if( i != 0 )
                d_freeSlots.enqueue(i);
            continue;
        }
        const quint16 oop = i << 1;
        Q_ASSERT( !d_objects.contains(oop) );
        d_objects << oop;

        const OOP cls = slot.getClass();
        d_classes << cls;
        d_classes << fetchPointerOfObject(0,cls); // superclass of cls
        if( cls == classCompiledMethod )
        {
            for( int j = 0; j < literalCountOf(oop); j++ )
            {
                const OOP ptr = literalOfMethod(j,oop);
                if( !isInt(ptr) && ptr != objectNil && ptr != objectTrue && ptr != objectFalse )
                    d_xref[ptr].append(oop);
            }
        }else if( hasPointerMembers(oop) )
        {

            const int len = fetchWordLenghtOf(oop);
            for( int j = 0; j < len; j++ )
            {
                quint16 ptr = fetchPointerOfObject(j,oop);
                if( !isInt(ptr) && ptr != objectNil && ptr != objectTrue && ptr != objectFalse )
                    d_xref[ptr].append(oop);
            }
        }
    }

    d_classes << classSmallInteger;

    d_objects -= d_classes;

    foreach( quint16 cls, d_classes )
    {
        const quint16 nameId = fetchPointerOfObject(6, cls);
        const quint16 nameCls = fetchClassOf(nameId);
        if( cls == nameCls && cls != classSymbol )
            d_metaClasses << cls;
    }
    d_classes -= d_metaClasses;

    QSet<quint16> corrections;
    foreach( quint16 obj, d_objects )
    {
        if( d_metaClasses.contains(fetchClassOf(obj)) )
            corrections.insert(obj); // obj is actually a class but was not identified because it has no instances
    }
    d_objects -= corrections;
    d_classes += corrections;
}

ObjectMemory2::OtSlot* ObjectMemory2::ObjectTable::allocate(quint16 slot, quint32 numOfBytes, OOP cls, bool isPtr)
{
    Q_ASSERT( slot < d_slots.size() && d_slots[slot].d_obj == 0 );
    bool isOdd = false;
    if( numOfBytes & 0x1 )
    {
        numOfBytes++;
        isOdd = true;
    }
    const int byteLen = sizeof(Object) + numOfBytes - 1;
    void* ptr = ::malloc( byteLen + 1 ); // additional 0 at end
    if( ptr == 0 )
        return 0;
    ::memset(ptr, 0, byteLen + 1 );
    OtSlot& ots = d_slots[slot];
    ots.d_obj = (Object*) ptr;
    ots.d_isOdd = isOdd;
    ots.d_isPtr = isPtr;
    ots.d_class = cls >> 1;
    ots.d_size = numOfBytes >> 1;
    return &ots;
}

void ObjectMemory2::ObjectTable::free(quint16 slot)
{
    Q_ASSERT( slot < d_slots.size() && d_slots[slot].d_obj != 0 );
    OtSlot& ots = d_slots[slot];
    ::free( ots.d_obj );
    ots.d_obj = 0;
    ots.d_class = 0;
    ots.d_size = 0;
    ots.d_isOdd = 0;
    ots.d_isPtr = 0;
}
