#ifndef STIMAGEVIEWER_H
#define STIMAGEVIEWER_H

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

#include <QMainWindow>
#include <QTreeView>

namespace St
{
    class ObjectMemory;
    class ObjectTree;

    class ImageViewer : public QMainWindow
    {
        Q_OBJECT
    public:
        ImageViewer();
        bool parse( const QString& path );
    protected slots:
        void onObject( quint16 );
    private:
        friend class ObjectTree;
        ObjectMemory* d_om;
        class Model;
        Model* d_mdl;
        ObjectTree* d_tree;
    };

    class ObjectTree : public QTreeView
    {
        Q_OBJECT
    public:
        ObjectTree(QWidget* p = 0);
        void setModel(ImageViewer::Model*);
        void expandTopLevel();
    signals:
        void sigObject( quint16 );
    protected slots:
        void onDoubleClicked(const QModelIndex&);
    private:
        ImageViewer::Model* d_mdl;
    };
}

#endif // STIMAGEVIEWER_H
