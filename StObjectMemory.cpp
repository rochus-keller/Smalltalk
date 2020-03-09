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

#include "StObjectMemory.h"
#include <QIODevice>
#include <QtDebug>
using namespace St;

// According to "Smalltalk-80: Virtual Image Version 2", Xerox PARC, 1983
// see http://www.wolczko.com/st80/manual.pdf.gz
// and the Blue Book

ObjectMemory::ObjectMemory()
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

static void writeU16( QByteArray& data, int off, quint16 val )
{
    Q_ASSERT( off + 1 < data.size() );
    data[off] = ( val >> 8 ) & 0xff;
    data[off+1] = val & 0xff;
}

static const int objHdrByteLen = 4;
static const int methHdrByteLen = objHdrByteLen + 2;

// | xxxxxxxx xxxxxxxx | word size
// | xxxxxxxx xxxxxxxx | class
// | fffttttt fllllll1 | method header

static inline quint8 getLiteralByteCount( const QByteArray& space, quint32 startOfObject )
{
    return 2 * ( ( quint8( space[ startOfObject + methHdrByteLen - 1] ) >> 1 ) & 0x3f );
}

static inline quint8 getMethodFlags( const QByteArray& space, quint32 startOfObject )
{
    return ( quint8( space[ startOfObject + methHdrByteLen - 2 ]) >> 5 ) & 0x7;
}

static inline bool isCompiledMethod(const QByteArray& space, quint32 startOfObject)
{
    return readU16(space,startOfObject+2) == ObjectMemory::classCompiledMethod;
}

bool ObjectMemory::readFrom(QIODevice* in)
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

    d_objectSpace = in->read( objectSpaceLenBytes ); // first word is oop_2
    if( d_objectSpace.size() != objectSpaceLenBytes )
        return false;

    const int numOfPages = objectSpaceLenBytes / 512;
    const int off = 512 + ( numOfPages + 1 ) * 512;
    in->seek( off );
    d_objectTable = in->read( objectTableLenBytes ); // first word is oop_0
    if( d_objectTable.size() != objectTableLenBytes )
        return false;

    // printObjectTable(); // TEST
    return true;
}

quint16 ObjectMemory::fetchPointerOfObject(quint16 fieldIndex, quint16 objectPointer) const
{
    Data data = getDataOf( objectPointer );
    const quint32 off = fieldIndex * 2;
    Q_ASSERT( data.d_isPtr && ( off + 1 ) < data.d_len );
    return readU16( d_objectSpace, data.d_pos + off );
}

void ObjectMemory::storePointerOfObject(quint16 fieldIndex, quint16 objectPointer, quint16 withValue)
{
    Data data = getDataOf( objectPointer );
    const quint32 off = fieldIndex * 2;
    Q_ASSERT( data.d_isPtr && ( off + 1 ) < data.d_len && !isInt(withValue) );
    writeU16( d_objectSpace, data.d_pos + off, withValue );
}

quint16 ObjectMemory::fetchWordOfObject(quint16 fieldIndex, quint16 objectPointer) const
{
    Data data = getDataOf( objectPointer );
    const quint32 off = fieldIndex * 2;
    Q_ASSERT( !data.d_isPtr && ( off + 1 ) < data.d_len );
    return readU16( d_objectSpace, data.d_pos + off );
}

void ObjectMemory::storeWordOfObject(quint16 fieldIndex, quint16 objectPointer, quint16 withValue)
{
    Data data = getDataOf( objectPointer );
    const quint32 off = fieldIndex * 2;
    Q_ASSERT( !data.d_isPtr && ( off + 1 ) < data.d_len );
    writeU16( d_objectSpace, data.d_pos + off, withValue );
}

quint8 ObjectMemory::fetchByteOfObject(quint16 byteIndex, quint16 objectPointer) const
{
    Data data = getDataOf( objectPointer );
    const quint32 off = byteIndex;
    Q_ASSERT( !data.d_isPtr && off < data.d_len );
    return (quint8)d_objectSpace[data.d_pos + off];
}

void ObjectMemory::storeByteOfObject(quint16 byteIndex, quint16 objectPointer, quint8 withValue)
{
    Data data = getDataOf( objectPointer );
    const quint32 off = byteIndex;
    Q_ASSERT( !data.d_isPtr && off < data.d_len );
    d_objectSpace[data.d_pos + off] = withValue;
}

quint16 ObjectMemory::fetchClassOf(quint16 objectPointer) const
{
    return getClassOf(objectPointer);
}

quint16 ObjectMemory::fetchByteLenghtOf(quint16 objectPointer) const
{
    Data data = getDataOf( objectPointer );
    return data.d_len;
}

quint16 ObjectMemory::instantiateClassWithPointers(quint16 classPointer, quint16 instanceSize)
{
    return createInstance(classPointer, instanceSize * 2, true );
}

quint16 ObjectMemory::instantiateClassWithWords(quint16 classPointer, quint16 instanceSize)
{
    return createInstance(classPointer, instanceSize * 2, false );
}

quint16 ObjectMemory::instantiateClassWithBytes(quint16 classPointer, quint16 instanceByteSize)
{
    return createInstance(classPointer, instanceByteSize, false );
}

quint8 ObjectMemory::methodTemporaryCount(quint16 methodPointer) const
{
    Data d = getDataOf( methodPointer, false );
    Q_ASSERT( isCompiledMethod(d_objectSpace,d.d_pos) );
    return d_objectSpace[ d.d_pos + methHdrByteLen - 2] & 0x1f;
}

ObjectMemory::CompiledMethodFlags ObjectMemory::methodFlags(quint16 methodPointer) const
{
    Data d = getDataOf( methodPointer, false );
    Q_ASSERT( isCompiledMethod(d_objectSpace,d.d_pos) );
    return CompiledMethodFlags( getMethodFlags( d_objectSpace, d.d_pos ) );
}

bool ObjectMemory::methodLargeContext(quint16 methodPointer) const
{
    Data d = getDataOf( methodPointer, false );
    Q_ASSERT( isCompiledMethod(d_objectSpace,d.d_pos) );
    return quint8( d_objectSpace[ d.d_pos + methHdrByteLen - 1 ] ) & 0x80;
}

quint8 ObjectMemory::methodLiteralCount(quint16 methodPointer) const
{
    Data d = getDataOf( methodPointer, false );
    Q_ASSERT( isCompiledMethod(d_objectSpace,d.d_pos) );
    return getLiteralByteCount(d_objectSpace, d.d_pos ) / 2;
}

ObjectMemory::ByteString ObjectMemory::methodBytecodes(quint16 methodPointer) const
{
    Data d = getDataOf( methodPointer, false );
    Q_ASSERT( isCompiledMethod(d_objectSpace,d.d_pos) );
    const quint8 literalByteCount = getLiteralByteCount(d_objectSpace, d.d_pos );
    const quint8* bytes = (quint8*) d_objectSpace.constData() + d.d_pos + methHdrByteLen + literalByteCount;
    const quint16 byteLen = d.d_len - ( methHdrByteLen + literalByteCount );
    return ByteString( bytes, byteLen );
}

quint8 ObjectMemory::methodArgumentCount(quint16 methodPointer) const
{
    Data d = getDataOf( methodPointer, false );
    Q_ASSERT( isCompiledMethod(d_objectSpace,d.d_pos) );
    const quint8 flags = getMethodFlags(d_objectSpace, d.d_pos );
    if( flags <= FourArguments )
        return flags;
    else if( flags == ZeroArgPrimitiveReturnSelf || flags == ZeroArgPrimitiveReturnVar )
        return 0;
    Q_ASSERT( flags == HeaderExtension );
    const quint8 literalByteCount = getLiteralByteCount(d_objectSpace, d.d_pos );
    const quint16 extension = readU16( d_objectSpace, d.d_pos + methHdrByteLen
                                       + literalByteCount - 4 ); // next to the last literal
    return ( extension >> 9 ) & 0x1f;
}

quint8 ObjectMemory::methodPrimitiveIndex(quint16 methodPointer) const
{
    Data d = getDataOf( methodPointer, false );
    Q_ASSERT( isCompiledMethod(d_objectSpace,d.d_pos) );
    const quint8 flags = getMethodFlags( d_objectSpace, d.d_pos );
    if( flags != HeaderExtension )
        return 0;
    Q_ASSERT( flags == HeaderExtension );
    const quint8 literalByteCount = getLiteralByteCount(d_objectSpace, d.d_pos );
    const quint16 extension = readU16( d_objectSpace, d.d_pos + methHdrByteLen
                                       + literalByteCount - 4 ); // next to the last literal
    return ( extension >> 1 ) & 0xff;
}

quint16 ObjectMemory::methodLiteral(quint16 methodPointer, quint8 index) const
{
    Data d = getDataOf( methodPointer, false );
    Q_ASSERT( isCompiledMethod(d_objectSpace,d.d_pos) );
    const quint8 literalByteCount = getLiteralByteCount(d_objectSpace, d.d_pos );
    const quint16 byteIndex = 2 * index;
    Q_ASSERT( byteIndex < literalByteCount );
    return readU16( d_objectSpace, d.d_pos + methHdrByteLen + byteIndex );
}

static inline QByteArray printData( const QByteArray& data )
{
    if( data.isEmpty() )
        return QByteArray();
    QByteArray printable;
    int i = 0;
    while( i < data.size() && ::isprint(data[i]) )
        printable += data[i++];
    if( !printable.isEmpty() )
        return "\"" + printable.simplified() + "\"";
    else
        return data.left(32).toHex() + ( data.size() > 32 ? "..." : "" );
}

void ObjectMemory::printObjectTable()
{
    for( int i = 0; i < d_objectTable.size(); i += 4 )
    {
        // i is byte number
        const quint8 flags = quint8(d_objectTable[i+1]);
        const quint16 loc = readU16(d_objectTable,i+2);
        const quint8 seg = flags & 0xf;
        const quint32 spaceAddr = ( seg << 16 ) + ( loc << 1 );
        const quint16 size = readU16( d_objectSpace, spaceAddr );
        const quint16 cls = readU16(d_objectSpace,spaceAddr+2);
        const QByteArray objData = ( !isPtr(flags) ? d_objectSpace.mid(spaceAddr+4,size*2) : QByteArray() );
        qDebug() << "oop:" << QByteArray::number(i/2,16) // oop is the word number, not the byte number
                 << "count:" << QByteArray::number(quint8(d_objectTable[i])).constData()
                 //<< "flags:"
                 << ( isOdd(flags) ? "Odd" : "" ) << ( isPtr(flags) ? "Ptr" : "" ) << (isFree(flags) ? "Free" : "" )
                    << (flags & 0x10 ? "UnknFlag" : "" )
                 //<< "seg:" << ( seg )
                 //<< "loc:" << QByteArray::number(loc,16)
                 << "size:" << size // words
                 << "class:" << QByteArray::number(cls,16) // entry into objectTable
                 << "data:" << printData( objData ).constData();
    }

    // oop with lsb bit set are smallintegers, not pointers
}

quint32 ObjectMemory::getSpaceAddr(quint16 oop, bool* odd, bool* ptr ) const
{
    oop *= 2;
    Q_ASSERT( ( oop + 4 ) < d_objectTable.size() );
    const quint8 flags = quint8(d_objectTable[oop+1]);
    const quint16 loc = readU16(d_objectTable,oop+2);
    const quint8 seg = flags & 0xf;
    const quint32 spaceAddr = ( seg << 16 ) + ( loc << 1 );
    if( odd )
        *odd = isOdd(flags);
    if( ptr )
        *ptr = isPtr(flags);
    return spaceAddr;
}

quint16 ObjectMemory::getClassOf(quint16 oop) const
{
    const quint32 spaceAddr = getSpaceAddr(oop);
    return readU16(d_objectSpace, spaceAddr + 2 );
}

ObjectMemory::Data ObjectMemory::getDataOf(quint16 oop, bool noHeader ) const
{
    bool odd, ptr;
    Data res;
    res.d_pos = getSpaceAddr(oop, &odd, &ptr );
    res.d_isPtr = ptr;
    res.d_len = readU16( d_objectSpace, res.d_pos ) * 2;
    if( odd )
        res.d_len--;
    Q_ASSERT( res.d_pos + res.d_len < d_objectSpace.size() );
    if( noHeader )
    {
        res.d_pos += 4;
        res.d_len -= 4;
    }
    return res;
}

qint32 ObjectMemory::findNextFree()
{
    for( int i = 0; i < d_objectTable.size(); i += 4 )
    {
        if( isFree(d_objectTable[i]) )
            return i;
    }
    return -1;
}

quint16 ObjectMemory::createInstance(quint16 classPtr, quint16 byteLen, bool isPtr)
{
    bool odd = false;
    if( byteLen & 1 )
    {
        odd = true;
        byteLen++;
    }

    int oop = findNextFree();
    if( oop < 0 )
    {
        oop = d_objectTable.size();
        d_objectTable.resize( d_objectTable.size() + 4 );
    }

    quint32 spaceAddr = d_objectSpace.size();
    d_objectSpace.resize( d_objectSpace.size() + 4 + byteLen );

    // init pointer objects with nil values and others with 0
    for( int i = spaceAddr + 4; i < d_objectSpace.size(); i += 2 )
         writeU16(d_objectSpace, i, isPtr ? objectNil : 0 );

    writeU16(d_objectSpace, spaceAddr, byteLen / 2 );
    writeU16(d_objectSpace, spaceAddr + 2, classPtr );

    const quint8 seg = ( spaceAddr >> 16 ) & 0xf;
    const quint16 loc = ( spaceAddr & 0xffff ) >> 1;

    d_objectTable[oop] = 0;
    d_objectTable[oop+1] = ( isPtr ? 0x40 : 0 ) | ( odd ? 0x80 : 0 ) | seg;
    writeU16(d_objectTable, 2, loc );

    oop = oop / 2;
    return oop;
}

