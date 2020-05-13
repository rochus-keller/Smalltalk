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

#include <QWidget>


namespace St
{
    class Bitmap
    {
    public:
        Bitmap():d_buf(0),d_wordLen(0) {}
        Bitmap( quint8* buf, quint16 wordLen, quint16 pixWidth, quint16 pixHeight );
        bool testBit( quint16 x, quint16 y ) const;
        quint16 width() const { return d_width; }
        quint16 height() const { return d_height; }
        quint16 wordAt(quint16 i ) const;
        void wordAtPut( quint16 i, quint16 v );
        bool isNull() const { return d_buf == 0; }
    private:
        quint16 d_width, d_height, d_lineWidth, d_wordLen;
        quint8* d_buf;
    };

    class Display : public QWidget
    {
        Q_OBJECT
    public:
        explicit Display(QWidget *parent = 0);
        static Display* inst();

        void setBitmap( const Bitmap& );
    protected:
        void paintEvent(QPaintEvent *);
        void timerEvent(QTimerEvent *);

    private:
        Bitmap d_bitmap;
    };

    class BitBlt
    {
    public:
        struct Input
        {
            Bitmap* sourceBits;
            Bitmap* destBits;
            Bitmap* halftoneBits;
            quint8 combinationRule;
            int destX, clipX, clipWidth, sourceX, width;
            int destY, clipY, clipHeight, sourceY, height;
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
        quint16 merge( quint16 source, quint16 destination );
    private:
        Bitmap* sourceBits;
        quint16 sourceRaster;
        Bitmap* destBits;
        quint16 destRaster;
        Bitmap* halftoneBits;
        int skew, mask1, mask2, skewMask, nWords, vDir, hDir;
        int sx, sy, dx, dy, w, h;
        int destX, clipX, clipWidth, sourceX, width;
        int destY, clipY, clipHeight, sourceY, height;
        int sourceIndex, destIndex, sourceDelta, destDelta;
        quint8 combinationRule;
        bool preload;
        static const QList<quint16> RightMasks;
        static const quint16 AllOnes;
    };
}

#endif // STDISPLAY_H
