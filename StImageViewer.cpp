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

#include "StImageViewer.h"
#include "StObjectMemory.h"
#include <QApplication>
#include <QFileDialog>
using namespace St;

ImageViewer::ImageViewer()
{

}

bool ImageViewer::parse(const QString& path)
{
    QFile in(path);
    if( !in.open(QIODevice::ReadOnly) )
        return false;
    ObjectMemory r;
    return r.readFrom(&in);
}


int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationName("me@rochus-keller.ch");
    a.setOrganizationDomain("github.com/rochus-keller/Smalltalk");
    a.setApplicationName("Smalltalk 80 Image Viewer");
    a.setApplicationVersion("0.1");
    a.setStyle("Fusion");

    ImageViewer w;
    w.show();

    if( a.arguments().size() > 1 )
        w.parse( a.arguments()[1] );
    else
    {
        const QString path = QFileDialog::getOpenFileName(&w,ImageViewer::tr("Open Smalltalk-80 Image File"),
                                                          QString(), "*.image" );
        if( path.isEmpty() )
            return 0;
        w.parse(path);
    }
    return a.exec();
}
