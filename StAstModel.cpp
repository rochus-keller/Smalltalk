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

#include "StAstModel.h"
#include "StParser.h"
#include "StLexer.h"
#include <QtDebug>
using namespace St;
using namespace St::Ast;

static const char* s_globals[] = {

    "AltoFilePool",     // Alto stuff
    "Backp",            // Alto stuff
    "Basal",            // Font Face ?
    "Boffset",          // Alto stuff
    "Bold",
    "BoldItalic",
    "BS",
    "BS2",
    "CaretForm",
    "CCR",
    "CCW",
    "Centered",
    "CR",
    "CrossedX",
    "CtrlB",
    "Ctrlb",
    "Ctrlf",
    "Ctrli",
    "CtrlI",
    "CtrlMinus",
    "CtrlS",
    "Ctrls",
    "CtrlShiftMinus",
    "Ctrlt",
    "Ctrlw",
    "Ctrlx",
    "Ctrlz",
    "Cut",
    "CWW",
    "DefaultBaseline",
    "DefaultLineGrid",
    "DefaultMarginTabsArray",
    "DefaultMask",
    "DefaultRule",
    "DefaultSpace",
    "DefaultTab",
    "DefaultTabsArray",
    "DefaultTextStyle",
    "Dfmask",
    "Dirname",
    "Disk",
    "Display",
    "EndOfRun",
    "ESC",
    "Face",
    "FamilyName",
    "FilePool",
    "Italic",
    "Justified",
    "LeftFlush",
    "Nextp",
    "NonFaceEmphasisMask",
    "Numch",
    "Pagen",
    "Paste",
    "PointSize",
    "Processor",
    "Read",
    "RightFlush",
    "ScheduledControllers", // class ControlManager
    "Sensor",               // NOTE: Sensor seems to be the only global appearing left of an assignment
    "Shorten",
    "Smalltalk",            // class SystemDictionary
    "Sn1",                  // Alto stuff
    "Sn2",                  // Alto stuff
    "SourceFiles",          // OrderedCollection ?
    "Space",
    "SubscriptedBit",
    "SubSuperscriptMask",
    "SuperscriptedBit",
    "SystemOrganization",
    "Tab",
    "TextConstants",
    "Transcript",
    "Undeclared",
    "Underlined",
    "UnderlinedBit",
    "Vn",
    "Write",
    // st80 original error "byte",
    // st80 original error "firstByte",
    // st80 original error "response",
    // st80 original error "secondByte",
    0
};

Model::Model(QObject *parent) : QObject(parent)
{

}

static bool sortSubs( const Ref<Class>& lhs, const Ref<Class>& rhs )
{
    return lhs->d_name < rhs->d_name;
}

static bool sortFields( const Ref<Variable>& lhs, const Ref<Variable>& rhs )
{
    return lhs->d_name < rhs->d_name;
}

struct Model::ResolveIdents : public AstVisitor
{
    QList<Scope*> stack;
    Model* mdl;
    Method* meth;
    QHash<const char*,int> unresolved;
    bool inAssig;

    void visit( Class* m )
    {
        inAssig = false;
        meth = 0;
        stack.push_back(m);
        for( int i = 0; i < m->d_vars.size(); i++ )
            m->d_vars[i]->accept(this);
        for( int i = 0; i < m->d_methods.size(); i++ )
            m->d_methods[i]->accept(this);
        stack.pop_back();
    }
    void visit( Variable* v )
    {
       if( meth )
       {
           Ref<Ident> id = new Ident(v->d_name, v->d_pos, meth);
           id->d_resolved = v;
           id->d_use = Ident::Declaration;
           meth->d_helper << id.data();
           mdl->d_ix[v].append(id.data());
       }
    }
    void visit( Method* m)
    {
        meth = m;
        stack.push_back(m);
        for( int i = 0; i < m->d_vars.size(); i++ )
            m->d_vars[i]->accept(this);
        for( int i = 0; i < m->d_body.size(); i++ )
            m->d_body[i]->accept(this);
        stack.pop_back();
        meth = 0;
    }
    void visit( Block* b)
    {
        stack.push_back(b->d_func.data());
        for( int i = 0; i < b->d_func->d_vars.size(); i++ )
            b->d_func->d_vars[i]->accept(this);
        for( int i = 0; i < b->d_func->d_body.size(); i++ )
            b->d_func->d_body[i]->accept(this);
        stack.pop_back();
    }
    void visit( Cascade* c )
    {
        for( int i = 0; i < c->d_calls.size(); i++ )
            c->d_calls[i]->accept(this);
    }
    void visit( Assig* a )
    {
        inAssig = true;
        for( int i = 0; i < a->d_lhs.size(); i++ )
            a->d_lhs[i]->accept(this);
        inAssig = false;
        a->d_rhs->accept(this);
    }
    void visit( Ident* i )
    {
        Q_ASSERT( !stack.isEmpty() );
        if( inAssig )
            i->d_use = Ident::AssigTarget;
        else
            i->d_use = Ident::Rhs;

        if( mdl->d_keywords.contains(i->d_ident.constData()) )
        {
            i->d_keyword = true;
            return;
        }

        if( inAssig )
        {
            QList<Named*> res = stack.back()->findVars(i->d_ident);
            if( res.isEmpty() )
                res = mdl->d_globals.findVars(i->d_ident);
            Named* hit = 0;
            for( int j = 0; j < res.size(); j++ )
            {
                if( !res[j]->classLevel() || hit == 0 )
                    hit = res[j];
            }
            if( hit )
            {
                i->d_resolved = hit;
                mdl->d_ix[hit].append(i);
                return;
            }
        }else
        {
            QList<Named*> res = stack.back()->findVars(i->d_ident);
            if( res.isEmpty() )
                res = stack.back()->findMeths(i->d_ident);
            if( res.isEmpty() )
            {
                Named* n = mdl->d_classes2.value(i->d_ident.constData()).data();
                if( n )
                    res << n;
            }
            if( res.isEmpty() )
                res = mdl->d_globals.findVars(i->d_ident);

            if( !res.isEmpty() )
            {
                //if( res.size() > 1 ) // apparently never happens in St80.sources
                //    qDebug() << "more than one result" << i->d_ident << meth->getClass()->d_name << meth->d_name;
                i->d_resolved = res.first();
                mdl->d_ix[res.first()].append(i);
                return;
            }
        }
#if 0
        QList<Named*> res = stack.back()->find(i->d_ident);
        if( res.size() > 1 )
        {
            qDebug() << "***" << meth->getClass()->d_name << meth->d_name << i->d_ident << (i->d_pos - meth->d_pos);
            for( int j = 0; j < res.size(); j++ )
            {
                qDebug() << res[j]->classLevel() << res[j]->getTag() << inAssig;
            }
        }
#endif
        unresolved[i->d_ident.constData()]++;
        //else
        qWarning() << "cannot resolve ident" << i->d_ident << "in class/method" << meth->getClass()->d_name << meth->d_name;
    }
    void visit( MsgSend* s)
    {
        for( int i = 0; i < s->d_args.size(); i++ )
            s->d_args[i]->accept(this);
        s->d_receiver->accept(this);
        QByteArray name = Lexer::getSymbol( s->prettyName(false) );
        mdl->d_tx[name.constData()].append(s);
        if( s->d_receiver->getTag() == Ast::Thing::T_Ident )
        {
            Ast::Ident* id = static_cast<Ast::Ident*>(s->d_receiver.data());
            id->d_use = Ident::MsgReceiver;
        }
    }
    void visit( Return* r )
    {
        r->d_what->accept(this);
    }
    void visit( ArrayLiteral* a)
    {
        for( int i = 0; i < a->d_elements.size(); i++ )
            a->d_elements[i]->accept(this);
    }
};

bool Model::parse(QIODevice* in)
{
    clear();

    nil = Lexer::getSymbol("nil");
    d_keywords.insert(nil.constData());
    d_keywords.insert( Lexer::getSymbol("true").constData());
    d_keywords.insert( Lexer::getSymbol("false").constData());
    d_keywords.insert( Lexer::getSymbol("self").constData());
    d_keywords.insert( Lexer::getSymbol("super").constData() );
    d_keywords.insert( Lexer::getSymbol("thisContext").constData());
    fillGlobals();

    Lexer lex;
    lex.setDevice(in);
    Parser p(&lex);
    if( !p.readFile() )
    {
        foreach( const Parser::Error& e, p.getErrs() )
            d_errs += Error( e.d_msg, e.d_pos );
        return false;
    }

    d_classes2 = p.getClasses();
    // check super classes
    Parser::Classes::const_iterator i;
    for( i = p.getClasses().begin(); i != p.getClasses().end(); ++i )
    {
        if( i.value()->d_superName.isEmpty() )
            d_errs += Error( "class without super class", i.value()->d_pos );
        else if( i.value()->d_superName.constData() == nil.constData() )
        {
            if( i.value()->d_name != "Object" )
                d_errs += Error( "only Object is a subclass of nil", i.value()->d_pos );
            i.value()->d_superName.clear();
        }else
        {
            Parser::Classes::const_iterator j = p.getClasses().find( i.value()->d_superName.constData() );
            if( j == p.getClasses().end() )
                d_errs += Error( "unknown super class", i.value()->d_pos );
            else
            {
                j.value()->d_subs.append(i.value());
                i.value()->d_owner = j.value().data();
            }
        }
        d_classes.insert( i.value()->d_name, i.value() );
        if( !i.value()->d_category.isEmpty() )
            d_cats[ i.value()->d_category ].append( i.value().data() );
        // don't sort vars: std::sort( i.value()->d_vars.begin(), i.value()->d_vars.end(), sortFields );
        for( int j = 0; j < i.value()->d_methods.size(); j++ )
        {
            d_mx[ i.value()->d_methods[j]->d_name.constData() ].append( i.value()->d_methods[j].data() );
            if( i.value()->d_methods[j]->d_primitive )
                d_px[ i.value()->d_methods[j]->d_primitive ].append( i.value()->d_methods[j].data() );
        }
        for( int j = 0; j < i.value()->d_vars.size(); j++ )
        {
            d_vx[ i.value()->d_vars[j]->d_name.constData() ].append( i.value()->d_vars[j].data() );
        }
    }

    ResolveIdents ri;
    ri.mdl = this;
    for( i = p.getClasses().begin(); i != p.getClasses().end(); ++i )
    {
        std::sort( i.value()->d_subs.begin(), i.value()->d_subs.end(), sortSubs );
        i.value()->accept(&ri);
    }
#if 0
    qDebug() << "********* unresolved";
    QHash<const char*,int>::const_iterator j;
    for( j = ri.unresolved.begin(); j != ri.unresolved.end(); ++j )
        //qDebug() << j.key() << "\t" << j.value();
        qDebug() << QString("\"%1\",").arg(j.key()).toUtf8().constData();
#endif
    return d_errs.isEmpty();
}

void Model::clear()
{
    d_mx.clear();
    d_px.clear();
    d_errs.clear();
    d_classes.clear();
    d_classes2.clear();
    d_cats.clear();
    d_keywords.clear();
    d_ix.clear();
    d_tx.clear();
    d_vx.clear();
    d_globals.d_varNames.clear();
    d_globals.d_vars.clear();
}

void Model::fillGlobals()
{
    int i = 0;
    while( s_globals[i] != 0 )
    {
        Ref<Variable> v = new Variable();
        v->d_name = Lexer::getSymbol(s_globals[i]);
        v->d_kind = Variable::Global;
        v->d_owner = &d_globals;
        d_globals.d_vars.append(v);
        d_globals.d_varNames[v->d_name.constData()].append( v.data() );
        d_vx[v->d_name.constData()].append( v.data() );
        i++;
    }
}

Ast::Method*Ast::Class::findMethod(const QByteArray& name) const
{
    for( int i = 0; i < d_methods.size(); i++ )
    {
        if( d_methods[i]->d_name.constData() == name.constData() )
            return d_methods[i].data();
    }
    return 0;
}

Variable*Class::findVar(const QByteArray& name) const
{
    for( int i = 0; i < d_vars.size(); i++ )
    {
        if( d_vars[i]->d_name.constData() == name.constData() )
            return d_vars[i].data();
    }
    return 0;
}

Class*Class::getSuper() const
{
    if( d_owner && d_owner->getTag() == Thing::T_Class )
        return static_cast<Class*>(d_owner);
    else
        return 0;
}

void Class::addMethod(Method* m)
{
//    Method* old = findMethod(m->d_name);
//    if( old && old->d_classLevel == m->d_classLevel )
//        qWarning() << "duplicate method name" << m->d_name << "in class" << d_name;
    d_methods.append(m);
    m->d_owner = this;
    d_methodNames[m->d_name.constData()].append(m);
}

void Class::addVar(Variable* v)
{
//    if( findVar(v->d_name ) )
//        qWarning() << "duplicate variable name" << v->d_name << "in class" << d_name;
    d_vars.append(v);
    v->d_owner = this;
    d_varNames[v->d_name.constData()].append(v);
}

QByteArray Method::prettyName(const QByteArrayList& pattern, quint8 kind , bool withSpace)
{
    if( !pattern.isEmpty() )
    {
        switch( kind )
        {
        case UnaryPattern:
        case BinaryPattern:
            return pattern.first();
        case KeywordPattern:
            return pattern.join(withSpace ? ": " : ":" ) + ':';
        }
    }
    return QByteArray();
}

QByteArray Method::prettyName(bool withSpace) const
{
    return prettyName(d_pattern,d_patternType,withSpace);
}

void Method::updateName()
{
    if( d_name.isEmpty() )
        d_name = Lexer::getSymbol( prettyName(false) );
}

Variable*Method::findVar(const QByteArray& name) const
{
    for( int i = 0; i < d_vars.size(); i++ )
    {
        if( d_vars[i]->d_name.constData() == name.constData() )
            return d_vars[i].data();
    }
    return 0;
}

Class*Scope::getClass() const
{
    if( getTag() == Thing::T_Class )
        return static_cast<Class*>( const_cast<Scope*>(this) );
    else if( d_owner )
        return d_owner->getClass();
    else
        return 0;
}

Expression* Method::findByPos(quint32 pos) const
{
    struct Locator : public AstVisitor
    {
        quint32 d_pos;
        bool inline isHit( Thing* t )
        {
            return isHit( t->d_pos, t->getLen() );
        }
        bool inline isHit( quint32 pos, quint32 len )
        {
            return pos <= d_pos && d_pos <= pos + len;
        }
        void visit( Method* m)
        {
            for( int i = 0; i < m->d_vars.size(); i++ )
                m->d_vars[i]->accept(this);
            for( int i = 0; i < m->d_body.size(); i++ )
                m->d_body[i]->accept(this);
            for( int i = 0; i < m->d_helper.size(); i++ )
                m->d_helper[i]->accept(this);
        }
        void visit( Block* b)
        {
            for( int i = 0; i < b->d_func->d_vars.size(); i++ )
                b->d_func->d_vars[i]->accept(this);
            for( int i = 0; i < b->d_func->d_body.size(); i++ )
                b->d_func->d_body[i]->accept(this);
        }
        void visit( Cascade* c )
        {
            for( int i = 0; i < c->d_calls.size(); i++ )
                c->d_calls[i]->accept(this);
        }
        void visit( Assig* a )
        {
            for( int i = 0; i < a->d_lhs.size(); i++ )
                a->d_lhs[i]->accept(this);
            a->d_rhs->accept(this);
        }
        void visit( Symbol* s )
        {
            if( isHit(s) )
                throw (Expression*)s;
        }
        void visit( Ident* i )
        {
            if( isHit(i) )
                throw (Expression*)i;
        }
        void visit( Selector* s)
        {
            if( isHit(s) )
                throw (Expression*)s;
        }
        void visit( MsgSend* s)
        {
            for( int i = 0; i < s->d_pattern.size(); i++ )
                if( isHit( s->d_pattern[i].second, s->d_pattern[i].first.size() ) )
                    throw (Expression*)s;
            for( int i = 0; i < s->d_args.size(); i++ )
                s->d_args[i]->accept(this);
            s->d_receiver->accept(this);
        }
        void visit( Return* r )
        {
            r->d_what->accept(this);
        }
        void visit( ArrayLiteral* a)
        {
            for( int i = 0; i < a->d_elements.size(); i++ )
                a->d_elements[i]->accept(this);
        }
    };

    Locator l;
    l.d_pos = pos;
    try
    {
        const_cast<Method*>(this)->accept(&l);
    }catch( Expression* e )
    {
        return e;
    }
    return 0;
}


Variable*Function::findVar(const QByteArray& name) const
{
    for( int i = 0; i < d_vars.size(); i++ )
    {
        if( d_vars[i]->d_name.constData() == name.constData() )
            return d_vars[i].data();
    }
    return 0;
}

void Function::addVar(Variable* v)
{
//    if( findVar(v->d_name.constData() ) )
//        qWarning() << "duplicate variable name" << v->d_name << "in function" << d_name;
    d_vars.append(v);
    v->d_owner = this;
    d_varNames[v->d_name.constData()].append(v);
}

Method* Scope::getMethod() const
{
    if( getTag() == Thing::T_Method )
        return static_cast<Method*>( const_cast<Scope*>(this) );
    else if( d_owner )
        return d_owner->getMethod();
    else
        return 0;
}

Block::Block()
{
    d_func = new Function();
}

QByteArray MsgSend::prettyName(bool withSpace) const
{
    QByteArrayList tmp;
    for( int i = 0; i < d_pattern.size(); i++ )
        tmp << d_pattern[i].first;
    return Method::prettyName(tmp,d_patternType,withSpace);
}


QList<Named*> Scope::findVars(const QByteArray& name, bool recursive) const
{
    QList<Named*> res = d_varNames.value(name.constData());
    if( res.isEmpty() && recursive && d_owner )
        res = d_owner->findVars(name, recursive);
    return res;
}

QList<Named*> Scope::findMeths(const QByteArray& name, bool recursive) const
{
    QList<Named*> res = d_methodNames.value(name.constData());
    if( res.isEmpty() && recursive && d_owner )
        res = d_owner->findMeths(name, recursive);
    return res;
}
