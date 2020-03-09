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
#include <QShortcut>
using namespace St;

class ClassBrowser::CodeViewer : public QPlainTextEdit
{
public:
    ClassBrowser* d_that;
    QString d_code;
    QMap<quint32,quint16> d_offCorr; // filepos->offCorr

    CodeViewer( ClassBrowser* p ):QPlainTextEdit(p),d_that(p)
    {
        setTabStopWidth( fontMetrics().width("nnn"));
        setLineWrapMode( QPlainTextEdit::NoWrap );
        setReadOnly(true);
        new Highlighter(document());
    }

    void setCode( QString str )
    {
        d_offCorr.clear();
        d_offCorr[0] = 0;
        int pos = str.indexOf("!!");
        int off = 0;
        while( pos != -1 )
        {
            off++;
            d_offCorr[pos+2] = off;
            pos = str.indexOf("!!", pos+2);
        }
        d_offCorr[str.size()] = off;
        str.replace("!!", "!" );
    #ifdef _ST_LEXER_SUPPORT_UNDERSCORE_IN_IDENTS_
        str.replace(" _ ", " ← ");
        str.replace(" _\r", " ←\r");
    #else
        str.replace("_", "←");
    #endif
        str.replace("^","↑");
        d_code = str;
        setPlainText(str);
    }

    int offCorr( quint32 methodPos ) const
    {
        QMap<quint32,quint16>::const_iterator i = d_offCorr.lowerBound(methodPos);
        if( i != d_offCorr.end() )
        {
            --i;
            //qDebug() << methodPos << i.key() << i.value() << d_offCorr;
            return methodPos - i.value();
        }else
            return methodPos;
    }

    void markCode( QTextCursor cur, const QPoint& pos, bool click )
    {
        const int numOfBangs = d_code.leftRef( cur.position() ).count(QChar('!'));
        //qDebug() << "cur pos" << cur.position() << numOfBangs << cur.position() + numOfBangs;
        Ast::Expression* e = d_that->d_curMethod->findByPos( cur.position() + numOfBangs + d_that->d_curMethod->d_pos );
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
                        d_that->fillNamedUse(id->d_resolved);
                    cur.setPosition( offCorr( e->d_pos - d_that->d_curMethod->d_pos ) );
                    cur.setPosition( cur.position() + e->getLen(), QTextCursor::KeepAnchor );
                    sel.cursor = cur;
                    esl.append(sel);
                    const QList<Ast::Ident*>& ids = d_that->d_mdl->getIxref().value(id->d_resolved);
                    for( int i = 0; i < ids.size(); i++ )
                    {
                        if( ids[i]->d_pos >= d_that->d_curMethod->d_pos &&
                                ids[i]->d_pos < d_that->d_curMethod->d_endPos )
                        {
                            cur.setPosition( offCorr( ids[i]->d_pos - d_that->d_curMethod->d_pos ) );
                            cur.setPosition( cur.position() + ids[i]->getLen(), QTextCursor::KeepAnchor );
                            sel.cursor = cur;
                            esl.append(sel);
                        }
                    }
                    if( id->d_resolved && id->d_resolved->d_pos >= d_that->d_curMethod->d_pos &&
                            id->d_resolved->d_pos < d_that->d_curMethod->d_endPos )
                    {
                        cur.setPosition( offCorr( id->d_resolved->d_pos - d_that->d_curMethod->d_pos ) );
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
                            if( click && QApplication::keyboardModifiers() == Qt::ControlModifier )
                            {
                                d_that->setCurClass( d_that->d_mdl->getClasses().value(id->d_resolved->d_name).data() );
                                d_that->pushLocation();
                                d_that->syncLists();
                                d_that->fillNamedUse(d_that->d_curClass.data());
                            }
                            break;
                        }
                    }
                    if( click )
                        QToolTip::showText(pos,
                                       tr("<html><b>%1:</b><p>%2</p></html>").arg(title)
                                       .arg(text),this);
                    QList<QTreeWidgetItem*> items = d_that->d_vars->findItems(
                                id->d_ident, Qt::MatchExactly );
                    if( items.size() == 1 )
                    {
                        d_that->d_vars->setCurrentItem(items.first());
                        d_that->d_vars->scrollToItem(items.first(),QAbstractItemView::PositionAtCenter );
                        d_that->d_vars->expandItem(items.first());
                    }else
                        d_that->d_vars->setCurrentItem(0);
                }
                break;
            case Ast::Thing::T_MsgSend:
                {
                    Ast::MsgSend* s = static_cast<Ast::MsgSend*>(e);
                    if( click )
                    {
                        QByteArray name = Lexer::getSymbol(s->prettyName(false));
                        d_that->fillPatternUse(name);
                    }
                    for( int i = 0; i < s->d_pattern.size(); i++ )
                    {
                        cur.setPosition( offCorr( s->d_pattern[i].second - d_that->d_curMethod->d_pos ) );
                        cur.setPosition( cur.position() + s->d_pattern[i].first.size(), QTextCursor::KeepAnchor );
                        sel.cursor = cur;
                        esl.append(sel);
                    }
                    if( click )
                    {
                        if( QApplication::keyboardModifiers() == Qt::ControlModifier )
                        {
                            if( s->d_receiver->getTag() == Ast::Thing::T_Ident )
                            {
                                Ast::Ident* id = static_cast<Ast::Ident*>( s->d_receiver.data() );
                                if( id->d_resolved && id->d_resolved->getTag() == Ast::Thing::T_Class )
                                {
                                    Ast::Class* cls = static_cast<Ast::Class*>( id->d_resolved );
                                    Ast::Method* m = cls->findMethod( Lexer::getSymbol(s->prettyName(false)) );
                                    if( m )
                                    {
                                        d_that->setCurMethod(m);
                                        d_that->setCurClass( m->getClass() );
                                        d_that->pushLocation();
                                        d_that->syncLists();
                                        d_that->fillPatternUse(m->d_name);
                                        return;
                                    }
                                }
                            }
                        }
                        QToolTip::showText(pos,
                                       tr("<html><b>Message:</b><p>%1</p></html>")
                                       .arg(s->prettyName().constData()),this);
                    }
                    QList<QTreeWidgetItem*> items = d_that->d_messages->findItems(
                                s->prettyName(false), Qt::MatchExactly );
                    if( items.size() == 1 )
                    {
                        d_that->d_messages->setCurrentItem(items.first());
                        d_that->d_messages->scrollToItem(items.first(),QAbstractItemView::PositionAtCenter );
                        d_that->d_messages->expandItem(items.first());
                    }else
                        d_that->d_messages->setCurrentItem(0);

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
        }
        QPlainTextEdit::mousePressEvent(event);
    }
};

ClassBrowser::ClassBrowser(QWidget *parent)
    : QMainWindow(parent),d_pushBackLock(false)
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
    createVars();

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

    new QShortcut(tr("ALT+LEFT"),this,SLOT(onGoBack()));
    new QShortcut(tr("ALT+RIGHT"),this,SLOT(onGoForward()));
}

ClassBrowser::~ClassBrowser()
{

}

bool ClassBrowser::parse(const QString& path)
{
    QFile in(path);
    if( !in.open(QIODevice::ReadOnly) )
        return false;

    // Parser::convertFile(&in,"out.txt");
    in.reset();
    d_path = path;
    QElapsedTimer time;
    time.start();
    const bool res = d_mdl->parse(&in);
    qDebug() << "parsed in" << time.elapsed() << "ms";
    if( !res )
    {
        d_code->appendPlainText("**** Parsing errors");
        foreach( const Model::Error& e, d_mdl->getErrs() )
            d_code->appendPlainText(e.d_msg);
        return false;
    }

    fillClassList();
    fillCatList();
    fillHierarchy();
    fillMessages();
    fillPrimitives();
    fillVars();
    return true;
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
    connect( d_class, SIGNAL(linkActivated(QString)), this, SLOT(onLink(QString)) );
}

static QByteArray superClasses( Ast::Class* c )
{
    if( c && c->getSuper() )
        return "<a href=\"class:" + c->getSuper()->d_name + "\">" +
                c->getSuper()->d_name + "</a> " + superClasses( c->getSuper() );
    else
        return QByteArray();
}

static QString fieldList( Ast::Class* c, int& nr, bool instance )
{
    QString res;
    Ast::Class* super = c->getSuper();
    if( super )
        res = fieldList(super, nr,instance);
    QString vars;
    for( int i = 0; i < c->d_vars.size(); i++ )
    {
        if( ( instance && c->d_vars[i]->d_kind == Ast::Variable::InstanceLevel )
                || ( !instance && c->d_vars[i]->d_kind == Ast::Variable::ClassLevel ) )
            vars += QString("<br>%1 %2").arg(nr++).arg(c->d_vars[i]->d_name.constData());
    }
    if( !vars.isEmpty() )
    {
        if( !res.isEmpty() )
            res += "<br>\n";
        res += QString("<u>%1</u>").arg(c->d_name.constData()) + vars;
    }

    return res;
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
        int nr = 1;
        QString varlist = "<h3>Instance:</h3>\n" + fieldList( d_curClass.data(), nr, true );
        nr = 1;
        varlist += "<h3>Class:</h3>\n" + fieldList( d_curClass.data(), nr, false );
        fields->setToolTip(0, varlist );
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
            mem->setToolTip(0,mem->text(0));
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

void ClassBrowser::setCurVar(Ast::Variable* v)
{
    if( v != 0 )
    {
        Ast::Class* c = v->d_owner->getClass();
        if( d_curClass != c )
        {
            d_curClass = v->d_owner->getClass();
            fillMembers();
        }
    }
    d_curVar = v;
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
    d_code->setCode( str );
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
            // sub->setData(0,Qt::UserRole+1, QVariant::fromValue( Ast::ClassRef(m->getClass()) ) );
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

void ClassBrowser::fillNamedUse(Ast::Named* n)
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
            item->setToolTip(0, QString("%1 uses%2").arg(count).arg( isAssig ? ", used as assignment target" : "" ));
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

void ClassBrowser::fillPatternUse(const QByteArray& pat)
{
    d_use->clear();
    d_useTitle->clear();

    if( pat.isEmpty() )
        return;

    d_useTitle->setText(QString("Pattern <b><i>%1</i></b>").arg( pat.constData() ) );

    const QList<Ast::MsgSend*>& sends = d_mdl->getTxref().value(pat.constData());
    Ast::Method* m = 0;
    int i = 0;
    while( i < sends.size() )
    {
        if( sends[i]->d_inMethod != m )
        {
            int count = 0;
            Ast::MsgSend* send = sends[i];
            m = send->d_inMethod;
            while( i < sends.size() && sends[i]->d_inMethod == m )
            {
                count++;
                i++;
            }
            QTreeWidgetItem* item = new QTreeWidgetItem( d_use );
            item->setText(0, QString::number(count) );
            item->setToolTip(0, QString("%1 uses").arg(count));
            Q_ASSERT( !send->d_pattern.isEmpty() );
            item->setData(0,Qt::UserRole,send->d_pattern.first().second );
            item->setText(1, QString("%1 %2").arg(send->d_inMethod->getClass()->d_name.constData() ).
                          arg(send->d_inMethod->d_name.constData() ) );
            item->setData(1,Qt::UserRole, QVariant::fromValue( Ast::MethodRef(send->d_inMethod) ) );
            item->setToolTip(1,item->text(1));
        }else
            i++;
    }
    d_use->sortByColumn(1,Qt::AscendingOrder);
    d_use->resizeColumnToContents(0);
}

void ClassBrowser::pushLocation()
{
    if( d_pushBackLock )
        return;
    Location loc = qMakePair(d_curClass,d_curMethod);
    if( !d_backHisto.isEmpty() && d_backHisto.last() == loc )
        return; // o ist bereits oberstes Element auf dem Stack.
    d_backHisto.removeAll( loc );
    d_backHisto.push_back( loc );
}

static QTreeWidgetItem* _find( QTreeWidgetItem* w, const QVariant& v, int col )
{
    for( int i = 0; i < w->childCount(); i++ )
    {
        if( w->child(i)->data(col,Qt::UserRole) == v )
            return w->child(i);
        QTreeWidgetItem* res = _find(w->child(i), v, col );
        if( res )
            return res;
    }
    return 0;
}

static QTreeWidgetItem* _find( QTreeWidget* w, const QVariant& v, int col = 0 )
{
    for( int i = 0; i < w->topLevelItemCount(); i++ )
    {
        if( w->topLevelItem(i)->data(col,Qt::UserRole) == v )
            return w->topLevelItem(i);
        QTreeWidgetItem* res = _find(w->topLevelItem(i), v, col );
        if( res )
            return res;
    }
    return 0;
}

void ClassBrowser::syncLists(QWidget* besides)
{
    if( d_classes != besides )
    {
        if( d_curClass.isNull() )
            d_classes->setCurrentItem(0);
        else
        {
            QTreeWidgetItem* res = _find(d_classes, QVariant::fromValue(d_curClass) );
            d_classes->setCurrentItem(res);
            d_classes->scrollToItem(res);
        }
    }
    if( d_cats != besides )
    {
        if( d_curClass.isNull() )
            d_cats->setCurrentItem(0);
        else
        {
            QTreeWidgetItem* res = _find(d_cats, QVariant::fromValue(d_curClass) );
            d_cats->setCurrentItem(res);
            d_cats->scrollToItem(res);
        }
    }
    if( d_hierarchy != besides )
    {
        if( d_curClass.isNull() )
            d_hierarchy->setCurrentItem(0);
        else
        {
            QTreeWidgetItem* res = _find(d_hierarchy, QVariant::fromValue(d_curClass) );
            d_hierarchy->setCurrentItem(res);
            d_hierarchy->scrollToItem(res);
        }
    }
    if( d_messages != besides )
    {
        if( d_curMethod.isNull() )
            d_messages->setCurrentItem(0);
        else
        {
            QTreeWidgetItem* res = _find(d_messages, QVariant::fromValue(d_curMethod) );
            d_messages->setCurrentItem(res);
            d_messages->scrollToItem(res);
        }
    }
    if( d_primitives != besides )
    {
        if( d_curMethod.isNull() )
            d_primitives->setCurrentItem(0);
        else
        {
            QTreeWidgetItem* res = _find(d_primitives, QVariant::fromValue(d_curMethod) );
            d_primitives->setCurrentItem(res);
            d_primitives->scrollToItem(res);
        }
    }
    if( d_members != besides )
    {
        if( d_curMethod.isNull() && d_curVar.isNull() )
            d_members->setCurrentItem(0);
        else if( !d_curMethod.isNull() )
        {
            QTreeWidgetItem* res = _find(d_members, QVariant::fromValue(d_curMethod) );
            d_members->setCurrentItem(res);
            d_members->scrollToItem(res);
        }else
        {
            QTreeWidgetItem* res = _find(d_members, QVariant::fromValue(d_curVar) );
            d_members->setCurrentItem(res);
            d_members->scrollToItem(res);
        }
    }
    if( d_use != besides )
    {
        if( d_curMethod.isNull() )
            d_use->setCurrentItem(0);
        else
        {
            QTreeWidgetItem* res = _find(d_use, QVariant::fromValue(d_curMethod), 1 );
            d_use->setCurrentItem(res);
            d_use->scrollToItem(res);
        }
    }
}

void ClassBrowser::createVars()
{
    QDockWidget* dock = new QDockWidget( tr("Globals && Fields"), this );
    dock->setObjectName("Vars");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable );
    d_vars = new QTreeWidget(dock);
    d_vars->setHeaderHidden(true);
    d_vars->setAlternatingRowColors(true);
    dock->setWidget(d_vars);
    addDockWidget( Qt::LeftDockWidgetArea, dock );
    connect( d_vars, SIGNAL(itemClicked(QTreeWidgetItem*,int)), this, SLOT(onVarsClicked()) );
}

void ClassBrowser::fillVars()
{
    d_vars->clear();
    const Model::VariableXref& vx = d_mdl->getVxref();
    QFont bold = d_vars->font();
    bold.setBold(true);
    Model::VariableXref::const_iterator i;
    for( i = vx.begin(); i != vx.end(); ++i )
    {
        QTreeWidgetItem* item = new QTreeWidgetItem( d_vars );
        item->setText(0, i.key() );
        item->setToolTip(0,item->text(0));
        foreach( Ast::Variable* v, i.value() )
        {
            QTreeWidgetItem* sub = new QTreeWidgetItem( item );
            if( v->d_owner && v->d_owner->getTag() == Ast::Thing::T_Class )
                sub->setText(0, v->d_owner->d_name );
            else
            {
                sub->setText(0, "<global>" );
                item->setFont(0,bold );
            }
            sub->setData(0,Qt::UserRole, QVariant::fromValue( Ast::VarRef(v) ) );
        }
    }
    d_vars->sortByColumn(0,Qt::AscendingOrder);
}

void ClassBrowser::onClassesClicked()
{
    QTreeWidgetItem* item = d_classes->currentItem();
    if( item == 0 )
        return;
    setCurClass( item->data(0,Qt::UserRole).value<Ast::ClassRef>().data() );
    pushLocation();
    syncLists(d_classes);
    fillNamedUse(d_curClass.data());
}

void ClassBrowser::onCatsClicked()
{
    QTreeWidgetItem* item = d_cats->currentItem();
    if( item == 0 || !item->data(0,Qt::UserRole).canConvert<Ast::ClassRef>() )
        return;
    setCurClass( item->data(0,Qt::UserRole).value<Ast::ClassRef>().data() );
    pushLocation();
    syncLists(d_cats);
    fillNamedUse(d_curClass.data());
}

void ClassBrowser::onMembersClicked()
{
    QTreeWidgetItem* item = d_members->currentItem();
    if( item == 0 )
        return;
    if( item->data(0,Qt::UserRole).canConvert<Ast::MethodRef>() )
    {
        setCurMethod( item->data(0,Qt::UserRole).value<Ast::MethodRef>().data() );
        pushLocation();
        syncLists(d_members);
        fillPatternUse(d_curMethod->d_name);
    }else if( item->data(0,Qt::UserRole).canConvert<Ast::VarRef>() )
    {
        setCurVar( item->data(0,Qt::UserRole).value<Ast::VarRef>().data() );
        pushLocation();
        syncLists(d_members);
        fillNamedUse(d_curVar.data());
    }
}

void ClassBrowser::onHierarchyClicked()
{
    QTreeWidgetItem* item = d_hierarchy->currentItem();
    if( item == 0 || !item->data(0,Qt::UserRole).canConvert<Ast::ClassRef>() )
        return;
    setCurClass( item->data(0,Qt::UserRole).value<Ast::ClassRef>().data() );
    pushLocation();
    syncLists(d_hierarchy);
    fillNamedUse(d_curClass.data());
}

void ClassBrowser::onMessagesClicked()
{
    QTreeWidgetItem* item = d_messages->currentItem();
    if( item == 0 || !item->data(0,Qt::UserRole).canConvert<Ast::MethodRef>() )
        return;
    setCurMethod( item->data(0,Qt::UserRole).value<Ast::MethodRef>().data() );
    if( !d_curMethod.isNull() )
        setCurClass( d_curMethod->getClass() );
    pushLocation();
    syncLists(d_messages);
    fillPatternUse(d_curMethod->d_name);
}

void ClassBrowser::onPrimitiveClicked()
{
    QTreeWidgetItem* item = d_primitives->currentItem();
    if( item == 0 || !item->data(0,Qt::UserRole).canConvert<Ast::MethodRef>() )
        return;
    setCurMethod( item->data(0,Qt::UserRole).value<Ast::MethodRef>().data() );
    if( !d_curMethod.isNull() )
        setCurClass( d_curMethod->getClass() );
    pushLocation();
    syncLists(d_primitives);
    fillPatternUse(d_curMethod->d_name);
}

void ClassBrowser::onVarsClicked()
{
    QTreeWidgetItem* item = d_vars->currentItem();
    if( item == 0 || !item->data(0,Qt::UserRole).canConvert<Ast::VarRef>() )
        return;
    d_curMethod = 0;
    setCurVar( item->data(0,Qt::UserRole).value<Ast::VarRef>().data() );
    pushLocation();
    syncLists(d_vars);
    fillNamedUse(d_curVar.data());
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
        const quint32 pos = item->data(0,Qt::UserRole).toUInt();
        if( pos > d_curMethod->d_pos )
        {
            cur.setPosition( pos - d_curMethod->d_pos );
            d_code->markCode( cur, QPoint(), false );
        }
    }
    pushLocation();
    syncLists(d_use);
}

void ClassBrowser::onGoBack()
{
    if( d_backHisto.size() <= 1 )
        return;

    d_pushBackLock = true;
    d_forwardHisto.push_back( d_backHisto.last() );
    d_backHisto.pop_back();
    setCurClass(d_backHisto.last().first.data());
    setCurMethod(d_backHisto.last().second.data());
    syncLists();
    d_pushBackLock = false;
}

void ClassBrowser::onGoForward()
{
    if( d_forwardHisto.isEmpty() )
        return;

    Location cur = d_forwardHisto.last();
    d_forwardHisto.pop_back();
    setCurClass(cur.first.data());
    setCurMethod(cur.second.data());
    syncLists();
    pushLocation();
}

void ClassBrowser::onLink(const QString& link)
{
    if( link.startsWith("class:") )
    {
        setCurClass( d_mdl->getClasses().value(link.mid(6).toUtf8()).data() );
        pushLocation();
        syncLists();
        fillNamedUse(d_curClass.data());
    }
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationName("me@rochus-keller.ch");
    a.setOrganizationDomain("github.com/rochus-keller/Smalltalk");
    a.setApplicationName("Smalltalk 80 Class Browser");
    a.setApplicationVersion("0.7.2");
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
