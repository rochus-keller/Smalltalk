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

#include "StObjectMemory2.h"
#include "StInterpreter.h"
#include "StVirtualMachine.h"
#include "StDisplay.h"
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QtDebug>
using namespace St;

VirtualMachine::VirtualMachine(QObject* parent) : QObject(parent)
{
    d_om = new ObjectMemory2(this);
    d_ip = new Interpreter(this);
}

void VirtualMachine::run(const QString& path)
{
    QFile in(path);
    if( !in.open(QIODevice::ReadOnly) )
    {
        QMessageBox::critical(Display::inst(),tr("Loading Smalltalk-80 Image"), tr("Cannot open file %1").arg(path) );
        return;
    }
    const bool res = d_om->readFrom(&in);
    if( !res )
    {
        QMessageBox::critical(Display::inst(),tr("Loading Smalltalk-80 Image"), tr("Incompatible format.") );
        return;
    }

    d_ip->setOm(d_om);
    d_ip->interpret();
}


int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationName("me@rochus-keller.ch");
    a.setOrganizationDomain("github.com/rochus-keller/Smalltalk");
    a.setApplicationName("Smalltalk 80 Virtual Machine");
    a.setApplicationVersion("0.6.1");
    a.setStyle("Fusion");

    VirtualMachine w;

    if( a.arguments().size() > 1 )
        w.run( a.arguments()[1] );
    else
    {
        const QString path = QFileDialog::getOpenFileName(Display::inst(),VirtualMachine::tr("Open Smalltalk-80 Image File"),
                                                          QString(), "VirtualImage *.image *.im" );
        if( path.isEmpty() )
            return 0;
        w.run(path);
    }
    return 0; // a.exec();
}
