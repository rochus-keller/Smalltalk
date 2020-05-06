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


Display::Display(QWidget *parent) : QWidget(parent),d_buf(0)
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

void Display::setBuffer(const quint8* buf, quint16 width, quint16 height)
{
    d_width = width;
    d_height = height;
    d_buf = buf;
    update();

#if 0
    QFile out( "bitmap" );
    out.open(QIODevice::WriteOnly);
    out.write((const char*)buf, d_width * d_height / 8 );
#endif
}

void Display::paintEvent(QPaintEvent*)
{
    if( d_buf == 0 )
        return;
    QPainter p(this);
    p.fillRect(0,0,d_width,d_height, Qt::white );
    p.setPen(Qt::black);

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

}

void Display::timerEvent(QTimerEvent*)
{
    update();
}

