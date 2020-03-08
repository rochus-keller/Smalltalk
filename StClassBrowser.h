#ifndef ST_CLASSBROWSER_H
#define ST_CLASSBROWSER_H

/*
* Copyright 2020 Rochus Keller <mailto:me@rochus-keller.ch>
*
* This file is part of the Smalltalk ClassBrowser application.
*
* The following is the license that applies to this copy of the
* application. For a license to use the application under conditions
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
#include <Smalltalk/StAstModel.h>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPlainTextEdit;
class QTextBrowser;

namespace St
{
class ClassBrowser : public QMainWindow
{
    Q_OBJECT

public:
    ClassBrowser(QWidget *parent = 0);
    ~ClassBrowser();

    bool parse(const QString& path );

protected:
    void closeEvent(QCloseEvent* event);
    void createClassList();
    void fillClassList();
    static void fillClassItem( QTreeWidgetItem* item, Ast::Class* );
    void createCatList();
    void fillCatList();
    void createClassInfo();
    void createMembers();
    void fillMembers();
    void setCurClass(Ast::Class*);
    void setCurMethod(Ast::Method*);
    void setCurVar(Ast::Variable*);
    static QString getClassSummary(Ast::Class*, bool elided = true);
    void createMethod();
    void fillMethod();
    void createHierarchy();
    void fillHierarchy();
    void fillHierarchy(QTreeWidgetItem* p, Ast::Class* );
    void createMessages();
    void fillMessages();
    void createPrimitives();
    void fillPrimitives();
    void createUse();
    void fillNamedUse(Ast::Named*);
    void fillPatternUse(const QByteArray&);
    void pushLocation();
    void syncLists(QWidget* besides = 0);
    void createVars();
    void fillVars();

protected slots:
    void onClassesClicked();
    void onCatsClicked();
    void onMembersClicked();
    void onHierarchyClicked();
    void onMessagesClicked();
    void onPrimitiveClicked();
    void onVarsClicked();
    void onUseClicked();
    void onGoBack();
    void onGoForward();
    void onLink( const QString& link );


private:
    class CodeViewer;
    Model* d_mdl;
    QString d_path;
    QTreeWidget* d_classes;
    QTreeWidget* d_cats;
    QLabel* d_class;
    QTreeWidget* d_members;
    QTreeWidget* d_hierarchy;
    QTreeWidget* d_messages;
    QTreeWidget* d_primitives;
    QTreeWidget* d_vars;
    QTreeWidget* d_use;
    QLabel* d_useTitle;
    CodeViewer* d_code;
    QTextBrowser* d_classInfo;
    QLabel* d_method;
    Ast::ClassRef d_curClass;
    Ast::MethodRef d_curMethod;
    Ast::VarRef d_curVar;
    typedef QPair<Ast::ClassRef,Ast::MethodRef> Location;
    QList<Location> d_backHisto; // d_backHisto.last() is the current location
    QList<Location> d_forwardHisto;
    bool d_pushBackLock;
};
}

#endif // ST_CLASSBROWSER_H
