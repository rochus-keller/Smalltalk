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

class QTreeWidget;
class QTextBrowser;

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
    protected:
        void createObjectTable();
        void createClasses();
        void fillClasses();
        void createDetail();
        void closeEvent(QCloseEvent* event);
        void showDetail( quint16 );
        QString detailText( quint16 );
        QString objectDetailText( quint16 );
        QString classDetailText( quint16 );
        QString methodDetailText( quint16 );
        QByteArrayList fieldList( quint16 cls, bool recursive = true );
        QByteArray prettyValue(quint16);
        void syncClasses(quint16);
        void syncObjects(quint16);
        static QPair<QString,int> bytecodeText(const quint8* , int pc);
    protected slots:
        void onObject( quint16 );
        void onClassesClicked();
        void onLink(const QUrl& );
    private:
        friend class ObjectTree;
        ObjectMemory* d_om;
        class Model;
        Model* d_mdl;
        ObjectTree* d_tree;
        QTreeWidget* d_classes;
        QTextBrowser* d_detail;
    };

    class ObjectTree : public QTreeView
    {
        Q_OBJECT
    public:
        ObjectTree(QWidget* p = 0);
        void setModel(ImageViewer::Model*);
    signals:
        void sigObject( quint16 );
    protected slots:
        void onClicked(const QModelIndex&);
    private:
        ImageViewer::Model* d_mdl;
    };
}

#endif // STIMAGEVIEWER_H
