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
#include <QShortcut>
#include <QClipboard>
using namespace St;

#define _USE_BB_IMP_

static Display* s_inst = 0;
bool Display::s_run = true;
bool Display::s_break = false;
bool Display::s_copy = false;
QList<QFile*> Display::s_files;

static const int s_msPerFrame = 30; // 20ms according to BB
enum { whitePixel = 1, blackPixel = 0 };
static QFile s_out("st.log");

static QtMessageHandler s_oldHandler = 0;
void messageHander(QtMsgType type, const QMessageLogContext& ctx, const QString& message)
{
    if( type == QtDebugMsg )
    {
        if( s_out.isOpen() )
        {
            s_out.write(message.toUtf8());
            s_out.write("\n");
        }
    }else if( s_oldHandler )
        s_oldHandler(type, ctx, message );
}

Display::Display(QWidget *parent) : QWidget(parent),d_curX(-1),d_curY(-1),d_capsLockDown(false),
    d_shiftDown(false),d_recOn(false),d_forceClose(false),d_eventCb(0)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::BlankCursor);
    setWindowTitle( renderTitle() );
    show();
    d_lastEvent = 0;
    d_elapsed.start();
    // startTimer(s_msPerFrame);
#ifndef ST_DISPLAY_WORDARRY
    new QShortcut(tr("ALT+R"), this, SLOT(onRecord()) );
    new QShortcut(tr("ALT+L"), this, SLOT(onLog()) );
    new QShortcut(tr("ALT+X"), this, SLOT(onExit()) );
    new QShortcut(tr("ALT+B"), this, SLOT(onBreak()) );

    s_oldHandler = qInstallMessageHandler(messageHander);
#endif
    new QShortcut(tr("ALT+V"), this, SLOT(onPaste()) );
    new QShortcut(tr("ALT+SHIFT+V"), this, SLOT(onPasteBenchmark()) );
    new QShortcut(tr("ALT+C"), this, SLOT(onCopy()) );
}

Display::~Display()
{
    s_inst = 0;
    foreach( QFile* f, s_files )
        delete f;
    s_files.clear();
}

Display*Display::inst()
{
    if( s_inst == 0 )
    {
        s_inst = new Display();
        s_run = true;
    }
    return s_inst;
}

void Display::forceClose()
{
    if( s_inst )
    {
        s_inst->d_forceClose = true;
        s_inst->close();
    }
}

void Display::setBitmap(const Bitmap& buf)
{
    d_bitmap = buf;
    d_screen = QImage( buf.width(), buf.height(), QImage::Format_RGB32 );
    d_bitmap.toImage(d_screen);
    d_updateArea = QRect();
    setFixedSize( buf.width(), buf.height() );
    update();
}

void Display::setCursorBitmap(const Bitmap& bm)
{
    QImage cursor( bm.width(), bm.height(), QImage::Format_RGB32 );
    bm.toImage(cursor);
    QBitmap pix = QPixmap::fromImage( cursor );
    setCursor( QCursor( pix, pix, 0, 0 ) );
    // update();
}

void Display::setCursorPos(qint16 x, qint16 y)
{
    d_curX = x;
    d_curY = y;
    update();
}

void Display::drawRecord(int x, int y, int w, int h)
{
    if( !d_recOn )
        return;
    QPainter p(&d_record);
    if( w < 0 || h < 0 )
        p.setPen(Qt::red);
    else
        p.setPen(Qt::green);
    p.drawRect(x,y,w,h);
}

void Display::updateArea(const QRect& r )
{
    d_updateArea |= r;

    update( r );
}

void Display::setLog(bool on)
{
    if( on && !s_out.isOpen() )
        onLog();
    else if( !on && s_out.isOpen() )
        onLog();
}

void Display::processEvents()
{
    static quint32 last = 0;
    static quint32 count = 0;

    if( count > 4000 )
    {
        count = 0;
        Display* d = Display::inst();
        const quint32 cur = d->d_elapsed.elapsed();
        if( ( cur - last ) >= 30 )
        {
            last = cur;
            QApplication::processEvents();
        }
    }else
        count++;
}

void Display::copyToClipboard(const QByteArray& str)
{
    QString text = QString::fromUtf8(str);
    text.replace( '\r', '\n' );
    QApplication::clipboard()->setText( text );
}

void Display::onRecord()
{
    if( !d_recOn )
    {
        qWarning() << "record on";
        d_recOn = true;
        d_record = d_screen.convertToFormat(QImage::Format_RGB32);
    }else
    {
        qWarning() << "record off";
        d_record.save("record.png");
        d_record = QImage();
        d_recOn = false;
    }
}

void Display::onExit()
{
    exit(0);
}

void Display::onLog()
{
    if( s_out.isOpen() )
    {
        qWarning() << "logging off";
        s_out.close();
    }else if( s_out.open(QIODevice::WriteOnly) )
    {
        qWarning() << "logging on";
    }else
        qCritical() << "ERROR: cannot open log for writing";
}

void Display::onBreak()
{
    if( s_break )
        return;
    s_break = true;
    qWarning() << "break started";
}

void Display::onPaste()
{
    const QByteArray text = QApplication::clipboard()->text().toLatin1();
    for( int i = 0; i < text.size(); i++ )
    {
        simulateKeyEvent(text[i]);
    }
}

void Display::onCopy()
{
    s_copy = true;
}

void Display::onPasteBenchmark()
{
    QFile in(":/benchmark/Benchmark.st");
    if( in.open(QIODevice::ReadOnly) )
    {
        const QByteArray text = in.readAll();
        for( int i = 0; i < text.size(); i++ )
        {
            simulateKeyEvent(text[i]);
        }
    }
}

void Display::paintEvent(QPaintEvent* event)
{
    if( d_bitmap.isNull() )
        return;

    const QRect r = event->rect();
    if( r.isNull() )
        return;

    if( !d_updateArea.isNull() )
    {
        d_bitmap.toImage( d_screen, d_updateArea );
        d_updateArea = QRect();
    }

    QPainter p(this);
    p.setRenderHints( QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform, false );

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
            for( int x = 0; x < d_cursor.width(); x++ )
            {
                if( d_cursor.pixelIndex(x,y) == 0 )
                    d_screen.setPixel( mx + x, my + y, d_screen.pixelIndex( mx + x, my + y ) ? blackPixel : whitePixel );
            }
        }
#endif
    }
#endif

    p.drawImage( r,d_screen, r);
}

void Display::timerEvent(QTimerEvent*)
{
    // update();
}

void Display::closeEvent(QCloseEvent* event)
{
    if( d_forceClose || !s_run )
    {
        event->accept();
        deleteLater();
        return;
    }
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

    quint32 diff = d_elapsed.elapsed() - d_lastEvent;
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

enum MousButton { LeftButton = 130,
                  MidButton = 128, // BB error, mixed up 129 and 128, VIM fixed
                  RightButton = 129
                };

void Display::mousePressEvent(QMouseEvent* event)
{
    mousePressReleaseImp( true, event->button() );
}

void Display::mouseReleaseEvent(QMouseEvent* event)
{
    mousePressReleaseImp( false, event->button() );
}

void Display::mousePressReleaseImp(bool press, int button)
{
    const EventType t = press ? BiStateOn : BiStateOff;

    switch( button )
    {
    case Qt::LeftButton:
        if( QApplication::keyboardModifiers() == 0 )
            postEvent( t, LeftButton );
        else if( QApplication::keyboardModifiers() == Qt::ControlModifier )
            postEvent( t, RightButton );
        else if( QApplication::keyboardModifiers() == ( Qt::ControlModifier | Qt::ShiftModifier ) )
            postEvent( t, MidButton );
        break;
    case Qt::RightButton:
        if( QApplication::keyboardModifiers() == Qt::ShiftModifier )
            postEvent( t, MidButton );
        else
            postEvent( t, RightButton );
        break;
    case Qt::MidButton:
        postEvent( t, MidButton );
        break;
    default:
        break;
    }
}

void Display::keyPressEvent(QKeyEvent* event)
{
    //qDebug() << "keyPressEvent" << QByteArray::number(event->key(),16).constData() << event->text();
    char ch = 0;
    if( !event->text().isEmpty() )
        ch = event->text()[0].toLatin1();
    if( !keyEvent( event->key(), ch, true ) )
        QWidget::keyPressEvent(event);
}

void Display::keyReleaseEvent(QKeyEvent* event)
{
    char ch = 0;
    if( !event->text().isEmpty() )
        ch = event->text()[0].toLatin1();
    if( !keyEvent( event->key(), ch, false ) )
        QWidget::keyReleaseEvent(event);
}

void Display::inputMethodEvent(QInputMethodEvent* event)
{
    QString text = event->commitString();

    if( !text.isEmpty() && text.at(0).isPrint() )
    {
        const char ch = text.at(0).toLatin1();
        keyEvent( 0, ch, true );
        keyEvent( 0, ch, false );
    }
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
        quint32 time = d_elapsed.elapsed();
        quint32 diff = time - d_lastEvent;
        d_lastEvent = time;

        if( diff <= MaxPos )
        {
            d_events.enqueue( compose( DeltaTime, diff ) );
            notify();
        }else
        {
            d_events.enqueue( compose( AbsoluteTime, 0 ) );
            notify();
            d_events.enqueue( ( time >> 16 ) & 0xffff);
            notify();
            d_events.enqueue( time & 0xffff );
            notify();
        }
    }
    d_events.enqueue( compose( t, param ) );
    notify();
    return true;
}

/*
    // Alto keyboard layout
    // see https://www.extremetech.com/wp-content/uploads/2011/10/Alto_Mouse_c.jpg
    1 !
    2 @
    3 #
    4 $
    5 %
    6 ~
    7 &
    8 *
    9 (
    0 )
    - _
    = +
    \ |
    [ {
    ] }
    ← ↑
    ; :
    ' "
    , <
    . >
    / ?
    a A
    ...
    z Z
  */

static inline char toAltoUpper( char ch )
{
    switch( ch )
    {
    case '+':
        return '='; // means: the key is labeled with '=' for normal press and '+' for shift press
                    // if we want a '+' to appear we have to send shift-down '=' shift-up to the VM
    case '_':
        return '-';
    case '|':
        return '\\';
    case '{':
        return '[';
    case '}':
        return ']';
    case ':':
        return ';';
    case '"':
        return '\'';
    case '<':
        return ',';
    case '>':
        return '.';
    case '?':
        return '/';
    case '!':
        return '1';
    case '@':
        return '2';
    case '#':
        return '3';
    case '$':
        return '4';
    case '%':
        return '5';
    case '~':
        return '6';
    case '&':
        return '7';
    case '*':
        return '8';
    case '(':
        return '9';
    case ')':
        return '0';
    }
    if( ch >= 'A' && ch <= 'Z' )
        return ::tolower(ch);
    return 0;
}

static inline bool isAltoLower(char ch )
{
    if( ( ch >= 'a' && ch <= 'z' ) || ( ch >= '0' && ch <= '9' ) )
        return true;
    switch( ch )
    {
    case '-':
    case '=':
    case '\\':
    case '[':
    case ']':
    case ';':
    case '\'':
    case ',':
    case '.':
    case '/':
        return true;
    }
    return false;
}

bool Display::keyEvent(int keyCode, char ch, bool down)
{
    //qDebug() << QByteArray::number(keyCode,16).constData() << ch;
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
        d_shiftDown = down;
        return postEvent( down ? BiStateOn : BiStateOff, 136 );
    case Qt::Key_Control:
        return postEvent( down ? BiStateOn : BiStateOff, 138 );
    case Qt::Key_CapsLock:
        d_capsLockDown = down;
        return postEvent( down ? BiStateOn : BiStateOff, 139 );
    case Qt::Key_Left:
        // ← ASCII 95 0x5f _
        return postEvent( down ? BiStateOn : BiStateOff, 95 );
    case Qt::Key_Up:
        // ↑ ASCII 94 0x5e ^
        return postEvent( down ? BiStateOn : BiStateOff, 94 );
    }
    if( ch >= '!' && ch <= '~' )
    {
        if( isAltoLower( ch ) )
        {
            if( down )
                sendShift( true, false );
            const bool res = postEvent( down ? BiStateOn : BiStateOff, ch );
            if( !down )
                sendShift( false, false );
            return res;
        }else if( ( ch = toAltoUpper( ch ) ) )
        {
            if( down )
                sendShift( true, true );
            const bool res = postEvent( down ? BiStateOn : BiStateOff, ch );
            if( !down )
                sendShift( false, true );
            return res;
        }
    }
    return false;
}

void Display::simulateKeyEvent(char ch)
{
    switch( ch )
    {
    case ' ':
        keyEvent(Qt::Key_Space,0,true);
        keyEvent(Qt::Key_Space,0,false);
        return;
    case '\n':
        keyEvent(Qt::Key_Return,0,true);
        keyEvent(Qt::Key_Return,0,false);
        return;
    case '\r':
        return;
    case 0x08:
        keyEvent(Qt::Key_Backspace,0,true);
        keyEvent(Qt::Key_Backspace,0,false);
        return;
    case 0x09:
        keyEvent(Qt::Key_Tab,0,true);
        keyEvent(Qt::Key_Tab,0,false);
        return;
    case 0x1b:
        keyEvent(Qt::Key_Escape,0,true);
        keyEvent(Qt::Key_Escape,0,false);
        return;
    }
    keyEvent(0,ch,true);
    keyEvent(0,ch,false);
}

void Display::sendShift(bool keyPress, bool shiftRequired)
{
    if( shiftRequired && !d_shiftDown ) // need to press shift
        postEvent( keyPress ? BiStateOn : BiStateOff, 136 );
    else if( !shiftRequired && d_shiftDown ) // need to release shift
        postEvent( !keyPress ? BiStateOn : BiStateOff, 136 );
}

void Display::notify()
{
    emit sigEventQueue();
    if( d_eventCb )
        d_eventCb();
}

#ifndef ST_DISPLAY_WORDARRY
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

static inline void writeU16( quint8* data, int off, quint16 val )
{
    data[off] = ( val >> 8 ) & 0xff;
    data[off+1] = val & 0xff;
}

void Bitmap::wordAtPut(quint16 i, quint16 v)
{
    Q_ASSERT( i <= d_wordLen );
    i--;
    writeU16( d_buf, i * 2, v );
}

void Bitmap::toImage(QImage& img, QRect area) const
{
    if( isNull() )
        return;
    Q_ASSERT( img.format() == QImage::Format_RGB32 && img.width() == d_pixWidth && img.height() == d_pixHeight );
    // more efficient than Mono because the Qt pipeline has to convert it otherwise

    if( area.isNull() )
        area = img.rect();
    else
    {
        Q_ASSERT( area.x() >= 0 && ( area.x() + area.width() ) <= d_pixWidth );
        Q_ASSERT( area.y() >= 0 && ( area.y() + area.height() ) <= d_pixHeight );
    }

    const int sw = d_pixLineWidth / 8;
    const int dw = img.bytesPerLine();
    const uchar *src_data = d_buf;
    uchar *dest_data = img.bits();
    const int ax = area.x();
    const int aw = area.width();
    const int axaw = ax + aw;
    const int ah = area.height();
    const int ay = area.y();

    src_data += sw * ay;
    dest_data += dw * ay;
    for( int y = 0; y < ah; y++ )
    {
        uint*p = (uint*)dest_data;
        p += ax;
        for( int x = ax; x < axaw; x++ )
            *p++ = ((src_data[x>>3] >> (7 - (x & 7))) & 1) ? 0xff000000 : 0xffffffff;
        src_data += sw;
        dest_data += dw;
    }
}

#else
Bitmap::Bitmap(quint16* array, quint16 wordLen, quint16 pixWidth, quint16 pixHeight)
{
    d_buf = array;
    d_wordLen = wordLen;
    d_pixWidth = pixWidth;
    d_pixHeight = pixHeight;
    d_wordWidth = ( d_pixWidth + 15 ) / 16;

    Q_ASSERT( d_wordWidth * d_pixHeight == d_wordLen );
}

void Bitmap::toImage(QImage& img, QRect area) const
{
    if( isNull() )
        return;
    Q_ASSERT( img.format() == QImage::Format_RGB32 && img.width() == d_pixWidth && img.height() == d_pixHeight );
    // more efficient than Mono because the Qt pipeline has to convert it otherwise

    if( area.isNull() )
        area = img.rect();
    else
    {
        Q_ASSERT( area.x() >= 0 && ( area.x() + area.width() ) <= d_pixWidth );
        Q_ASSERT( area.y() >= 0 && ( area.y() + area.height() ) <= d_pixHeight );
    }

    const int sw = d_wordWidth;
    const int dw = img.bytesPerLine();
    const quint16 *src_data = d_buf; // quint16 statt uchar
    uchar *dest_data = img.bits();
    const int ax = area.x();
    const int aw = area.width();
    const int axaw = ax + aw;
    const int ah = area.height();
    const int ay = area.y();

    src_data += sw * ay;
    dest_data += dw * ay;
    for( int y = 0; y < ah; y++ )
    {
        uint*p = (uint*)dest_data;
        p += ax;
        for( int x = ax; x < axaw; x++ )
            *p++ = ((src_data[x>>4] >> (15 - (x & 15))) & 1) ? 0xff000000 : 0xffffffff; // 15 statt 7
        src_data += sw;
        dest_data += dw;
    }
}
#endif

BitBlt::BitBlt(const Input& in):
    sourceBits( in.sourceBits ),
    destBits( in.destBits ),
    halftoneBits( in.halftoneBits ),
    combinationRule( in.combinationRule ),
    destX( in.destX ), clipX( in.clipX ), clipWidth( in.clipWidth ), sourceX( in.sourceX ), width( in.width ),
    destY( in.destY ), clipY( in.clipY ), clipHeight( in.clipHeight ), sourceY( in.sourceY ), height( in.height )
{
    Q_ASSERT( sourceBits != 0 || halftoneBits != 0 );
    // at least source or ht is present or both
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

    if( sourceBits == 0 )
        return;

    if( sx < 0 )
    {
        dx = dx - sx;
        w = w + sx;
        sx = 0;
    }
    if( ( sx + w ) > sourceBits->width() )
        w = w - ( sx + w - sourceBits->width() );
    if( sy < 0 )
    {
        dy = dy - sy;
        h = h + sy;
        sy = 0;
    }
    if( ( sy + h ) > sourceBits->height() )
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
    const quint16 startBits = 16 - ( dx & 15 );
    mask1 = RightMasks[ startBits /* + 1 */ ]; // ST array index starts with 1
    const quint16 endBits = 15 - ( ( dx + w - 1 ) & 15 );
    mask2 = ~RightMasks[ endBits /* + 1 */ ];
    skewMask = skew == 0 ? 0 : RightMasks[ 16 - skew /* + 1 */ ];
    if( w < startBits )
    {
        mask1 = mask1 & mask2;
        mask2 = 0;
        nWords = 1;
    }else
        // nWords = ( w - startBits - 1 ) / 16 + 2; // BB error, doesn't work
        // fix found in https://github.com/dbanay/Smalltalk/blob/master/src/bitblt.cpp
        // ERROR dbanay : nWords <-  (w - startBits + 15) // 16 + 1 for False case"
        nWords = ( w - startBits + 15) / 16 + 1;
}

void BitBlt::checkOverlap()
{
    hDir = vDir = 1;
    if( sourceBits && destBits && sourceBits->isSameBuffer(*destBits) && dy >= sy )
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
            qint16 t = mask1;
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
    sourceIndex = sy * sourceRaster + ( sx / 16 );
    destIndex = dy * destRaster + ( dx / 16 );
    sourceDelta = ( sourceRaster * vDir ) - ( (nWords + ( preload ? 1 : 0 ) ) * hDir );
    destDelta = ( destRaster * vDir ) - ( nWords * hDir );
}

void BitBlt::copyLoop()
{
#ifdef _USE_BB_IMP_
    quint16 prevWord, thisWord, skewWord, mergeMask,
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
                if( word <= sourceRaster && sourceIndex >= 0 && sourceIndex < sourceBits->wordLen() )
                    thisWord = sourceBits->wordAt( sourceIndex + 1 );
                else
                    thisWord = 0;
                skewWord = prevWord | ( thisWord & ~skewMask );
                prevWord = thisWord;
                // does not work:
                // skewWord = ObjectMemory2::bitShift( skewWord, skew ) | ObjectMemory2::bitShift( skewWord, skew - 16 );
                skewWord = ( skewWord << skew ) | ( skewWord >> -( skew - 16 ) );
            }
            if( destIndex >= destBits->wordLen() )
                return;
            const quint16 destWord =  destBits->wordAt( destIndex + 1 );
            mergeWord = merge( combinationRule, skewWord & halftoneWord, destWord );
            destBits->wordAtPut( destIndex + 1, ( mergeMask & mergeWord ) | ( ~mergeMask & destWord ) );
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

    // qDebug() << "copyLoop destination" << dx << dy << w << h << ( sourceBits != 0 ? "source" : "" ) << ( halftoneBits != 0 ? "ht" : "" );

    // this code produces the same screen as the "Smalltalk 80 Virtual Image Version 2" handbook and
    // doesn't seem to be slower than the above BB code
    // has issues though with italic fonts
    for( int y = 0; y < h; y++ )
    {
        for( int x = 0; x < w; x++ )
        {
            const quint16 dest = destBits->test(dx+x,dy+y);
            if( sourceBits )
            {
                bool bit = merge( combinationRule, sourceBits->test(sx+x,sy+y), dest ) != 0;
                destBits->set( dx+x, dy+y, bit );
            }else
            {
                const bool halftone = halftoneBits != 0 ? halftoneBits->test( x & 15, y & 15 ) : AllOnes;
                bool bit = merge( combinationRule, halftone, dest ) != 0;
                destBits->set( dx+x, dy+y, bit );
            }
        }
    }

#endif
}

quint16 BitBlt::merge(quint16 combinationRule, quint16 source, quint16 destination)
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
    default:
        Q_ASSERT( false );
        break;
    }
    return 0;
}

const QList<qint16> BitBlt::RightMasks = QList<qint16>() <<
    0 << 0x1 << 0x3 << 0x7 << 0xf <<
       0x1f << 0x3f << 0x7f << 0xff <<
       0x1ff << 0x3ff << 0x7ff << 0xfff <<
       0x1fff << 0x3fff << 0x7fff << 0xffff;

const qint16 BitBlt::AllOnes = 0xffff;


