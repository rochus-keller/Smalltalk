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
#include "StObjectMemory2.h"
#include <QFile>
#include <QPainter>
#include <stdint.h>
#include <QtDebug>
#include <QBitmap>
#include <QMessageBox>
#include <QApplication>
#include <QCloseEvent>
using namespace St;

//#define _USE_BB_IMP_

static Display* s_inst = 0;
bool Display::s_run = true;
static const int s_msPerFrame = 30; // 20ms according to BB

Display::Display(QWidget *parent) : QWidget(parent),d_curX(-1),d_curY(-1)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::BlankCursor);
    setWindowTitle( renderTitle() );
    show();
    d_lastEvent = 0;
    d_timer.start();
    startTimer(s_msPerFrame);
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
    d_screen = QImage( d_bitmap.width(), d_bitmap.height(), QImage::Format_Mono );
    setFixedSize( buf.width(), buf.height() );
    update();
}

void Display::setCursorBitmap(const Bitmap& bm)
{
#if 1 // use Qt cursor
    QBitmap pix = QPixmap::fromImage( bm.toImage() );
    setCursor( QCursor( pix, pix, 0, 0 ) );
#else
    d_cursor = bm.toImage();
#endif
    update();
}

void Display::setCursorPos(qint16 x, qint16 y)
{
    d_curX = x;
    d_curY = y;
    update();
}

void Display::paintEvent(QPaintEvent*)
{
    if( d_bitmap.isNull() )
        return;
    QPainter p(this);
#if 0
    p.fillRect(0,0,d_bitmap.width(),d_bitmap.height(), Qt::white );
    p.setPen(Qt::black);
    enum { white = 1, black = 0 };
    img.fill(white);
    for( int y = 0; y < d_bitmap.height(); y++ )
    {
        for( int x = 0; x < d_bitmap.width(); x++ )
        {
            if( d_bitmap.testBit(x,y) )
                img.setPixel(x,y,black); // p.drawPoint(x, y);
        }
    }
#else
    // using QImage is much faster than drawing single points with QPainter
    const int lw = d_bitmap.lineWidth() / 8;
    for( int y = 0; y < d_bitmap.height(); y++ )
    {
        uchar* dest = d_screen.scanLine(y);
        const quint8* src = d_bitmap.scanLine(y);
        for( int x = 0; x < lw; x++ )
            dest[x] = ~src[x];
        //memcpy( dest, src, lw );
    }

#if 0 // draw cursor ourselves
    //d_img.invertPixels();
    if( false ) // d_curX >= 0 && d_curY >= 0 )
    {
        QPainter p(&d_screen);
        p.drawImage( d_curX, d_curY, d_cursor);
    }else if( !d_cursor.isNull() )
    {
#if 1
        QPainter p(&d_screen);
        p.setCompositionMode(QPainter::CompositionMode_ColorBurn);
        p.drawImage( d_mousePos.x(), d_mousePos.y(), d_cursor);
#else
        const int mx = d_mousePos.x();
        const int my = d_mousePos.y();
        for( int y = 0; y < d_cursor.height(); y++ )
        {
            // pixelIndex black=0, white=1
            for( int x = 0; x < d_cursor.width(); x++ )
            {
                if( d_cursor.pixelIndex(x,y) == 0 )
                    d_screen.setPixel( mx + x, my + y, d_screen.pixelIndex( mx + x, my + y ) ? 0 : 1 );
            }
        }
#endif
    }
#endif
#endif
    p.drawImage(0,0,d_screen);
}

void Display::timerEvent(QTimerEvent*)
{
    update();
}

void Display::closeEvent(QCloseEvent* event)
{
    event->ignore();
    const int res = QMessageBox::warning(this, renderTitle(), tr("Do you really want to close the VM? Changes are lost!"),
                                         QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel );
    if( res == QMessageBox::Ok )
        s_run = false;
}

void Display::mouseMoveEvent(QMouseEvent* event)
{
    QPoint old = d_mousePos;
    d_mousePos = event->pos();
    if( d_mousePos.x() < 0 )
        d_mousePos.setX(0);
    if( d_mousePos.y() < 0 )
        d_mousePos.setY(0);
    if( d_mousePos.x() >= width() )
        d_mousePos.setX( width() - 1 );
    if( d_mousePos.y() >= height() )
        d_mousePos.setY( height() - 1 );

    quint32 diff = d_timer.elapsed() - d_lastEvent;
    if( diff < s_msPerFrame )
        return;

    if( old.x() != d_mousePos.x() )
    {
        if( d_mousePos.x() > MaxPos )
            postEvent( XLocation, MaxPos );
        else
            postEvent( XLocation, d_mousePos.x() );
    }
    if( old.y() != d_mousePos.y() )
    {
        if( d_mousePos.y() > MaxPos )
            postEvent( YLocation, MaxPos );
        else
            postEvent( YLocation, d_mousePos.y() );
    }
}

void Display::mousePressEvent(QMouseEvent* event)
{
    switch( event->button() )
    {
    case Qt::LeftButton:
        postEvent( BiStateOn, 130 );
        break;
    case Qt::RightButton:
        postEvent( BiStateOn, 129 ); // BB error, mixed up 129 and 128, VIM fixed
        break;
    case Qt::MidButton:
        postEvent( BiStateOn, 128 );
        break;
    default:
        break;
    }
}

void Display::mouseReleaseEvent(QMouseEvent* event)
{
    switch( event->button() )
    {
    case Qt::LeftButton:
        postEvent( BiStateOff, 130 );
        break;
    case Qt::RightButton:
        postEvent( BiStateOff, 129 ); // BB error, mixed up 129 and 128, VIM fixed
        break;
    case Qt::MidButton:
        postEvent( BiStateOff, 128 );
        break;
    default:
        break;
    }
}

void Display::keyPressEvent(QKeyEvent* event)
{
#if 0
    qDebug() << QByteArray::number(event->key(),16).constData() << event->text();
    if( !event->text().isEmpty() )
    {
        const int ch = event->text()[0].unicode();
        if( ch <= 128)
        {
            postEvent( BiStateOn, ch, true );
            postEvent( BiStateOff, ch, false );
        } // else ignore
    }else
#endif
        // TODO: fix keyboard mappings
    if( !keyEvent( event->key(), true ) )
        QWidget::keyPressEvent(event);
}

void Display::keyReleaseEvent(QKeyEvent* event)
{
    if( !keyEvent( event->key(), false ) )
        QWidget::keyReleaseEvent(event);
}

QString Display::renderTitle() const
{
    return QString("%1 v%2").arg( QApplication::applicationName() ).arg( QApplication::applicationVersion() );
}

static inline quint16 compose( quint8 t, quint16 p )
{
    return t << 12 | p;
}

bool Display::postEvent(Display::EventType t, quint16 param, bool withTime )
{
    Q_ASSERT( t >= XLocation && t <= BiStateOff );

    if( withTime )
    {
        quint32 time = d_timer.elapsed();
        quint32 diff = time - d_lastEvent;
        d_lastEvent = time;

        if( diff <= MaxPos )
        {
            d_events.enqueue( compose( DeltaTime, diff ) );
            emit sigEventQueue();
        }else
        {
            d_events.enqueue( compose( AbsoluteTime, 0 ) );
            emit sigEventQueue();
            d_events.enqueue( ( time >> 16 ) & 0xffff);
            emit sigEventQueue();
            d_events.enqueue( time & 0xffff );
            emit sigEventQueue();
        }
    }
    d_events.enqueue( compose( t, param ) );
    emit sigEventQueue();
    return true;
}

bool Display::keyEvent(int keyCode, bool down)
{
    switch( keyCode )
    {
    case Qt::Key_Backspace:
        return postEvent( down ? BiStateOn : BiStateOff, 8 );
    case Qt::Key_Tab:
        return postEvent( down ? BiStateOn : BiStateOff, 9 );
        // NOTE: line feed	10 not supported
    case Qt::Key_Return:
        return postEvent( down ? BiStateOn : BiStateOff, 13 );
    case Qt::Key_Escape:
        return postEvent( down ? BiStateOn : BiStateOff, 27 );
    case Qt::Key_Space:
        return postEvent( down ? BiStateOn : BiStateOff, 32 );
    case Qt::Key_Delete:
        return postEvent( down ? BiStateOn : BiStateOff, 127 );
        // NOTE: right shift	137
    case Qt::Key_Shift:
        return postEvent( down ? BiStateOn : BiStateOff, 136 );
    case Qt::Key_Control:
        return postEvent( down ? BiStateOn : BiStateOff, 138 );
    case Qt::Key_CapsLock:
        return postEvent( down ? BiStateOn : BiStateOff, 139 );
    }
    if( keyCode >= '!' && keyCode <= '~' )
        return postEvent( down ? BiStateOn : BiStateOff, ::tolower(keyCode) );
    return false;
}

Bitmap::Bitmap(quint8* buf, quint16 wordLen, quint16 pixWidth, quint16 pixHeight)
{
    d_buf = buf;
    // Q_ASSERT( wordLen == pixWidth * pixHeight / 16 ); // empirically found to be true
    // Q_ASSERT( d_width >= 16 && d_height >= 16 ); // empirically found to be true
    d_wordLen = wordLen;
    d_pixWidth = pixWidth;
    d_pixHeight = pixHeight;
    d_pixLineWidth = ( ( d_pixWidth + PixPerWord - 1 ) / PixPerWord ) * PixPerWord; // line width is a multiple of 16
    // d_pixWidth != d_pixLineWidth happens twice
    Q_ASSERT( d_pixLineWidth * d_pixHeight / 16 == d_wordLen );
}

static inline quint16 readU16( const quint8* data, int off )
{
    return ( quint8(data[off]) << 8 ) + quint8(data[off+1] );
}

quint16 Bitmap::wordAt(qint16 i) const
{
    i--; // Smalltalk array indexes start with 1
    if( i < 0 || i >= d_wordLen )
    {
        // this happens five times, always in BitBlt::copyLoop
        qCritical() << "ERROR: Bitmap::wordAt width" << d_pixWidth << "height" << d_pixHeight << "wordlen" << d_wordLen << "out of bounds" << i;
        return 0;
    }
    Q_ASSERT( i < d_wordLen );
    return readU16( d_buf, i * 2 );
}

static inline void writeU16( quint8* data, int off, quint16 val )
{
    data[off] = ( val >> 8 ) & 0xff;
    data[off+1] = val & 0xff;
}

void Bitmap::wordAtPut(qint16 i, quint16 v)
{
    Q_ASSERT( i > 0 && i <= d_wordLen );
    i--;
    writeU16( d_buf, i * 2, v );
}

QImage Bitmap::toImage() const
{
    if( isNull() )
        return QImage();
    QImage img( width(), height(), QImage::Format_Mono );
    const int lw = lineWidth() / 8;
    for( int y = 0; y < height(); y++ )
    {
        uchar* dest = img.scanLine(y);
        const quint8* src = scanLine(y);
        for( int x = 0; x < lw; x++ )
            dest[x] = ~src[x];
    }
    return img;
}

QImage Bitmap::toImage(quint16 x, quint16 y, quint16 w, quint16 h) const
{
    if( isNull() || x >= d_pixWidth || y >= d_pixHeight || ( x + w > d_pixWidth ) || ( y + h > d_pixHeight ) )
        return QImage();
    QImage img( w, h, QImage::Format_Mono );
    img.fill(1);
    for( int yy = 0; yy < h; yy++ )
    {
        for( int xx = 0; xx < w; xx++ )
        {
            if( test( xx + x, yy + y ) )
                img.setPixel( xx, yy, 0 );
        }
    }
    return img;
}

BitBlt::BitBlt(const Input& in):
    sourceBits( in.sourceBits ),
    destBits( in.destBits ),
    halftoneBits( in.halftoneBits ),
    combinationRule( in.combinationRule ),
    destX( in.destX ), clipX( in.clipX ), clipWidth( in.clipWidth ), sourceX( in.sourceX ), width( in.width ),
    destY( in.destY ), clipY( in.clipY ), clipHeight( in.clipHeight ), sourceY( in.sourceY ), height( in.height )
{
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
    // set sx/y, dx/y, w and h so that dest doesn't exceed clipping range and
    // source only covers what needed by clipped dest
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
    // halftoneBits = halftoneForm bits
    skew = ( sx - dx ) & 15;
    qint16 startBits = 16 - ( dx & 15 );
    mask1 = RightMasks[ startBits /* + 1 */ ]; // ST array index starts with 1
    qint16 endBits = 15 - ( ( dx + w - 1 ) & 15 );
    mask2 = ~RightMasks[ endBits /* + 1 */ ];
    skewMask = skew == 0 ? 0 : RightMasks[ 16 - skew /* + 1 */ ];
    if( w < startBits )
    {
        mask1 = mask1 & mask2;
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
        // is never executed
#ifdef _USE_BB_IMP_
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
            qint16 t = mask1;
            mask1 = mask2;
            mask2 = t;
        }
#else
        qWarning() << "WARINING: BitBlt::checkOverlap for sourceBits == destBits && dy >= sy not implemented";
#endif
    }
}

void BitBlt::calculateOffsets()
{
    preload = ( sourceBits != 0 && skew != 0 && skew <= ( sx & 15 ) );
    if( hDir < 0 )
        preload = preload == false;
    sourceIndex = sy * sourceRaster + ( sx / 16 );
    destIndex = dy * destRaster + ( dx / 16 );
    sourceDelta = ( sourceRaster * vDir ) - ( nWords + ( preload ? 1 : 0 ) * hDir );
    destDelta = ( destRaster * vDir ) - ( nWords * hDir );
}

void BitBlt::copyLoop()
{
#ifdef _USE_BB_IMP_

    // this code doesn't seem to properly work

    qint16 prevWord, thisWord, skewWord, mergeMask,
            halftoneWord, mergeWord, word;
    for( int i = 1; i <= h; i++ )
    {
        if( halftoneBits != 0 )
        {
            halftoneWord = halftoneBits->wordAt( 1 + ( dy & 15 ) );
            dy = dy + vDir;
        }else
            halftoneWord = AllOnes;
        skewWord = halftoneWord;
        if( preload && sourceBits != 0 )
        {
            prevWord = sourceBits->wordAt( sourceIndex + 1 );
            sourceIndex = sourceIndex + hDir;
        }else
            prevWord = 0;
        mergeMask = mask1;
        for( word = 1; word <= nWords; word++ )
        {
            if( sourceBits != 0 )
            {
                prevWord = prevWord & skewMask;
                thisWord = sourceBits->wordAt( sourceIndex + 1 );
                skewWord = prevWord | ( thisWord & ~skewMask );
                prevWord = thisWord;
                skewWord = ObjectMemory2::bitShift( skewWord, skew ) | ObjectMemory2::bitShift( skewWord, skew - 16 );
            }
            mergeWord = merge( skewWord & halftoneWord, destBits->wordAt( destIndex + 1 ) );
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
#else

    // this code produces the same screen as the "Smalltalk 80 Virtual Image Version 2" handbook and
    // doesn't seem to be slower than the above BB code
    for( int y = 0; y < h; y++ )
    {
        for( int x = 0; x < w; x++ )
        {
            const quint16 dest = destBits->test(dx+x,dy+y);
            if( sourceBits )
            {
                bool bit = merge( sourceBits->test(sx+x,sy+y), dest ) != 0;
                destBits->set( dx+x, dy+y, bit );
            }else
            {
                const bool halftone = halftoneBits != 0 ? halftoneBits->test( x & 15, y & 15 ) : AllOnes;
                bool bit = merge( halftone, dest ) != 0;
                destBits->set( dx+x, dy+y, bit );
            }
        }
    }

#endif
}

qint16 BitBlt::merge(qint16 source, qint16 destination)
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

const QList<qint16> BitBlt::RightMasks = QList<qint16>() <<
    0 << 0x1 << 0x3 << 0x7 << 0xf <<
       0x1f << 0x3f << 0x7f << 0xff <<
       0x1ff << 0x3ff << 0x7ff << 0xfff <<
       0x1fff << 0x3fff << 0x7fff << 0xffff;

const qint16 BitBlt::AllOnes = 0xffff;
