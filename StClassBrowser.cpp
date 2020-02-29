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

#include "StClassBrowser.h"
#include "StAstModel.h"
#include "StParser.h"
#include "StHighlighter.h"
#include <QElapsedTimer>
#include <QFile>
#include <QtDebug>
#include <QApplication>
#include <QSettings>
#include <QDesktopWidget>
#include <QCloseEvent>
#include <QDockWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTextBrowser>
#include <QToolTip>
#include <QHeaderView>
#include <QFileDialog>
using namespace St;

class ClassBrowser::CodeViewer : public QPlainTextEdit
{
public:
    ClassBrowser* d_that;

    CodeViewer( ClassBrowser* p ):QPlainTextEdit(p),d_that(p)
    {
        setTabStopWidth( fontMetrics().width("nnn"));
        setLineWrapMode( QPlainTextEdit::NoWrap );
        setReadOnly(true);
        new Highlighter(document());
    }

    void markCode( QTextCursor cur, const QPoint& pos, bool click )
    {
        Ast::Expression* e = d_that->d_curMethod->findByPos( cur.position() + d_that->d_curMethod->d_pos );
        QList<QTextEdit::ExtraSelection> esl;
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground( QColor(Qt::yellow) );
        if( e )
        {
            switch( e->getTag() )
            {
            case Ast::Thing::T_Ident:
                {
                    Ast::Ident* id = static_cast<Ast::Ident*>(e);
                    if( id->d_keyword )
                        break;
                    if( click )
                        d_that->fillUse(id->d_resolved);
                    cur.setPosition( e->d_pos - d_that->d_curMethod->d_pos );
                    cur.setPosition( cur.position() + e->getLen(), QTextCursor::KeepAnchor );
                    sel.cursor = cur;
                    esl.append(sel);
                    const QList<Ast::Ident*>& ids = d_that->d_mdl->getIxref().value(id->d_resolved);
                    for( int i = 0; i < ids.size(); i++ )
                    {
                        if( ids[i]->d_pos >= d_that->d_curMethod->d_pos &&
                                ids[i]->d_pos < d_that->d_curMethod->d_endPos )
                        {
                            cur.setPosition( ids[i]->d_pos - d_that->d_curMethod->d_pos );
                            cur.setPosition( cur.position() + ids[i]->getLen(), QTextCursor::KeepAnchor );
                            sel.cursor = cur;
                            esl.append(sel);
                        }
                    }
                    if( id->d_resolved && id->d_resolved->d_pos >= d_that->d_curMethod->d_pos &&
                            id->d_resolved->d_pos < d_that->d_curMethod->d_endPos )
                    {
                        cur.setPosition( id->d_resolved->d_pos - d_that->d_curMethod->d_pos );
                        cur.setPosition( cur.position() + id->d_resolved->getLen(), QTextCursor::KeepAnchor );
                        sel.cursor = cur;
                        esl.append(sel);
                    }
                    QString title;
                    switch( id->d_use )
                    {
                    case Ast::Ident::Declaration:
                        title = "Declaration";
                        break;
                    case Ast::Ident::AssigTarget:
                        title = "Assignment target";
                        break;
                    case Ast::Ident::MsgReceiver:
                        title = "Message target";
                        break;
                    case Ast::Ident::Rhs:
                        title = "Value source";
                        break;
                    default:
                        title = "Unknown use";
                        break;
                    }
                    QString text = "unresolved";
                    if( id->d_resolved )
                    {
                        switch( id->d_resolved->getTag() )
                        {
                        case Ast::Thing::T_Method:
                            text = QString("Method <i>%1</i>").arg(id->d_resolved->d_name.constData());
                            break;
                        case Ast::Thing::T_Variable:
                            {
                                QString kind;
                                QString declared;
                                Ast::Variable* v = static_cast<Ast::Variable*>(id->d_resolved);
                                switch( v->d_kind )
                                {
                                case Ast::Variable::InstanceLevel:
                                    kind = "Instance variable";
                                    if( v->d_owner == d_that->d_curMethod->getClass() )
                                        declared = " declared in this class";
                                    else
                                        declared = QString(" declared in class <i>%1</i>").
                                                arg(v->d_owner->d_name.constData());
                                    break;
                                case Ast::Variable::ClassLevel:
                                    kind = "Class variable";
                                    if( v->d_owner == d_that->d_curMethod->getClass() )
                                        declared = " declared in this class";
                                    else
                                        declared = QString(" declared in class <i>%1</i>").
                                                arg(v->d_owner->d_name.constData());
                                    break;
                                case Ast::Variable::Argument:
                                    kind = "Argument";
                                    break;
                                case Ast::Variable::Temporary:
                                    kind = "Temporary variable";
                                    break;
                                case Ast::Variable::Global:
                                    kind = "Global variable";
                                    break;
                                }

                                text = QString("%1 <i>%2</i>%3").arg(kind)
                                        .arg(id->d_resolved->d_name.constData() ).arg(declared);
                            }
                            break;
                        case Ast::Thing::T_Class:
                            text = QString("Class <i>%1</i>").arg(id->d_resolved->d_name.constData());
                            break;
                        }
                    }
                    if( click )
                        QToolTip::showText(pos,
                                       tr("<html><b>%1:</b><p>%2</p></html>").arg(title)
                                       .arg(text),this);
                }
                break;
            case Ast::Thing::T_MsgSend:
                {
                    Ast::MsgSend* s = static_cast<Ast::MsgSend*>(e);
                    for( int i = 0; i < s->d_pattern.size(); i++ )
                    {
                        cur.setPosition( s->d_pattern[i].second - d_that->d_curMethod->d_pos );
                        cur.setPosition( cur.position() + s->d_pattern[i].first.size(), QTextCursor::KeepAnchor );
                        sel.cursor = cur;
                        esl.append(sel);
                    }
                    if( click )
                        QToolTip::showText(pos,
                                       tr("<html><b>Message:</b><p>%1</p></html>")
                                       .arg(s->prettyName().constData()),this);

                }
                break;
            }

        }
        setExtraSelections(esl);
    }

    void mousePressEvent(QMouseEvent* event)
    {
        if( !d_that->d_curMethod.isNull() )
        {
            QTextCursor cur = cursorForPosition(event->pos());
            markCode( cur, event->globalPos(), true );
#if 0
            Ast::Expression* e = d_that->d_curMethod->findByPos( cur.position() + d_that->d_curMethod->d_pos );
            QList<QTextEdit::ExtraSelection> esl;
            QTextEdit::ExtraSelection sel;
            sel.format.setBackground( QColor(Qt::yellow) );
            if( e )
            {
                switch( e->getTag() )
                {
                case Ast::Thing::T_Ident:
                    {
                        Ast::Ident* id = static_cast<Ast::Ident*>(e);
                        if( id->d_keyword )
                            break;
                        d_that->fillUse(id->d_resolved);
                        cur.setPosition( e->d_pos - d_that->d_curMethod->d_pos );
                        cur.setPosition( cur.position() + e->getLen(), QTextCursor::KeepAnchor );
                        sel.cursor = cur;
                        esl.append(sel);
                        const QList<Ast::Ident*>& ids = d_that->d_mdl->getIxref().value(id->d_resolved);
                        for( int i = 0; i < ids.size(); i++ )
                        {
                            if( ids[i]->d_pos >= d_that->d_curMethod->d_pos &&
                                    ids[i]->d_pos < d_that->d_curMethod->d_endPos )
                            {
                                cur.setPosition( ids[i]->d_pos - d_that->d_curMethod->d_pos );
                                cur.setPosition( cur.position() + ids[i]->getLen(), QTextCursor::KeepAnchor );
                                sel.cursor = cur;
                                esl.append(sel);
                            }
                        }
                        if( id->d_resolved && id->d_resolved->d_pos >= d_that->d_curMethod->d_pos &&
                                id->d_resolved->d_pos < d_that->d_curMethod->d_endPos )
                        {
                            cur.setPosition( id->d_resolved->d_pos - d_that->d_curMethod->d_pos );
                            cur.setPosition( cur.position() + id->d_resolved->getLen(), QTextCursor::KeepAnchor );
                            sel.cursor = cur;
                            esl.append(sel);
                        }
                        QString title;
                        switch( id->d_use )
                        {
                        case Ast::Ident::Declaration:
                            title = "Declaration";
                            break;
                        case Ast::Ident::AssigTarget:
                            title = "Assignment target";
                            break;
                        case Ast::Ident::MsgReceiver:
                            title = "Message target";
                            break;
                        case Ast::Ident::Rhs:
                            title = "Value source";
                            break;
                        default:
                            title = "Unknown use";
                            break;
                        }
                        QString text = "unresolved";
                        if( id->d_resolved )
                        {
                            switch( id->d_resolved->getTag() )
                            {
                            case Ast::Thing::T_Method:
                                text = QString("Method <i>%1</i>").arg(id->d_resolved->d_name.constData());
                                break;
                            case Ast::Thing::T_Variable:
                                {
                                    QString kind;
                                    QString declared;
                                    Ast::Variable* v = static_cast<Ast::Variable*>(id->d_resolved);
                                    switch( v->d_kind )
                                    {
                                    case Ast::Variable::InstanceLevel:
                                        kind = "Instance variable";
                                        if( v->d_owner == d_that->d_curMethod->getClass() )
                                            declared = " declared in this class";
                                        else
                                            declared = QString(" declared in class <i>%1</i>").
                                                    arg(v->d_owner->d_name.constData());
                                        break;
                                    case Ast::Variable::ClassLevel:
                                        kind = "Class variable";
                                        if( v->d_owner == d_that->d_curMethod->getClass() )
                                            declared = " declared in this class";
                                        else
                                            declared = QString(" declared in class <i>%1</i>").
                                                    arg(v->d_owner->d_name.constData());
                                        break;
                                    case Ast::Variable::Argument:
                                        kind = "Argument";
                                        break;
                                    case Ast::Variable::Temporary:
                                        kind = "Temporary variable";
                                        break;
                                    case Ast::Variable::Global:
                                        kind = "Global variable";
                                        break;
                                    }

                                    text = QString("%1 <i>%2</i>%3").arg(kind)
                                            .arg(id->d_resolved->d_name.constData() ).arg(declared);
                                }
                                break;
                            case Ast::Thing::T_Class:
                                text = QString("Class <i>%1</i>").arg(id->d_resolved->d_name.constData());
                                break;
                            }
                        }
                        QToolTip::showText(event->globalPos(),
                                           tr("<html><b>%1:</b><p>%2</p></html>").arg(title)
                                           .arg(text),this);
                    }
                    break;
                case Ast::Thing::T_MsgSend:
                    {
                        Ast::MsgSend* s = static_cast<Ast::MsgSend*>(e);
                        for( int i = 0; i < s->d_pattern.size(); i++ )
                        {
                            cur.setPosition( s->d_pattern[i].second - d_that->d_curMethod->d_pos );
                            cur.setPosition( cur.position() + s->d_pattern[i].first.size(), QTextCursor::KeepAnchor );
                            sel.cursor = cur;
                            esl.append(sel);
                        }
                        QToolTip::showText(event->globalPos(),
                                           tr("<html><b>Message:</b><p>%1</p></html>")
                                           .arg(s->prettyName().constData()),this);

                    }
                    break;
                }

            }
            setExtraSelections(esl);
#endif
        }
        QPlainTextEdit::mousePressEvent(event);
    }
};

ClassBrowser::ClassBrowser(QWidget *parent)
    : QMainWindow(parent)
{
    d_mdl = new Model(this);

    setDockNestingEnabled(true);
    setCorner( Qt::BottomRightCorner, Qt::RightDockWidgetArea );
    setCorner( Qt::BottomLeftCorner, Qt::LeftDockWidgetArea );
    setCorner( Qt::TopRightCorner, Qt::RightDockWidgetArea );
    setCorner( Qt::TopLeftCorner, Qt::LeftDockWidgetArea );

    createClassList();
    createCatList();
    createMembers();
    createClassInfo();
    createMethod();
    createHierarchy();
    createMessages();
    createPrimitives();
    createUse();

    QSettings s;

    const QRect screen = QApplication::desktop()->screenGeometry();
    resize( screen.width() - 20, screen.height() - 30 ); // so that restoreState works
    if( s.value("Fullscreen").toBool() )
        showFullScreen();
    else
        showMaximized();

    const QVariant state = s.value( "DockState" );
    if( !state.isNull() )
        restoreState( state.toByteArray() );

    setWindowTitle(tr("%1 %2").arg(qApp->applicationName()).arg(qApp->applicationVersion()));

}

ClassBrowser::~ClassBrowser()
{

}

bool ClassBrowser::parse(const QString& path)
{
    QFile in(path);
    if( !in.open(QIODevice::ReadOnly) )
        return false;

    Parser::convertFile(&in,"out.txt");
    in.reset();
    d_path = path;
    QElapsedTimer time;
    time.start();
    const bool res = d_mdl->parse(&in);
    qDebug() << "parsed in" << time.elapsed() << "ms";

    fillClassList();
    fillCatList();
    fillHierarchy();
    fillMessages();
    fillPrimitives();
    return res;
}

void ClassBrowser::closeEvent(QCloseEvent* event)
{
    QSettings s;
    s.setValue( "DockState", saveState() );
    event->setAccepted(true);
}

void ClassBrowser::createClassList()
{
    QDockWidget* dock = new QDockWidget( tr("Classes"), this );
    dock->setObjectName("Classes");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable );
    d_classes = new QTreeWidget(dock);
    d_classes->setHeaderHidden(true);
    d_classes->setAlternatingRowColors(true);
    d_classes->setRootIsDecorated(false);
    dock->setWidget(d_classes);
    addDockWidget( Qt::LeftDockWidgetArea, dock );
    connect( d_classes, SIGNAL(itemClicked(QTreeWidgetItem*,int)), this, SLOT(onClassesClicked()) );
}

void ClassBrowser::fillClassList()
{
    d_classes->clear();

    Model::Classes::const_iterator i;
    for( i = d_mdl->getClasses().begin(); i != d_mdl->getClasses().end(); ++i )
    {
        QTreeWidgetItem* item = new QTreeWidgetItem(d_classes);
        fillClassItem(item, i.value().data() );
    }
}

void ClassBrowser::fillClassItem(QTreeWidgetItem* item, Ast::Class* c)
{
    item->setText(0,c->d_name);
    item->setToolTip(0, getClassSummary(c) );
    item->setData(0,Qt::UserRole,QVariant::fromValue(Ast::ClassRef(c)) );
}

void ClassBrowser::createCatList()
{
    QDockWidget* dock = new QDockWidget( tr("Class Categories"), this );
    dock->setObjectName("Categories");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable );
    d_cats = new QTreeWidget(dock);
    d_cats->setHeaderHidden(true);
    d_cats->setAlternatingRowColors(true);
    dock->setWidget(d_cats);
    addDockWidget( Qt::LeftDockWidgetArea, dock );
    connect( d_cats, SIGNAL(itemClicked(QTreeWidgetItem*,int)), this, SLOT(onCatsClicked()) );
}

static bool sortClasses( Ast::Class* lhs, Ast::Class* rhs )
{
    return lhs->d_name < rhs->d_name;
}

void ClassBrowser::fillCatList()
{
    d_cats->clear();
    Model::ClassCats::const_iterator i;
    for( i = d_mdl->getCats().begin(); i != d_mdl->getCats().end(); ++i )
    {
        QTreeWidgetItem* item = new QTreeWidgetItem(d_cats);
        item->setText(0,i.key().constData());
        QList<Ast::Class*> l = i.value();
        std::sort( l.begin(), l.end(), sortClasses );
        foreach( Ast::Class* c, l )
        {
            QTreeWidgetItem* sub = new QTreeWidgetItem(item);
            fillClassItem(sub, c );
        }
    }
}

void ClassBrowser::createClassInfo()
{
    QDockWidget* dock = new QDockWidget( tr("Class Info"), this );
    dock->setObjectName("ClassInfo");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable );
    d_classInfo = new QTextBrowser(this);
    dock->setWidget(d_classInfo);
    addDockWidget( Qt::RightDockWidgetArea, dock );
}

void ClassBrowser::createMembers()
{
    QDockWidget* dock = new QDockWidget( tr("Class Members"), this );
    dock->setObjectName("ClassMembers");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable );
    QWidget* pane = new QWidget(dock);
    QVBoxLayout* vbox = new QVBoxLayout(pane);
    vbox->setMargin(0);
    vbox->setSpacing(0);
    d_class = new QLabel(pane);
    d_class->setMargin(2);
    d_class->setWordWrap(true);
    vbox->addWidget(d_class);
    d_members = new QTreeWidget(pane);
    d_members->setHeaderHidden(true);
    d_members->setAlternatingRowColors(true);
    vbox->addWidget(d_members);
    dock->setWidget(pane);
    addDockWidget( Qt::RightDockWidgetArea, dock );
    connect( d_members, SIGNAL(itemClicked(QTreeWidgetItem*,int)), this, SLOT(onMembersClicked()) );
}

static QByteArray superClasses( Ast::Class* c )
{
    if( c && c->getSuper() )
        return c->getSuper()->d_name + " " + superClasses( c->getSuper() );
    else
        return QByteArray();
}

void ClassBrowser::fillMembers()
{
    d_class->clear();
    d_members->clear();
    d_classInfo->clear();

    if( d_curClass.isNull() )
        return;

    QFont bold = d_members->font();
    bold.setBold(true);

    d_class->setText(QString("<b><i>%1</i></b> <i>%2</i>").arg(d_curClass->d_name.constData() )
                     .arg( superClasses(d_curClass.data()).constData() ));
    d_class->setToolTip( getClassSummary(d_curClass.data()) );

    d_classInfo->setHtml( getClassSummary(d_curClass.data(), false) );

    if( !d_curClass->d_vars.isEmpty() )
    {
        QTreeWidgetItem* fields = new QTreeWidgetItem(d_members);
        fields->setFont(0,bold);
        fields->setText(0,tr("fields:"));
        for( int i = 0; i < d_curClass->d_vars.size(); i++ )
        {
            QTreeWidgetItem* sub1 = new QTreeWidgetItem(fields);
            sub1->setText(0, ( d_curClass->d_vars[i]->d_kind == Ast::Variable::ClassLevel ? "[c] " : "" )
                          + d_curClass->d_vars[i]->d_name );
            sub1->setData(0,Qt::UserRole, QVariant::fromValue(d_curClass->d_vars[i]));
        }
    }

    typedef QMap<QByteArray,QMap<QByteArray,Ast::Method*> > Cats;
    Cats cats;

    for( int i = 0; i < d_curClass->d_methods.size(); i++ )
        cats[d_curClass->d_methods[i]->d_category][d_curClass->d_methods[i]->prettyName()] = d_curClass->d_methods[i].data();

    Cats::const_iterator j;
    for( j = cats.begin(); j != cats.end(); ++ j )
    {
        QTreeWidgetItem* cat = new QTreeWidgetItem(d_members);
        cat->setFont(0,bold);
        if( j.key().isEmpty() )
            cat->setText(0,"<uncategorized>:");
        else
            cat->setText(0,j.key() + ":");

        QMap<QByteArray,Ast::Method*>::const_iterator k;
        for( k = j.value().begin(); k != j.value().end(); ++k )
        {
            QTreeWidgetItem* mem = new QTreeWidgetItem(cat);
            if( k.value()->d_classLevel )
                mem->setText(0,QString("[c] %1").arg(k.key().constData()));
            else
                mem->setText(0, k.key());
            mem->setData(0,Qt::UserRole,QVariant::fromValue(Ast::MethodRef(k.value())));
        }
    }
    d_members->expandAll();
}

void ClassBrowser::setCurClass(Ast::Class* c)
{
    d_curClass = c;
    fillMembers();
}

void ClassBrowser::setCurMethod(Ast::Method* m)
{
    if( m != 0 )
        d_curClass = m->getClass();
    d_curMethod = m;
    fillMethod();
}

QString ClassBrowser::getClassSummary(Ast::Class* c, bool elided)
{
    const int max = 1000;
    QString cmt1 = QString::fromUtf8(c->d_comment);
    if( elided && cmt1.size() > max )
        cmt1 = cmt1.left(max) + "...";
    cmt1.replace('\n',"<br>");
    QString cmt2 = QString::fromUtf8(c->d_classComment);
    if( elided && cmt2.size() > max )
        cmt2 = cmt2.left(max) + "...";
    cmt2.replace('\n',"<br>");
    return tr("<html><b>Class:</b> %1<br><b>Category:</b> %2<br><b>Super:</b> %3<br>"
                           "%6 fields, %7 methods, %8 subclasses"
                           "<p>%4</p><p>%5</p></html>")
                     .arg(c->d_name.constData()).arg(c->d_category.constData())
                     .arg(c->d_superName.constData() ).arg( cmt2 ).arg( cmt1 )
            .arg(c->d_vars.size()).arg(c->d_methods.size()).arg(c->d_subs.size());
}

void ClassBrowser::createMethod()
{
    QDockWidget* dock = new QDockWidget( tr("Method"), this );
    dock->setObjectName("Method");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable );
    QWidget* pane = new QWidget(dock);
    QVBoxLayout* vbox = new QVBoxLayout(pane);
    vbox->setMargin(0);
    vbox->setSpacing(0);
    d_method = new QLabel(pane);
    d_method->setMargin(2);
    d_method->setWordWrap(true);
    vbox->addWidget(d_method);
    d_code = new CodeViewer(this);
    vbox->addWidget(d_code);
    dock->setWidget(pane);
    addDockWidget( Qt::BottomDockWidgetArea, dock );
}

void ClassBrowser::fillMethod()
{
    d_code->clear();
    d_method->clear();

    if( d_curMethod.isNull() )
        return;

    QFile in(d_path);
    if( !in.open(QIODevice::ReadOnly) )
        return;

    d_method->setText(QString("%3<b><i>%1 %2</i></b>").arg(d_curMethod->d_owner->d_name.constData() )
                      .arg(QString(d_curMethod->prettyName()).toHtmlEscaped() ).arg( d_curMethod->d_classLevel ? "[c] " : "" ) );

    in.seek( d_curMethod->d_pos );
    QString str = QString::fromUtf8( in.read( d_curMethod->d_endPos - d_curMethod->d_pos + 1 ) );
    str.replace("!!", "!" );
#ifdef _ST_LEXER_SUPPORT_UNDERSCORE_IN_IDENTS_
    str.replace(" _ ", " ← ");
    str.replace(" _\r", " ←\r");
#else
    str.replace("_", "←");
#endif
    str.replace("^","↑");
    d_code->setPlainText( str );
}

void ClassBrowser::createHierarchy()
{
    QDockWidget* dock = new QDockWidget( tr("Inheritance Tree"), this );
    dock->setObjectName("Inheritance");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable );
    d_hierarchy = new QTreeWidget(dock);
    d_hierarchy->setHeaderHidden(true);
    d_hierarchy->setAlternatingRowColors(true);
    dock->setWidget(d_hierarchy);
    addDockWidget( Qt::BottomDockWidgetArea, dock );
    connect( d_hierarchy, SIGNAL(itemClicked(QTreeWidgetItem*,int)), this, SLOT(onHierarchyClicked()) );
}

void ClassBrowser::fillHierarchy()
{
    d_hierarchy->clear();

    Model::Classes::const_iterator i = d_mdl->getClasses().find("Object");
    if( i == d_mdl->getClasses().end() )
        return;

    QTreeWidgetItem* item = new QTreeWidgetItem(d_hierarchy);
    fillClassItem(item, i.value().data() );
    fillHierarchy(item,i.value().data());
    d_hierarchy->expandAll();
}

void ClassBrowser::fillHierarchy(QTreeWidgetItem* p, Ast::Class* c)
{
    for( int i = 0; i < c->d_subs.size(); i++ )
    {
        QTreeWidgetItem* item = new QTreeWidgetItem(p);
        fillClassItem(item, c->d_subs[i].data() );
        fillHierarchy(item, c->d_subs[i].data() );
    }
}

void ClassBrowser::createMessages()
{
    QDockWidget* dock = new QDockWidget( tr("Message Patterns"), this );
    dock->setObjectName("Messages");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable );
    d_messages = new QTreeWidget(dock);
    d_messages->setHeaderHidden(true);
    d_messages->setAlternatingRowColors(true);
    dock->setWidget(d_messages);
    addDockWidget( Qt::RightDockWidgetArea, dock );
    connect( d_messages, SIGNAL(itemClicked(QTreeWidgetItem*,int)), this, SLOT(onMessagesClicked()) );
}

void ClassBrowser::fillMessages()
{
    d_messages->clear();
    const Model::MethodXref& mx = d_mdl->getMxref();
    Model::MethodXref::const_iterator i;
    for( i = mx.begin(); i != mx.end(); ++i )
    {
        QTreeWidgetItem* item = new QTreeWidgetItem( d_messages );
        item->setText(0, i.key() );
        item->setToolTip(0,item->text(0));
        foreach( Ast::Method* m, i.value() )
        {
            QTreeWidgetItem* sub = new QTreeWidgetItem( item );
            sub->setText(0, m->d_owner->d_name );
            sub->setToolTip(0, "category: " + m->d_category );
            sub->setData(0,Qt::UserRole, QVariant::fromValue( Ast::MethodRef(m) ) );
        }
    }
    d_messages->sortByColumn(0,Qt::AscendingOrder);
}

void ClassBrowser::createPrimitives()
{
    QDockWidget* dock = new QDockWidget( tr("Primitives"), this );
    dock->setObjectName("Primitives");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable );
    d_primitives = new QTreeWidget(dock);
    d_primitives->setHeaderHidden(true);
    d_primitives->setAlternatingRowColors(true);
    dock->setWidget(d_primitives);
    addDockWidget( Qt::RightDockWidgetArea, dock );
    connect( d_primitives, SIGNAL(itemClicked(QTreeWidgetItem*,int)), this, SLOT(onPrimitiveClicked()) );
}

void ClassBrowser::fillPrimitives()
{
    d_primitives->clear();
    const Model::PrimitiveXref& px = d_mdl->getPxref();
    Model::PrimitiveXref::const_iterator i;
    for( i = px.begin(); i != px.end(); ++i )
    {
        QTreeWidgetItem* item = new QTreeWidgetItem( d_primitives );
        item->setText(0, QString("%1").arg(i.key(),3,10,QChar('0')) );
        item->setToolTip(0,item->text(0));
        foreach( Ast::Method* m, i.value() )
        {
            QTreeWidgetItem* sub = new QTreeWidgetItem( item );
            sub->setText(0, m->d_owner->d_name + " " + m->prettyName() );
            sub->setToolTip(0, sub->text(0) );
            sub->setData(0,Qt::UserRole, QVariant::fromValue( Ast::MethodRef(m) ) );
        }
    }
    d_primitives->sortByColumn(0,Qt::AscendingOrder);
    d_primitives->expandAll();
}

void ClassBrowser::createUse()
{
    QDockWidget* dock = new QDockWidget( tr("Where used"), this );
    dock->setObjectName("Use");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable );
    QWidget* pane = new QWidget(dock);
    QVBoxLayout* vbox = new QVBoxLayout(pane);
    vbox->setMargin(0);
    vbox->setSpacing(0);
    d_useTitle = new QLabel(pane);
    d_useTitle->setMargin(2);
    d_useTitle->setWordWrap(true);
    vbox->addWidget(d_useTitle);
    d_use = new QTreeWidget(pane);
    d_use->setHeaderHidden(true);
    d_use->setAlternatingRowColors(true);
    d_use->setRootIsDecorated(false);
    d_use->setColumnCount(2); // Class/Method, Count
    //d_use->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    //d_use->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    vbox->addWidget(d_use);
    dock->setWidget(pane);
    addDockWidget( Qt::RightDockWidgetArea, dock );
    connect( d_use, SIGNAL(itemClicked(QTreeWidgetItem*,int)), this, SLOT(onUseClicked()) );
}

void ClassBrowser::fillUse(Ast::Named* n)
{
    d_use->clear();
    d_useTitle->clear();

    if( n == 0 )
        return;

    switch( n->getTag() )
    {
    case Ast::Thing::T_Class:
        d_useTitle->setText(QString("Class <b><i>%1</i></b>").arg( n->d_name.constData() ) );
        break;
    case Ast::Thing::T_Variable:
        {
            Ast::Variable* v = static_cast<Ast::Variable*>(n);
            QString what;
            switch( v->d_kind )
            {
            case Ast::Variable::InstanceLevel:
                what = v->d_owner->getClass()->d_name;
                break;
            case Ast::Variable::ClassLevel:
                what = "[c] " + v->d_owner->getClass()->d_name;
                break;
            case Ast::Variable::Global:
                what = "Global";
                break;
            case Ast::Variable::Argument:
            case Ast::Variable::Temporary:
                what = v->d_owner->getClass()->d_name + " " + v->d_owner->getMethod()->prettyName() + " (local)";
                break;
            }

            d_useTitle->setText(QString("%1 <b><i>%2</i></b>").arg( what ).arg( n->d_name.constData() ) );
        }
        break;
    default:
        d_useTitle->setText(QString("<b><i>%1</i></b>").arg( n->d_name.constData() ) );
        break;
    }

    const QList<Ast::Ident*>& ids = d_mdl->getIxref().value(n);
    Ast::Method* m = 0;
    int i = 0;
    QFont bold = d_use->font();
    bold.setBold(true);
    while( i < ids.size() )
    {
        if( ids[i]->d_inMethod != m )
        {
            int count = 0;
            Ast::Ident* id = ids[i];
            bool isAssig = false;
            m = id->d_inMethod;
            while( i < ids.size() && ids[i]->d_inMethod == m )
            {
                if( ids[i]->d_use == Ast::Ident::AssigTarget )
                    isAssig = true;
                count++;
                i++;
            }
            QTreeWidgetItem* item = new QTreeWidgetItem( d_use );
            item->setText(0, QString::number(count) );
            item->setData(0,Qt::UserRole,id->d_pos);
            if( isAssig )
                item->setFont(0,bold);
            item->setText(1, QString("%1 %2").arg(id->d_inMethod->getClass()->d_name.constData() ).
                          arg(id->d_inMethod->d_name.constData() ) );
            item->setData(1,Qt::UserRole, QVariant::fromValue( Ast::MethodRef(id->d_inMethod) ) );
            item->setToolTip(1,item->text(1));
        }else
            i++;
    }
    d_use->sortByColumn(1,Qt::AscendingOrder);
    d_use->resizeColumnToContents(0);
}

void ClassBrowser::onClassesClicked()
{
    QTreeWidgetItem* item = d_classes->currentItem();
    if( item == 0 )
        return;
    setCurClass( item->data(0,Qt::UserRole).value<Ast::ClassRef>().data() );
}

void ClassBrowser::onCatsClicked()
{
    QTreeWidgetItem* item = d_cats->currentItem();
    if( item == 0 || !item->data(0,Qt::UserRole).canConvert<Ast::ClassRef>() )
        return;
    setCurClass( item->data(0,Qt::UserRole).value<Ast::ClassRef>().data() );
}

void ClassBrowser::onMembersClicked()
{
    QTreeWidgetItem* item = d_members->currentItem();
    if( item == 0 || !item->data(0,Qt::UserRole).canConvert<Ast::MethodRef>() )
        return;
    setCurMethod( item->data(0,Qt::UserRole).value<Ast::MethodRef>().data() );
}

void ClassBrowser::onHierarchyClicked()
{
    QTreeWidgetItem* item = d_hierarchy->currentItem();
    if( item == 0 || !item->data(0,Qt::UserRole).canConvert<Ast::ClassRef>() )
        return;
    setCurClass( item->data(0,Qt::UserRole).value<Ast::ClassRef>().data() );
}

void ClassBrowser::onMessagesClicked()
{
    QTreeWidgetItem* item = d_messages->currentItem();
    if( item == 0 || !item->data(0,Qt::UserRole).canConvert<Ast::MethodRef>() )
        return;
    setCurMethod( item->data(0,Qt::UserRole).value<Ast::MethodRef>().data() );
    if( !d_curMethod.isNull() )
        setCurClass( d_curMethod->getClass() );
}

void ClassBrowser::onPrimitiveClicked()
{
    QTreeWidgetItem* item = d_primitives->currentItem();
    if( item == 0 || !item->data(0,Qt::UserRole).canConvert<Ast::MethodRef>() )
        return;
    setCurMethod( item->data(0,Qt::UserRole).value<Ast::MethodRef>().data() );
    if( !d_curMethod.isNull() )
        setCurClass( d_curMethod->getClass() );
}

void ClassBrowser::onUseClicked()
{
    QTreeWidgetItem* item = d_use->currentItem();
    if( item == 0 || !item->data(1,Qt::UserRole).canConvert<Ast::MethodRef>() )
        return;
    setCurMethod( item->data(1,Qt::UserRole).value<Ast::MethodRef>().data() );
    if( !d_curMethod.isNull() )
    {
        setCurClass( d_curMethod->getClass() );
        QTextCursor cur = d_code->textCursor();
        cur.setPosition( item->data(0,Qt::UserRole).toUInt() - d_curMethod->d_pos );
        d_code->markCode( cur, QPoint(), false );
    }
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationName("me@rochus-keller.ch");
    a.setOrganizationDomain("github.com/rochus-keller/Smalltalk");
    a.setApplicationName("Smalltalk 80 Class Browser");
    a.setApplicationVersion("0.4");
    a.setStyle("Fusion");

    ClassBrowser w;
    w.show();

    if( a.arguments().size() > 1 )
        w.parse( a.arguments()[1] );
    else
    {
        const QString path = QFileDialog::getOpenFileName(&w,ClassBrowser::tr("Open Smalltalk-80 Sources File"),
                                                          QString(), "*.sources" );
        if( path.isEmpty() )
            return 0;
        w.parse(path);
    }
    return a.exec();
}
