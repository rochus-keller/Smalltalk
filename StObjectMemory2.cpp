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
using namespace St;

// According to "Smalltalk-80: Virtual Image Version 2", Xerox PARC, 1983
// see http://www.wolczko.com/st80/manual.pdf.gz
// and the Blue Book

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

static quint16 readU16( const QByteArray& data, int off )
{
    Q_ASSERT( off + 1 < data.size() );
    return ( quint8(data[off]) << 8 ) + quint8(data[off+1] );
}

static quint16 readU16( const quint8* data, int off )
{
    return ( quint8(data[off]) << 8 ) + quint8(data[off+1] );
}

static void writeU16( QByteArray& data, int off, quint16 val )
{
    Q_ASSERT( off + 1 < data.size() );
    data[off] = ( val >> 8 ) & 0xff;
    data[off+1] = val & 0xff;
}

static void writeU16( quint8* data, int off, quint16 val )
{
    data[off] = ( val >> 8 ) & 0xff;
    data[off+1] = val & 0xff;
}

static const int methHdrByteLen = 2;
static const int ValueIndex = methHdrByteLen / 2;

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

    qDebug() << "object space" << objectSpaceLenBytes << "bytes, object table" << objectTableLenBytes << "bytes";

    // Object Space format:
    // | xxxxxxxx xxxxxxxx | word size
    // | xxxxxxxx xxxxxxxx | class
    // | payload of lenght word size - 2

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

        const OOP oop = i >> 2;

        Object* obj = d_ot.allocate( oop, byteLen, cls, isPtr(flags) );
        ::memcpy( obj->d_data, objectSpace.constData() + addr + 4, byteLen ); // without header
    }

    d_objects.clear();
    d_classes.clear();
    d_metaClasses.clear();

    for( int i = 0; i < d_ot.d_slots.size(); i++ )
    {
        const OtSlot& slot = d_ot.d_slots[i];
        if( slot.isFree() )
            continue;
        const quint16 oop = i << 1;
        if( d_objects.contains(oop) )
            continue;
        d_objects << oop;

        const OOP cls = slot.getClass();
        d_classes << cls;
        d_classes << fetchPointerOfObject(0,cls); // superclass of cls
        if( cls == classCompiledMethod )
        {
            for( int j = 0; j < methodLiteralCount(oop); j++ )
            {
                const OOP ptr = methodLiteral(j,oop);
                if( !isInt(ptr) && ptr != objectNil && ptr != objectTrue && ptr != objectFalse )
                    d_xref[ptr].append(oop);
            }
        }else if( hasPointerMembers(oop) )
        {

            const int len = fetchWordLenghtOf(oop);
            for( int j = 0; j < len; j++ )
            {
                quint16 ptr = fetchWordOfObject(j,oop);
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
        // d_metaClasses << fetchClassOf(cls);
    }
    d_classes -= d_metaClasses;

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

void ObjectMemory2::setRegister(quint8 index, quint16 value)
{
    if( index > d_registers.size() )
        d_registers.resize( d_registers.size() + 10 );
    d_registers[index] = value;
}

quint16 ObjectMemory2::getRegister(quint8 index) const
{
    if( index < d_registers.size() )
        return d_registers[index];
    else
        return 0;
}

void ObjectMemory2::addTemp(OOP oop)
{
    d_temps.insert(oop);
}

void ObjectMemory2::removeTemp(ObjectMemory2::OOP oop)
{
    d_temps.remove(oop);
}

bool ObjectMemory2::hasPointerMembers(OOP objectPointer) const
{
    if( isInt(objectPointer) )
        return false;
    const OtSlot& s = getSlot(objectPointer);
    return s.d_isPtr;
}

ObjectMemory2::OOP ObjectMemory2::fetchPointerOfObject(quint16 fieldIndex, OOP objectPointer) const
{
    const OtSlot& s = getSlot(objectPointer);
    const quint32 off = fieldIndex * 2;
    Q_ASSERT( s.d_isPtr && ( off + 1 ) < s.byteLen() );
    return readU16( s.d_obj->d_data, off );
}

void ObjectMemory2::storePointerOfObject(quint16 fieldIndex, OOP objectPointer, OOP withValue)
{
    const OtSlot& s = getSlot(objectPointer);
    const quint32 off = fieldIndex * 2;
    Q_ASSERT( ( off + 1 ) < s.byteLen() && !isInt(withValue) );
    writeU16( s.d_obj->d_data, off, withValue );
}

quint16 ObjectMemory2::fetchWordOfObject(quint16 fieldIndex, OOP objectPointer) const
{
    const OtSlot& s = getSlot(objectPointer);
    const quint32 off = fieldIndex * 2;
    Q_ASSERT( ( off + 1 ) < s.byteLen() );
    return readU16( s.d_obj->d_data, off );
}

void ObjectMemory2::storeWordOfObject(quint16 fieldIndex, OOP objectPointer, quint16 withValue)
{
    const OtSlot& s = getSlot(objectPointer);
    const quint32 off = fieldIndex * 2;
    Q_ASSERT( ( off + 1 ) < s.byteLen() );
    writeU16( s.d_obj->d_data, off, withValue );
}

quint8 ObjectMemory2::fetchByteOfObject(quint16 byteIndex, OOP objectPointer) const
{
    const OtSlot& s = getSlot(objectPointer);
    const quint32 off = byteIndex;
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
    // TODO
    return 0;
}

ObjectMemory2::OOP ObjectMemory2::instantiateClassWithWords(OOP classPointer, quint16 instanceSize)
{
    // TODO
    return 0;
}

ObjectMemory2::OOP ObjectMemory2::instantiateClassWithBytes(OOP classPointer, quint16 instanceByteSize)
{
    // TODO
    return 0;
}

ObjectMemory2::ByteString ObjectMemory2::fetchByteString(OOP objectPointer) const
{
    if( isInt(objectPointer) )
        return ByteString(0,0);
    const OtSlot& s = getSlot(objectPointer);
    return ByteString( s.d_obj->d_data, s.byteLen() );
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
    }
    return QByteArray();
}

quint8 ObjectMemory2::methodTemporaryCount(OOP methodPointer) const
{
    const OtSlot& s = getSlot(methodPointer);
    Q_ASSERT( s.getClass() == ObjectMemory2::classCompiledMethod );
    return s.d_obj->d_data[0] & 0x1f;
}

ObjectMemory2::CompiledMethodFlags ObjectMemory2::methodFlags(OOP methodPointer) const
{
    const OtSlot& s = getSlot(methodPointer);
    Q_ASSERT( s.getClass() == ObjectMemory2::classCompiledMethod );
    return CompiledMethodFlags( getMethodFlags( s.d_obj->d_data[0] ) );
}

bool ObjectMemory2::methodLargeContext(OOP methodPointer) const
{
    const OtSlot& s = getSlot(methodPointer);
    Q_ASSERT( s.getClass() == ObjectMemory2::classCompiledMethod );
    return ( s.d_obj->d_data[1] & 0x80 );
}

quint8 ObjectMemory2::methodLiteralCount(OOP methodPointer) const
{
    const OtSlot& s = getSlot(methodPointer);
    Q_ASSERT( s.getClass() == ObjectMemory2::classCompiledMethod );
    return getLiteralByteCount( s.d_obj->d_data ) / 2;
}

ObjectMemory2::ByteString ObjectMemory2::methodBytecodes(OOP methodPointer) const
{
    const OtSlot& s = getSlot(methodPointer);
    Q_ASSERT( s.getClass() == ObjectMemory2::classCompiledMethod );
    const quint8 literalByteCount = getLiteralByteCount( s.d_obj->d_data );
    const quint8* bytes = s.d_obj->d_data + methHdrByteLen + literalByteCount;
    const quint16 byteLen = s.byteLen() - ( methHdrByteLen + literalByteCount );
    return ByteString( bytes, byteLen );
}

quint8 ObjectMemory2::methodArgumentCount(OOP methodPointer) const
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

quint8 ObjectMemory2::methodPrimitiveIndex(OOP methodPointer) const
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

ObjectMemory2::OOP ObjectMemory2::methodLiteral(quint8 index, OOP methodPointer) const
{
    const OtSlot& s = getSlot(methodPointer);
    Q_ASSERT( s.getClass() == ObjectMemory2::classCompiledMethod );
    const quint8 literalByteCount = getLiteralByteCount(s.d_obj->d_data);
    const quint16 byteIndex = 2 * index;
    Q_ASSERT( byteIndex < literalByteCount );
    return readU16( s.d_obj->d_data, methHdrByteLen + byteIndex );
}

quint32 ObjectMemory2::methodInitialInstructionPointer(ObjectMemory2::OOP methodPointer) const
{
    return ( methodLiteralCount(methodPointer) + ValueIndex ) * 2 + 1;
}

ObjectMemory2::OOP ObjectMemory2::methodClassOf(ObjectMemory2::OOP methodPointer) const
{
    const quint16 literalCount = methodLiteralCount(methodPointer);
    OOP association = methodLiteral(literalCount-1, methodPointer);
    return fetchPointerOfObject(ValueIndex,association);
}

bool ObjectMemory2::isPointer(OOP ptr)
{
    return !isInt(ptr);
}

qint16 ObjectMemory2::toInt(OOP objectPointer)
{
    if( isInt(objectPointer) )
    {
        int res = ( objectPointer >> 1 );
        if( objectPointer & 0x4000 )
        {
            res = -( ~res & 0x7fff ) - 1;
            return res;
        }else
            return res;
    }else
        return 0;
}

ObjectMemory2::OOP ObjectMemory2::toPtr(qint16 value)
{
    OOP res = 0;
    if( value >= 0 )
        res = ( value << 1 ) | 1;
    else
    {
        value = ::abs(value);
        res = ~value + 1;
        res = ( res << 1 ) | 1; // TODO: to TEST
    }
    return res;
}

int ObjectMemory2::findFreeSlot()
{
    // not efficient, but good enough for now
    for( int i = classSymbol / 2; i < d_ot.d_slots.size(); i++ )
    {
        if( d_ot.d_slots[i].isFree() )
            return i;
    }
    return -1;
}

ObjectMemory2::OOP ObjectMemory2::instantiateClass(ObjectMemory2::OOP cls, quint16 wordLen, bool isPtr)
{
    int slot = findFreeSlot();
    if( slot < 0 )
        collectGarbage();
    slot = findFreeSlot();
    if( slot < 0 )
    {
        qCritical() << "cannot allocate object, no free object table slots";
        return 0;
    }
    if( d_ot.allocate( slot, wordLen >> 1, cls, isPtr ) )
    {
        qCritical() << "cannot allocate object, no free memory";
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

    // mark
    foreach( quint16 reg, d_registers )
        mark(reg);
    foreach( quint16 reg, d_temps )
        mark(reg);
    for( int oop = 0; oop <= classSymbol; oop += 2 )
    {
        mark( oop );
    }

    // sweep
    for( int i = 0; i < d_ot.d_slots.size(); i++ )
    {
        const OtSlot& s = d_ot.d_slots[i];
        if( s.isFree() )
            continue;
        if( !s.d_obj->d_flags.test(Object::Marked) )
        {
            d_ot.free(i);
        }else
            s.d_obj->d_flags.set(Object::Marked, false);
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
    }else if( s.d_class == classCompiledMethod )
    {
        const quint16 len = methodLiteralCount(oop);
        for( int i = 0; i < len; i++ )
        {
            quint16 sub = methodLiteral(i, oop);
            if( isPointer(sub) )
                mark( sub );
        }
    }

    mark( s.getClass() );
}

const ObjectMemory2::OtSlot&ObjectMemory2::getSlot(ObjectMemory2::OOP oop) const
{
    const quint32 i = oop >> 1;
    Q_ASSERT( i < d_ot.d_slots.size() );
    return d_ot.d_slots[i];
}

ObjectMemory2::Object* ObjectMemory2::ObjectTable::allocate(quint16 slot, quint16 numOfBytes, OOP cls, bool isPtr)
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
    return ots.d_obj;
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
