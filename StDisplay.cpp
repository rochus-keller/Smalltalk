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
#include "StDisplay.h"

#include <QFile>
#include <QPainter>
#include <stdint.h>
using namespace St;

static Display* s_inst = 0;

#if 0
struct BS_Stream
{
    const uint8_t* bytes;
    int pos, size;
    uint32_t accu;
    int bits;
};

void BS_init(struct BS_Stream* bs, const uint8_t* buf, int size)
{
    bs->accu = 0;
    bs->bits = 0;
    bs->bytes = buf;
    bs->size = size;
    bs->pos = 0;
}

uint8_t BS_readBits(struct BS_Stream* stream, uint8_t nextNBits)
{
    Q_ASSERT( nextNBits <= 8 );
    int shiftRight = 0;
    uint8_t res = 0;
    uint8_t* buf = &res;

    uint32_t accu = stream->accu;
    int bitsUnread = stream->bits;
    int mask;

    while( nextNBits > 0 )
    {
        while( bitsUnread && nextNBits )
        {
            mask = 128 >> shiftRight; // start with 1'0000'0000b and move to right by shiftRight
            if( accu & (1 << (bitsUnread - 1)) )
                *buf |= mask;
            else
                *buf &= ~mask;

            nextNBits--;
            bitsUnread--;

            if( ++shiftRight >= 8 )
            {
                shiftRight = 0;
                buf++;
            }
        }
        if( nextNBits == 0 )
            break;
        if( stream->pos >= stream->size )
            break;
        uint8_t nextByte = stream->bytes[stream->pos++];
        accu = (accu << 8) | nextByte;
        bitsUnread += 8;
    }
    stream->accu = accu;
    stream->bits = bitsUnread;

    return res;
}
#endif

Display::Display(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::BlankCursor);
    showMaximized();
    startTimer(30);
}

Display*Display::inst()
{
    if( s_inst == 0 )
        s_inst = new Display();
    return s_inst;
}

void Display::setBitmap(const Bitmap& buf)
{
    d_bitmap = buf;
    update();

#if 0
    QFile out( "bitmap" );
    out.open(QIODevice::WriteOnly);
    out.write((const char*)buf, d_width * d_height / 8 );
#endif
}

void Display::paintEvent(QPaintEvent*)
{
    if( d_bitmap.isNull() )
        return;
    QPainter p(this);
    p.fillRect(0,0,d_bitmap.width(),d_bitmap.height(), Qt::white );
    p.setPen(Qt::black);

#if 0
    BS_Stream stream;
    BS_init( &stream, d_buf, d_width * d_height / 8 );
    for( int y = 0; y < d_height; y++ )
    {
        for( int x = 0; x < d_width; x++ )
        {
            quint8 pix = BS_readBits( &stream, 1 );
            if( pix )
                p.drawPoint(x, y);
        }
    }
#else
    for( int y = 0; y < d_bitmap.height(); y++ )
    {
        for( int x = 0; x < d_bitmap.width(); x++ )
        {
            if( d_bitmap.testBit(x,y) )
                p.drawPoint(x, y);
        }
    }
#endif

}

void Display::timerEvent(QTimerEvent*)
{
    update();
}

Bitmap::Bitmap(quint8* buf, quint16 wordLen, quint16 pixWidth, quint16 pixHeight)
{
    d_buf = buf;
    d_wordLen = wordLen;
    d_width = pixWidth;
    d_height = pixHeight;
    d_lineWidth = ( ( d_width + 7 ) / 8 ) * 8;
}

bool Bitmap::testBit(quint16 x, quint16 y) const
{
    Q_ASSERT( x < d_width && y < d_height );
    const int bytePos = ( y * d_lineWidth + x ) / 8;
    const quint8 bitpos = 1 << ( 7 - x % 8 );
    return ( d_buf[bytePos] & bitpos ) > 0;
}

static quint16 readU16( const quint8* data, int off )
{
    return ( quint8(data[off]) << 8 ) + quint8(data[off+1] );
}

quint16 Bitmap::wordAt(quint16 i) const
{
    Q_ASSERT( i <= d_wordLen );
    i--;
    return readU16( d_buf, i << 1 );
}

static void writeU16( quint8* data, int off, quint16 val )
{
    data[off] = ( val >> 8 ) & 0xff;
    data[off+1] = val & 0xff;
}

void Bitmap::wordAtPut(quint16 i, quint16 v)
{
    Q_ASSERT( i <= d_wordLen );
    i--;
    writeU16( d_buf, i << 1, v );
}

BitBlt::BitBlt(const Input& in)
{
    sourceBits = in.sourceBits;
    destBits = in.destBits;
    halftoneBits = in.halftoneBits;
    combinationRule = in.combinationRule;
    destX = in.destX; clipX = in.clipX; clipWidth = in.clipWidth; sourceX = in.sourceX; width = in.width;
    destY = in.destY; clipY = in.clipY; clipHeight = in.clipHeight; sourceY = in.sourceY; height = in.height;
}

void BitBlt::copyBits()
{
    clipRange();
    if( w <= 0 || h <= 0 )
        return;
    computeMasks();
    checkOverlap();
    calculateOffsets();
    copyLoop();
}

void BitBlt::clipRange()
{
    if( destX >= clipX )
    {
        sx = sourceX;
        dx = destX;
        w = width;
    }else
    {
        sx = sourceX + ( clipX - destX );
        w = width - ( clipX - destX );
        dx = clipX;
    }
    if( ( dx + w ) > ( clipX + clipWidth ) )
        w = w - ( ( dx + w ) - ( clipX + clipWidth ) );
    if( destY >= clipY )
    {
        sy = sourceY;
        dy = destY;
        h = height;
    }else
    {
        sy = sourceY + clipY - destY;
        h = height - clipY + destY;
        dy = clipY;
    }
    if( ( dy + h ) > ( clipY + clipHeight ) )
        h = h - ( ( dy + h ) - ( clipY + clipHeight ) );
    if( sx < 0 )
    {
        dx = dx - sx;
        w = w + sx;
        sx = 0;
    }
    if( sourceBits != 0 && ( sx + w ) > sourceBits->width() )
        w = w - ( sx + w - sourceBits->width() );
    if( sy < 0 )
    {
        dy = dy - sy;
        h = h + sy;
        sy = 0;
    }
    if( sourceBits != 0 && ( sy + h ) > sourceBits->height() )
        h = h - ( sy + h - sourceBits->height() );
}

void BitBlt::computeMasks()
{
    // destBits = destForm bits
    destRaster = ( ( destBits->width() - 1 ) / 16 ) + 1;
    if( sourceBits != 0 )
        sourceRaster = ( ( sourceBits->width() - 1 ) / 16 ) + 1;
    else
        sourceRaster = 0;
    // halftoneBits = halftoneForm bits
    skew = ( sx - dx ) & 15;
    quint16 startBits = 16 - ( dx & 15 );
    mask1 = RightMasks[ startBits /* + 1 */ ];
    quint16 endBits = 15 - ( ( dx + w - 1 ) & 15 );
    mask2 = ~RightMasks[ endBits /* + 1 */ ];
    skewMask = skew == 0 ? 0 : RightMasks[ 16 - skew /* + 1 */ ];
    if( w < startBits )
    {
        mask1 = mask1 && mask2;
        mask2 = 0;
        nWords = 1;
    }else
        nWords = ( w - startBits - 1 ) / 16 + 2;
}

void BitBlt::checkOverlap()
{
    hDir = vDir = 1;
    if( sourceBits == destBits && dy >= sy )
    {
        if( dy > sy )
        {
            vDir = -1;
            sy = sy + h - 1;
            dy = dy + h - 1;
        }else if( dx > sx )
        {
            hDir = -1;
            sx = sx + w - 1;
            dx = dx + w - 1;
            skewMask = ~skewMask;
            int t = mask1;
            mask1 = mask2;
            mask2 = t;
        }
    }
}

void BitBlt::calculateOffsets()
{
    preload = ( sourceBits != 0 && skew != 0 && skew <= ( sx & 15 ) );
    if( hDir < 0 )
        preload = preload == false;
    sourceIndex = sy * sourceRaster + sx / 16;
    destIndex = dy * destRaster + dx / 16;
    sourceDelta = sourceRaster * vDir - ( nWords + ( preload ? 1 : 0 ) * hDir );
    destDelta = destRaster * vDir - nWords * hDir;
}

static inline qint16 shiftLeft( qint16 v, qint16 n )
{
    Q_ASSERT( v >= 0 );
    if( n >= 0 )
        return v << n;
    else
        return v >> -n;
}

void BitBlt::copyLoop()
{
    for( int i = 1; i <= h; i++ )
    {
        quint16 halftoneWord;
        if( halftoneBits != 0 )
        {
            halftoneWord = halftoneBits->wordAt( 1 + ( dy & 15 ) );
            dy = dy + vDir;
        }else
            halftoneWord = AllOnes;
        quint16 skewWord = halftoneWord;
        quint16 prevWord, thisWord;
        if( preload && sourceBits != 0 )
        {
            prevWord = sourceBits->wordAt( sourceIndex + 1 );
            sourceIndex = sourceIndex + hDir;
        }else
            prevWord = 0;
        quint16 mergeMask = mask1;
        for( quint16 word = 1; word <= nWords; word++ )
        {
            if( sourceBits != 0 )
            {
                prevWord = prevWord & skewWord;
                thisWord = sourceBits->wordAt( sourceIndex + 1 );
                skewWord = prevWord | ( thisWord & ~skewMask );
                prevWord = thisWord;
                skewWord = shiftLeft( skewWord, skew ) | shiftLeft( skewWord, skew - 16 );
            }
            quint16 mergeWord = merge( skewWord & halftoneWord, destBits->wordAt( destIndex + 1 ) );
            destBits->wordAtPut( destIndex + 1, ( mergeMask & mergeWord ) |
                                 ( ~mergeMask & destBits->wordAt( destIndex + 1 ) ) );
            sourceIndex = sourceIndex + hDir;
            destIndex = destIndex + hDir;
            if( word == ( nWords - 1 ) )
                mergeMask = mask2;
            else
                mergeMask = AllOnes;
        }
        sourceIndex = sourceIndex + sourceDelta;
        destIndex = destIndex + destDelta;
    }
}

quint16 BitBlt::merge(quint16 source, quint16 destination)
{
    switch( combinationRule )
    {
    case 0:
        return 0;
    case 1:
        return source & destination;
    case 2:
        return source & ~destination;
    case 3:
        return source;
    case 4:
        return ~source & destination;
    case 5:
        return destination;
    case 6:
        return source ^ destination;
    case 7:
        return source | destination;
    case 8:
        return ~source & ~destination;
    case 9:
        return ~source ^ destination;
    case 10:
        return ~destination;
    case 11:
        return source | ~destination;
    case 12:
        return ~source;
    case 13:
        return ~source | destination;
    case 14:
        return ~source | ~destination;
    case 15:
        return AllOnes;
    }
    return 0;
}

const QList<quint16> BitBlt::RightMasks = QList<quint16>() <<
    0 << 0x1 << 0x3 << 0x7 << 0xf <<
       0x1f << 0x3f << 0x7f << 0xff <<
       0x1ff << 0x3ff << 0x7ff << 0xfff <<
       0x1fff << 0x3fff << 0x7fff << 0xffff;

const quint16 BitBlt::AllOnes = 0xffff;
