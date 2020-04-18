#ifndef STVIRTUALMACHINE_H
#define STVIRTUALMACHINE_H

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
    class ObjectMemory2;
    class Interpreter;

    class VirtualMachine : public QWidget
    {
    public:
        explicit VirtualMachine(QWidget *parent = 0);
        void run( const QString& path );
    private:
        ObjectMemory2* d_om;
        Interpreter* d_ip;
    };
}

#endif // STVIRTUALMACHINE_H
