#ifndef STDISPLAY_H
#define STDISPLAY_H

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

#include <QElapsedTimer>
#include <QFile>
#include <QQueue>
#include <QWidget>


namespace St
{
#ifdef ST_DISPLAY_WORDARRY
class Bitmap
{
public:
    Bitmap():d_buf(0),d_wordLen(0) {}
    Bitmap( quint16* array, quint16 wordLen, quint16 pixWidth, quint16 pixHeight );
    void toImage(QImage&, QRect = QRect()) const;
    quint16 width() const { return d_pixWidth; }
    quint16 height() const { return d_pixHeight; }
    quint16 wordLen() const { return d_wordLen; }
    bool isNull() const { return d_buf == 0; }
    bool isSameBuffer( const Bitmap& rhs ) const { return rhs.d_buf == d_buf; }
    bool isSameBuffer( quint16* buf ) const { return d_buf == buf; }
    inline quint16 wordAt(quint16 i) const
    {
        i--; // Smalltalk array indexes start with 1
        Q_ASSERT( i < d_wordLen );
        return d_buf[i];
    }
    void wordAtPut( quint16 i, quint16 v )
    {
        i--; // Smalltalk array indexes start with 1
        Q_ASSERT( i < d_wordLen );
        d_buf[i] = v;
    }
private:
    quint16 d_pixWidth, d_pixHeight, d_wordLen, d_wordWidth;
    quint16* d_buf;
};
#else
    class Bitmap
    {
    public:
        enum { PixPerByte = 8, PixPerWord = PixPerByte * 2 };
        Bitmap():d_buf(0),d_wordLen(0) {}
        Bitmap( quint8* buf, quint16 wordLen, quint16 pixWidth, quint16 pixHeight );
        inline const quint8* scanLine(int y) const
        {
            return d_buf + ( y * d_pixLineWidth / PixPerByte );
        }
        quint16 lineWidth() const { return d_pixLineWidth; }
        quint16 width() const { return d_pixWidth; }
        quint16 height() const { return d_pixHeight; }
        quint16 wordLen() const { return d_wordLen; }
        inline quint16 wordAt(quint16 i) const
        {
            i--; // Smalltalk array indexes start with 1
            Q_ASSERT( i < d_wordLen );
            return readU16( d_buf, i * 2 );
        }
        void wordAtPut( quint16 i, quint16 v );
        bool isNull() const { return d_buf == 0; }
        bool isSameBuffer( const Bitmap& rhs ) const { return rhs.d_buf == d_buf; }
        void toImage(QImage&, QRect = QRect()) const;
    protected:
        static inline quint16 readU16( const quint8* data, int off )
        {
            return ( quint8(data[off]) << 8 ) + quint8(data[off+1] );
        }

    private:
        quint16 d_pixWidth, d_pixHeight, d_pixLineWidth, d_wordLen;
        quint8* d_buf;
    };
#endif

    class Display : public QWidget
    {
        Q_OBJECT
    public:
        typedef void (*EventCallback)();
        enum EventType {
            DeltaTime = 0,
            XLocation = 1,
            YLocation = 2,
            BiStateOn = 3,
            BiStateOff = 4,
            AbsoluteTime = 5, // followed by 2 words
        };
        enum { MaxPos = 0xfff }; // 12 bits

        explicit Display(QWidget *parent = 0);
        ~Display();
        static Display* inst();
        static void forceClose();
        static bool s_run;
        static bool s_break;
        static bool s_copy;
        static QList<QFile*> s_files;

        void setBitmap( const Bitmap& );
        const Bitmap& getBitmap() const { return d_bitmap; }
        void setCursorBitmap( const Bitmap& );
        void setCursorPos( qint16 x, qint16 y );
        const QPoint& getMousePos() const { return d_mousePos; }
        quint16 nextEvent() { return d_events.dequeue(); }
        void clearEvents() { d_events.clear(); }
        quint32 getTicks() const { return d_elapsed.elapsed(); }
        void drawRecord( int x, int y, int w, int h );
        bool isRecOn() const { return d_recOn; }
        void updateArea(const QRect& r);
        void setLog(bool on);
        void setEventCallback( EventCallback cb ) { d_eventCb = cb; }
        static void processEvents();
        static void copyToClipboard( const QByteArray& );
    signals:
        void sigEventQueue();

    protected slots:
        void onRecord();
        void onExit();
        void onLog();
        void onBreak();
        void onPaste();
        void onCopy();
        void onPasteBenchmark();

    protected:
        void paintEvent(QPaintEvent *);
        void timerEvent(QTimerEvent *);
        void closeEvent(QCloseEvent * event);
        void mouseMoveEvent(QMouseEvent * event);
        void mousePressEvent(QMouseEvent * event);
        void mouseReleaseEvent(QMouseEvent *event);
        void mousePressReleaseImp(bool press, int button );
        void keyPressEvent(QKeyEvent* event);
        void keyReleaseEvent(QKeyEvent* event);
        void inputMethodEvent(QInputMethodEvent *);
        QString renderTitle() const;
        bool postEvent(EventType, quint16 param = 0 , bool withTime = true);
        bool keyEvent( int keyCode, char ch, bool down );
        void simulateKeyEvent( char ch );
        void sendShift(bool keyPress, bool shiftRequired);
        void notify();
    private:
        Bitmap d_bitmap;
        QImage d_screen;
        QImage d_cursor;
        qint16 d_curX, d_curY;
        QPoint d_mousePos;
        QQueue<quint16> d_events;
        quint32 d_lastEvent; // number of milliseconds since last event was posted to queue
        QElapsedTimer d_elapsed;
        QImage d_record;
        EventCallback d_eventCb;
        QRect d_updateArea;
        bool d_shiftDown, d_capsLockDown, d_recOn, d_forceClose;
    };

    class BitBlt
    {
    public:
        // This is a textbook implementation according to Blue Book chapter 18 "Simulation of BitBlt".
        // Known to be inefficient; focus is on functionality and compliance.
        struct Input
        {
            const Bitmap* sourceBits;
            Bitmap* destBits;
            const Bitmap* halftoneBits;
            qint16 combinationRule;
            qint16 destX, clipX, clipWidth, sourceX, width;
            qint16 destY, clipY, clipHeight, sourceY, height;
            Input():sourceBits(0),destBits(0),halftoneBits(0),combinationRule(0),
                destX(0),clipX(0),clipWidth(0),sourceX(0),width(0),
                destY(0),clipY(0),clipHeight(0),sourceY(0),height(0) {}
        };
        BitBlt( const Input& );
        void copyBits();
    protected:
        void clipRange();
        void computeMasks();
        void checkOverlap();
        void calculateOffsets();
        void copyLoop();
        static inline quint16 merge(quint16 combinationRule, quint16 source, quint16 destination );
    private:
        const Bitmap* sourceBits;
        const Bitmap* halftoneBits;
        const qint16 destX, clipX, clipWidth, sourceX, width; // pixel
        const qint16 destY, clipY, clipHeight, sourceY, height; // pixel
        const qint16 combinationRule;
        Bitmap* destBits;
        qint16 sourceRaster;
        qint16 destRaster;
        qint16 skew, nWords, vDir, hDir;
        quint16 mask1, mask2, skewMask;
        qint16 sx, sy, dx, dy, w, h; // pixel
        qint16 sourceIndex, destIndex, sourceDelta, destDelta;
        bool preload;
        static const QList<qint16> RightMasks;
        static const qint16 AllOnes;
    };
}

#endif // STDISPLAY_H
