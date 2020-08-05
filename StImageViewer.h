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
class QLabel;
class QTreeWidgetItem;
class QComboBox;

namespace St
{
    class ST_OBJECT_MEMORY;
    class ObjectTree;

    class ImageViewer : public QMainWindow
    {
        Q_OBJECT
    public:
        ImageViewer(QWidget* = 0);
        bool parse( const QString& path, bool collect = false );
        typedef QMap<QByteArray,quint16> Registers;
        void show(ST_OBJECT_MEMORY*, const Registers&);
        bool isNextStep() const { return d_nextStep; }
    signals:
        void sigClosing();

    public slots:
        void onContinue();
        void onNextStep();

    protected:
        void createObjectTable();
        void createClasses();
        void fillClasses();
        void createXref();
        void fillXref(quint16);
        void createInsts();
        void fillInsts(quint16);
        void createDetail();
        void closeEvent(QCloseEvent* event);
        void showDetail( quint16 );
        void createStack();
        QString detailText( quint16 );
        QString objectDetailText( quint16 );
        QString classDetailText( quint16 );
        QString methodDetailText( quint16 );
        QByteArrayList fieldList( quint16 cls, bool recursive = true );
        QString prettyValue(quint16);
        void syncClasses(quint16);
        void syncObjects(quint16);
        static QPair<QString,int> bytecodeText(const quint8* , int pc);
        void pushLocation(quint16);
        QPair<quint16,quint16> findSelectorAndClass(quint16 methodOop) const;
        void fillRegs(const Registers&);
        void syncAll(quint16, QObject* cause = 0, bool push = true );
        void fillStack( quint16 activeContext );
        void fillProcs(quint16 activeContext = 0);
    protected slots:
        void onObject( quint16 );
        void onClassesClicked();
        void onLink(const QUrl& );
        void onLink( const QString& );
        void onGoBack();
        void onGoForward();
        void onXrefClicked(QTreeWidgetItem*,int);
        void onInstsClicked(QTreeWidgetItem*,int);
        void onXrefDblClicked(QTreeWidgetItem*,int);
        void onInstsDblClicked(QTreeWidgetItem*,int);
        void onGotoAddr();
        void onFindText();
        void onFindNext();
        void onRegsClicked(QTreeWidgetItem*,int);
        void onStackClicked(QTreeWidgetItem*,int);
        void onProcess(int);
        void onCopyTree();
    private:
        friend class ObjectTree;
        ST_OBJECT_MEMORY* d_om;
        class Model;
        Model* d_mdl;
        ObjectTree* d_tree;
        QTreeWidget* d_classes;
        QTreeWidget* d_xref;
        QTreeWidget* d_insts;
        QTreeWidget* d_stack;
        QComboBox* d_procs;
        QLabel* d_xrefTitle;
        QLabel* d_instsTitle;
        QTextBrowser* d_detail;
        QList<quint16> d_backHisto; // d_backHisto.last() is the current location
        QList<quint16> d_forwardHisto;
        QString d_textToFind;
        bool d_pushBackLock, d_nextStep;
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
