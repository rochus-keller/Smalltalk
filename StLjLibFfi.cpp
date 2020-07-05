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

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <QFile>
#include "StDisplay.h"
#include "StLjObjectMemory.h"
#include <LjTools/Engine2.h>
#include <QCoreApplication>
#include <QMessageBox>
#include <QtDebug>

#ifdef _WIN32
#define DllExport __declspec(dllexport)
#else
#define DllExport
#endif

extern "C"
{
DllExport int St_DIV( int a, int b )
{
    // source: http://lists.inf.ethz.ch/pipermail/oberon/2019/013353.html
    assert( b != 0 );
    if( a < 0 )
        return (a - b + 1) / b;
    else
        return a / b;
}

DllExport int St_MOD( int a, int b )
{
    // source: http://lists.inf.ethz.ch/pipermail/oberon/2019/013353.html
    assert( b != 0 );
    if (a < 0)
        return (b - 1) + (a - b + 1) % b;
    else
        return a % b;
}

typedef struct{
    int count; // byte count
    uint8_t data[];
} ByteArray;

typedef struct{
    int count; // word count
    uint16_t data[];
} WordArray;

DllExport void St_initByteArray( ByteArray* ba, int byteLen, void* data )
{
    assert( ba != 0 );
    ba->count = byteLen;
    if( data )
        memcpy( ba->data, data, byteLen );
}

DllExport void St_initWordArray( WordArray* wa, int byteLen, void* data )
{
    assert( wa != 0 );
    wa->count = byteLen >> 1;
    if( data )
    {
        uint8_t* bytes = (uint8_t*)data;
        int i = 0;
        while( i < byteLen )
        {
            wa->data[i>>1] = (bytes[i] << 8) + bytes[i+1];
            i += 2;
        }
    }
}

DllExport int St_isRunning()
{
    return St::Display::s_run;
}

static quint32 s_startTime = 0;

DllExport void St_stop()
{
    St::Display::s_run = false;
    const quint32 stopTime = St::Display::inst()->getTicks();
    qWarning() << "runtime [ms]:" << ( stopTime - s_startTime );
}

DllExport void St_start()
{
    St::Display::s_run = true;
    s_startTime = St::Display::inst()->getTicks();
}

DllExport void St_processEvents()
{
    QCoreApplication::processEvents();
}

DllExport int St_extractBits(int from, int to, int word)
{
    assert( from <= to && to <= 15 );
    return ( word >> ( 15 - to ) ) & ( ( 1 << ( to - from + 1 ) ) - 1 );
}

DllExport int St_extractBitsSi(int from, int to, int word)
{
    assert( from <= to && to <= 15 );
    const int16_t sw = word;
    const uint16_t uw = sw << 1;
    return ( uw >> ( 15 - to ) ) & ( ( 1 << ( to - from + 1 ) ) - 1 );
}

DllExport int St_isIntegerValue( double val )
{
    if( val != floor(val) )
        return 0;
    // return val > -2147483647.0 && val <= 2147483647.0;
    return val >= -16384.0 && val <= 16383.0;
}

DllExport int St_round( double val )
{
    return val + 0.5;
}

DllExport uint32_t St_toUInt( ByteArray* ba )
{
    switch( ba->count )
    {
    case 1:
        return ba->data[0];
    case 2:
        return ( ba->data[1] << 8 ) + ba->data[0];
    case 3:
        return ( ba->data[2] << 16 ) + ( ba->data[1] << 8 ) + ba->data[0];
    case 4:
        return ( ba->data[3] << 24 ) + ( ba->data[2] << 16 ) + ( ba->data[1] << 8 ) + ba->data[0];
    default:
        printf( "WARNING: large integer with %d bytes not supported", ba->count );
        return 0;
    }
}

DllExport void St_setCursorPos( int x, int y )
{
    St::Display::inst()->setCursorPos(x,y);
}

DllExport int St_nextEvent()
{
    return St::Display::inst()->nextEvent();
}

DllExport int St_loadImage(const char* path )
{
    St::LjObjectMemory om( Lua::Engine2::getInst() );
    QFile in(path);
    if( !in.open(QIODevice::ReadOnly) )
    {
        QMessageBox::critical(St::Display::inst(),"Loading Smalltalk-80 Image", QString("Cannot open file %1").arg(path) );
        return false;
    }
    const bool res = om.readFrom(&in);
    if( !res )
    {
        QMessageBox::critical(St::Display::inst(),"Loading Smalltalk-80 Image", "Incompatible format." );
        return false;
    }
    return true;
}

DllExport void St_log( const char* msg )
{
    QFile out("st_log.txt");
    if( !out.open(QIODevice::Append) )
        qCritical() << "ERR: cannot open log for writing";
    else
    {
        out.write(msg);
        out.write("\n");
    }
}

DllExport const char* St_toString( ByteArray* ba )
{
    const char* str = (char*)ba->data;
    return str;
}

DllExport void St_beDisplay( WordArray* wa, int width, int height )
{
    St::Bitmap bmp(wa->data, wa->count, width, height );
    St::Display::inst()->setBitmap( bmp );
}


DllExport void St_beCursor( WordArray* wa, int width, int height )
{
    St::Bitmap bmp(wa->data, wa->count, width, height );
    St::Display::inst()->setCursorBitmap( bmp );
}

DllExport void St_bitBlt( WordArray* destBits, int destW, int destH,
                          WordArray* sourceBits, int srcW, int srcH,
                          WordArray* htBits, int htW, int htH,
                          int combinationRule,
                          int destX, int destY, int width, int height,
                          int sourceX, int sourceY,
                          int clipX, int clipY, int clipWidth, int clipHeight )
{
    St::Bitmap destBm( destBits ? destBits->data : 0, destBits ? destBits->count : 0, destW, destH);
    St::Bitmap sourceBm( sourceBits ? sourceBits->data : 0, sourceBits ? sourceBits->count : 0, srcW, srcH);
    St::Bitmap htBm( htBits ? htBits->data : 0, htBits ? htBits->count : 0, htW, htH);

    St::Display* disp = St::Display::inst();
    const bool drawToDisp = disp->getBitmap().isSameBuffer( destBm );

    St::BitBlt::Input in;
    Q_ASSERT( !destBm.isNull() );
    in.destBits = &destBm;
    if( !sourceBm.isNull() )
        in.sourceBits = &sourceBm;
    if( !htBm.isNull() )
        in.halftoneBits = &htBm;

#if 0
    QVector<quint16> before(destBits->count);
    for( int i = 0; i < destBits->count; i++ )
        before[i] = destBits->data[i];
#endif

    in.combinationRule = combinationRule;
    in.destX = destX;
    in.destY = destY;
    in.width = width;
    in.height = height;
    in.sourceX = sourceX;
    in.sourceY = sourceY;
    in.clipX = clipX;
    in.clipY = clipY;
    in.clipWidth = clipWidth;
    in.clipHeight = clipHeight;

    St::BitBlt bb( in );
    bb.copyBits();

#if 0
    bool same = true;
    for( int i = 0; i < destBits->count; i++ )
    {
        if( before[i] != destBits->data[i] )
        {
            same = false;
            break;
        }
    }
#endif

    if( drawToDisp )
    {
        const QRect dest(in.destX, in.destY, in.width, in.height);
        const QRect clip( in.clipX, in.clipY, in.clipWidth, in.clipHeight );
        disp->updateArea( dest & clip );
    }
#if 0
    static int count = 0;
    destBm.toImage().save(QString("screens/bitblt_%1.png").arg(++count,4, 10, QChar('0')));
    // there are indeed changes
    if( drawToDisp && same )
        qDebug() << "copyBits didn't change anything in" << count;
#endif
}

}
