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
#include <QtMath>
using namespace St;

// According to "Smalltalk-80: Virtual Image Version 2", Xerox PARC, 1983
// see http://www.wolczko.com/st80/manual.pdf.gz
// and the Blue Book

ObjectMemory::ObjectMemory(QObject* p):QObject(p)
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

    qDebug() << "object space" << objectSpaceLenBytes << "bytes, object table" << objectTableLenBytes << "bytes";

    d_objectSpace = in->read( objectSpaceLenBytes );
    if( d_objectSpace.size() != objectSpaceLenBytes )
        return false;

    const int numOfPages = objectSpaceLenBytes / 512;
    const int off = 512 + ( numOfPages + 1 ) * 512;
    in->seek( off );
    d_objectTable = in->read( objectTableLenBytes );
    if( d_objectTable.size() != objectTableLenBytes )
        return false;

    d_objects.clear();
    d_classes.clear();
    d_metaClasses.clear();

    for( int i = 0; i < d_objectTable.size(); i += 4 )
    {
        // i is byte number
        const quint16 oop = i >> 1;
        Q_ASSERT( !d_objects.contains(oop) );
        d_objects << oop;

        const quint16 cls = fetchClassOf(oop);
        d_classes << cls;
        d_classes << fetchPointerOfObject(0,cls); // superclass of cls
        if( cls == classCompiledMethod )
        {
            for( int j = 0; j < literalCountOf(oop); j++ )
            {
                quint16 ptr = literalOfMethod(j,oop);
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

    //printObjectTable(); // TEST
    //printObjectSpace();

    return true;
}

QList<quint16> ObjectMemory::getAllValidOop() const
{
    QList<quint16> res;
    for( int i = 0; i < d_objectTable.size(); i += 4 )
    {
        // i is byte number
        const quint8 flags = quint8(d_objectTable[i+1]);
        const quint16 oop = ( i >> 1 );
        if( !isFree(flags) )
            res << oop;
    }
    return res;
}

bool ObjectMemory::hasPointerMembers(quint16 objectPointer) const
{
    if( isInt(objectPointer) )
        return false;
    bool ptr;
    getSpaceAddr( objectPointer, 0, &ptr );
    return ptr;
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
    Q_ASSERT( ( off + 1 ) < data.d_len && !isInt(withValue) );
    writeU16( d_objectSpace, data.d_pos + off, withValue );
}

quint16 ObjectMemory::fetchWordOfObject(quint16 fieldIndex, quint16 objectPointer) const
{
    Data data = getDataOf( objectPointer );
    const quint32 off = fieldIndex * 2;
    Q_ASSERT( ( off + 1 ) < data.d_len );
    return readU16( d_objectSpace, data.d_pos + off );
}

void ObjectMemory::storeWordOfObject(quint16 fieldIndex, quint16 objectPointer, quint16 withValue)
{
    Data data = getDataOf( objectPointer );
    const quint32 off = fieldIndex * 2;
    Q_ASSERT( ( off + 1 ) < data.d_len );
    writeU16( d_objectSpace, data.d_pos + off, withValue );
}

quint8 ObjectMemory::fetchByteOfObject(quint16 byteIndex, quint16 objectPointer) const
{
    Data data = getDataOf( objectPointer );
    const quint32 off = byteIndex;
    Q_ASSERT( !data.d_isPtr && off < data.getLen() );
    return (quint8)d_objectSpace[data.d_pos + off];
}

void ObjectMemory::storeByteOfObject(quint16 byteIndex, quint16 objectPointer, quint8 withValue)
{
    Data data = getDataOf( objectPointer );
    const quint32 off = byteIndex;
    Q_ASSERT( !data.d_isPtr && off < data.getLen() );
    d_objectSpace[data.d_pos + off] = withValue;
}

quint16 ObjectMemory::fetchClassOf(quint16 objectPointer) const
{
    if( !isPointer(objectPointer) )
        return classSmallInteger;
    else
        return getClassOf(objectPointer);
}

quint16 ObjectMemory::fetchByteLenghtOf(quint16 objectPointer) const
{
    if( isInt(objectPointer) )
        return 0;
    Data data = getDataOf( objectPointer );
    return data.getLen();
}

quint16 ObjectMemory::fetchWordLenghtOf(quint16 objectPointer) const
{
    quint16 len = fetchByteLenghtOf(objectPointer);
    if( len & 0x01 )
        len++;
    return len / 2;
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

ObjectMemory::ByteString ObjectMemory::fetchByteString(quint16 objectPointer) const
{
    if( isInt(objectPointer) )
        return ByteString(0,0);
    Data d = getDataOf( objectPointer );
    return ByteString( (quint8*) d_objectSpace.constData() + d.d_pos, d.getLen() );
}

float ObjectMemory::fetchFloat(quint16 objectPointer) const
{
    union {
        float f;
        quint32 w;
    };
#ifdef _DEBUG
    // The BB corresponds to the IEEE 754 32 bit floating point version
    w = 0x4048f5c2; // 3.14 rounded
    Q_ASSERT( qFabs( f - 3.1399998 ) < 0.0000001 );
#endif
    w = fetchWordOfObject(0,objectPointer) << 16 | fetchWordOfObject(1,objectPointer);
    return f;
}

QByteArray ObjectMemory::fetchClassName(quint16 classPointer) const
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

quint8 ObjectMemory::temporaryCountOf(quint16 methodPointer) const
{
    Data d = getDataOf( methodPointer, false );
    Q_ASSERT( isCompiledMethod(d_objectSpace,d.d_pos) );
    return d_objectSpace[ d.d_pos + methHdrByteLen - 2] & 0x1f;
}

ObjectMemory::CompiledMethodFlags ObjectMemory::flagValueOf(quint16 methodPointer) const
{
    Data d = getDataOf( methodPointer, false );
    Q_ASSERT( isCompiledMethod(d_objectSpace,d.d_pos) );
    return CompiledMethodFlags( getMethodFlags( d_objectSpace, d.d_pos ) );
}

bool ObjectMemory::largeContextFlagOf(quint16 methodPointer) const
{
    Data d = getDataOf( methodPointer, false );
    Q_ASSERT( isCompiledMethod(d_objectSpace,d.d_pos) );
    return quint8( d_objectSpace[ d.d_pos + methHdrByteLen - 1 ] ) & 0x80;
}

quint8 ObjectMemory::literalCountOf(quint16 methodPointer) const
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
    const quint16 byteLen = d.getLen() - ( methHdrByteLen + literalByteCount );
    return ByteString( bytes, byteLen );
}

quint8 ObjectMemory::argumentCountOf(quint16 methodPointer) const
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

quint8 ObjectMemory::primitiveIndexOf(quint16 methodPointer) const
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

quint16 ObjectMemory::literalOfMethod(quint8 index, quint16 methodPointer) const
{
    Data d = getDataOf( methodPointer, false );
    Q_ASSERT( isCompiledMethod(d_objectSpace,d.d_pos) );
    const quint8 literalByteCount = getLiteralByteCount(d_objectSpace, d.d_pos );
    const quint16 byteIndex = 2 * index;
    Q_ASSERT( byteIndex < literalByteCount );
    return readU16( d_objectSpace, d.d_pos + methHdrByteLen + byteIndex );
}

bool ObjectMemory::isPointer(quint16 ptr)
{
    return !isInt(ptr);
}

qint16 ObjectMemory::integerValueOf(quint16 objectPointer)
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

static inline QByteArray printData( quint16 cls, const QByteArray& data, bool isPtr )
{
    if( cls == ObjectMemory::classString )
    {
        QByteArray str = data.constData();
        str = str.simplified();
        if( str.size() > 32 )
            return "String: \"" + str + "\"... " + QByteArray::number(str.size());
        else
            return "String: \"" + str + "\"";
    }else if( cls == ObjectMemory::classSymbol )
    {
        QByteArray str = data.constData();
        return "Symbol: \"" + str + "\"";
    }else if( cls == 0x28 )
    {
        // Q_ASSERT( isPtr );
        quint16 ch = readU16(data,0);
        // Q_ASSERT( !ObjectMemory::isPointer(ch) );
        ch = ch >> 1;
        return "Char: \"" + QByteArray(1,ch) + "\"";
    }
    if( data.isEmpty() )
        return QByteArray();
    /*
    QByteArray printable;
    int i = 0;
    while( i < data.size() && ::isprint(data[i]) )
        printable += data[i++];
    if( !printable.isEmpty() )
        return "printable: \"" + printable.simplified() + "\"";
    else*/
        return "data: " + data.left(16).toHex() + ( data.size() > 32 ? "..." : "" );
}

void ObjectMemory::printObjectTable()
{
    qDebug() << "************** OBJECT TABLE *******************";
    // QHash<quint16,int> nameclasses;
    for( int i = 0; i < d_objectTable.size(); i += 4 )
    {
        // i is byte number
        const quint8 flags = quint8(d_objectTable[i+1]);
        const quint32 loc = readU16(d_objectTable,i+2); // loc is apparently the number of words (32k max)
        const quint32 seg = flags & 0xf; // in Bits of History page 49: each segment contains 64k words!! (not bytes!!)
        const quint32 spaceAddr = ( seg << 17 ) + ( loc << 1 );
        const quint16 bytelen = readU16( d_objectSpace, spaceAddr ) * 2;
        const quint16 cls = readU16(d_objectSpace,spaceAddr+2);
        /*
        QByteArray clsName;
        if( fetchWordLenghtOf(cls) > 6 )
        {
            const quint16 name = fetchWordOfObject(6,cls);
            const quint16 namecls = fetchClassOf(name);
            if( namecls == classString )
                clsName = (const char*)fetchByteString(name).d_bytes;
            nameclasses[namecls]++;
        }else
            nameclasses[0]++;
            */

        const QByteArray objData = d_objectSpace.mid(spaceAddr+4,bytelen*2);
        qDebug() << "oop:" << QByteArray::number(i/2,16) // oop is the word number, not the byte number
                 << "count:" << QByteArray::number(quint8(d_objectTable[i])).constData()
                 //<< "flags:"
                 << ( isOdd(flags) ? "Odd" : "" ) << ( isPtr(flags) ? "Ptr" : "" ) << (isFree(flags) ? "Free" : "" )
                    << (flags & 0x10 ? "UnknFlag" : "" )
                 //<< "seg:" << ( seg )
                 //<< "loc:" << QByteArray::number(loc << 1,16)
                 << "addr:" << spaceAddr
                 << "bytelen:" << bytelen
                 << "class:" << QByteArray::number(cls,16) // entry into objectTable
                 << printData( cls, objData, isPtr(flags) ).constData();
    }
    // qDebug() << nameclasses;

    // oop with lsb bit set are smallintegers, not pointers
}

void ObjectMemory::printObjectSpace()
{
    qDebug() << "************** OBJECT SPACE *******************";

    int i = 0;
    while( i < d_objectSpace.size() )
    {
        quint16 len = readU16(d_objectSpace, i) * 2;
        quint16 cls = readU16(d_objectSpace, i+2);
        qDebug() << "addr:" << i << "seg:" << QByteArray::number( i >> 16, 16 ) <<
                    "loc:" << QByteArray::number( i & 0xffff, 16 ) << "bytelen:" << len <<
                    "class:" << QByteArray::number( cls, 16 ) <<
                    printData( cls, QByteArray::fromRawData( d_objectSpace.constData() + i + 4, len - 4 ),
                               false ).constData();
        i += len;
    }
}

quint32 ObjectMemory::getSpaceAddr(quint16 oop, bool* odd, bool* ptr ) const
{
    Q_ASSERT( !isInt(oop) );
    const quint32 i = oop * 2;
    if( ( i + 3 ) >= d_objectTable.size() )
    {
        qWarning() << "oop" << QByteArray::number(oop,16) << "out of object table, max oop"
                   << QByteArray::number(d_objectTable.size()/2,16);
        return 0;
    }
    const quint8 flags = quint8(d_objectTable[i+1]);
    const quint16 loc = readU16(d_objectTable,i+2);
    const quint8 seg = flags & 0xf;
    const quint32 spaceAddr = ( seg << 17 ) + ( loc << 1 );
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
    res.d_isOdd = odd;
    res.d_len = readU16( d_objectSpace, res.d_pos ) * 2;
    //if( odd ) // no because the data is there anyway
    //    res.d_len--;
    Q_ASSERT( res.d_pos + res.d_len <= d_objectSpace.size() );
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

