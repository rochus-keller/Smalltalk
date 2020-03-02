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

#include "StParser.h"
#include "StLexer.h"
#include <QIODevice>
#include <QtDebug>
#include <QFile>
using namespace St;
using namespace St::Ast;

Parser::Parser(Lexer* l):d_lex(l)
{
    subclass = Lexer::getSymbol("subclass");
    comment = Lexer::getSymbol("comment");
    methodsFor = Lexer::getSymbol("methodsFor");
    instanceVariableNames = Lexer::getSymbol("instanceVariableNames");
    classVariableNames = Lexer::getSymbol("classVariableNames");
    poolDictionaries = Lexer::getSymbol("poolDictionaries");
    category = Lexer::getSymbol("category");
    class_  = Lexer::getSymbol("class");
    variableSubclass = Lexer::getSymbol("variableSubclass");
    variableByteSubclass = Lexer::getSymbol("variableByteSubclass");
    variableWordSubclass = Lexer::getSymbol("variableWordSubclass");
    primitive = Lexer::getSymbol("primitive");
}

bool Parser::readFile()
{
    while( d_lex->peek().d_type != Lexer::EoF )
    {
        if( !readClass() )
            return false;
    }
    return true;
}

bool Parser::readClass()
{
    bool found = false;
    while( d_lex->peek().d_type != Lexer::EoC && d_lex->peek().isValid() )
    {
        if( !readClassExpr() )
            return false;
        found = true;
    }
    if( !found )
        return error("invalid class format", d_lex->getLine() );
    return found;
}

static QByteArray readLine( QIODevice* in )
{
    QByteArray res;
    char ch;
    in->getChar(&ch);
    while( !in->atEnd() && ch != '\r' && ch != '\n' )
    {
        if( ch == 0 )
            res += "§0";
        else if( ch == 12 )
            res += "§12\n"; // 12 == form feed
        else
            res += ch;
        in->getChar(&ch);
    }
    return res;
}

void Parser::convertFile(QIODevice* in, const QString& to)
{
    QFile out(to);
    out.open(QIODevice::WriteOnly);
    while( !in->atEnd() )
    {
        const QByteArray line = readLine(in);
        out.write( line );
        out.write( "\n" );
    }
}

bool Parser::error(const QByteArray& msg, quint32 pos)
{
    qCritical() << "ERROR" << msg.constData() << pos;
    d_errs.append( Error(msg,pos) );
    return false;
}

static void printChunk( const QList<Lexer::Token>& toks, quint32 line )
{
    QByteArray str;
    for( int i = 0; i < toks.size(); i++ )
        str += QByteArray(Lexer::s_typeName[toks[i].d_type]) + " ";
    qDebug() << "Chunk:" << line << str.constData();
}

bool Parser::readClassExpr()
{
    static const char* msg1 = "invalid message format";
    QList<Lexer::Token> toks;
    Lexer::Token t = d_lex->next();
    while( t.isValid() )
    {
        if( t.d_type == Lexer::Bang )
            break;
        toks << t;
        t = d_lex->next();
    }

    if( toks.size() <= 2 )
        return true;

    if( toks[0].d_type != Lexer::Ident && toks[1].d_type != Lexer::Ident )
        return error(msg1, toks.first().d_pos );

    const char* cmd = toks[1].d_val.constData();
    if( cmd == subclass.constData() || cmd == variableSubclass.constData() || cmd == variableByteSubclass.constData()
            || cmd == variableWordSubclass.constData() )
    {
        d_curClass = new Class();

        if( toks.size() < 4 || toks[2].d_type != Lexer::Colon || toks[3].d_type != Lexer::Symbol )
            return error(msg1, toks.first().d_pos );

        d_curClass->d_pos = toks.first().d_pos;
        d_curClass->d_name = toks[3].d_val.trimmed();
        d_curClass->d_superName = toks[0].d_val.trimmed();

        if( d_classes.contains(d_curClass->d_name.constData()) )
            return error("duplicate class name", toks.first().d_pos );
        d_classes.insert(d_curClass->d_name.constData(),d_curClass);

        int i = 4;
        while( i < toks.size() )
        {
            if( toks.size() - i < 3 )
                return error(msg1, toks.first().d_pos );
            if( toks[i].d_type != Lexer::Ident || toks[i+1].d_type != Lexer::Colon || toks[i+2].d_type != Lexer::String )
                return error(msg1, toks.first().d_pos );
            const char* cmd2 = toks[i].d_val.constData();
            if( cmd2 == instanceVariableNames.constData() )
            {
                addFields( toks[i+2].d_val.simplified().trimmed(), false, toks[i+2].d_pos );
            }else if( cmd2 == classVariableNames.constData() )
            {
                addFields( toks[i+2].d_val.simplified().trimmed(), true, toks[i+2].d_pos );
            }else if( cmd2 == category.constData() )
            {
                d_curClass->d_category = toks[i+2].d_val.trimmed();
            }else if( cmd2 == poolDictionaries.constData() )
            {
                // TODO NOP?
#if 0
                QByteArray str = toks[i+2].d_val.trimmed();
                if( !str.isEmpty() )
                    qDebug() << "class" << d_curClass->d_name << "poolDictionary" << str;
                /* renders
                class "AltoFile" poolDictionary "AltoFilePool"
                class "AltoFileDirectory" poolDictionary "AltoFilePool"
                class "AltoFilePage" poolDictionary "AltoFilePool"
                class "CharacterBlock" poolDictionary "TextConstants"
                class "CharacterBlockScanner" poolDictionary "TextConstants"
                class "CharacterScanner" poolDictionary "TextConstants"
                class "CompositionScanner" poolDictionary "TextConstants"
                class "DisplayScanner" poolDictionary "TextConstants"
                class "DisplayText" poolDictionary "TextConstants"
                class "File" poolDictionary "FilePool"
                class "FileDirectory" poolDictionary "FilePool"
                class "FilePage" poolDictionary "FilePool"
                class "FileStream" poolDictionary "FilePool"
                class "Paragraph" poolDictionary "TextConstants"
                class "ParagraphEditor" poolDictionary "TextConstants"
                class "StrikeFont" poolDictionary "TextConstants"
                class "Text" poolDictionary "TextConstants"
                class "TextLineInterval" poolDictionary "TextConstants"
                class "TextStyle" poolDictionary "TextConstants"
                */
#endif
            }else
                qWarning() << "unknown field" << cmd2 /* << toks[i].d_line */;
            i += 3;
        }
    }else if( cmd == comment.constData() )
    {
        if( toks.size()!= 4 || toks[2].d_type != Lexer::Colon || toks[3].d_type != Lexer::String )
            return error(msg1, toks.first().d_pos );
        Ast::Class* c = getClass(toks[0].d_val, toks[0].d_pos);
        if( c == 0 )
            return false;
        c->d_comment = toks[3].d_val;
        c->d_comment.replace('\r', '\n');
    }else if( cmd == methodsFor.constData() )
    {
        Ast::Class* c = getClass(toks[0].d_val, toks[0].d_pos);
        if( c == 0 )
            return false;

        while( d_lex->peek().d_type != Lexer::Bang && d_lex->peek().isValid() )
        {
            Ast::Ref<Ast::Method> m = readMethod(c,false);
            if( m.isNull() )
                return false;
            m->d_category = toks[3].d_val;
        }
    }else if( cmd == class_.constData() )
    {
        if( toks.size() < 5 || toks[2].d_type != Lexer::Ident ||
                toks[3].d_type != Lexer::Colon || toks[4].d_type != Lexer::String )
            return error(msg1, toks.first().d_pos );
        const char* cmd2 = toks[2].d_val.constData();
        Ast::Class* c = getClass(toks[0].d_val, toks[0].d_pos);
        if( c == 0 )
            return false;
        if( cmd2 == methodsFor.constData() )
        {
            while( d_lex->peek().d_type != Lexer::Bang && d_lex->peek().isValid() )
            {
                Ast::Ref<Ast::Method> m = readMethod(c,true);
                if( m.isNull() )
                    return false;
                m->d_category = toks[4].d_val;
            }
        }else if( cmd2 == instanceVariableNames.constData() )
        {
            addFields( toks[4].d_val.simplified().trimmed(), false, toks[4].d_pos ); // TODO: really instance?
        }else if( cmd2 == comment.constData() )
        {
            c->d_classComment = toks[4].d_val;
            c->d_classComment.replace('\r', '\n');
        }else
            qWarning() << "unknown command" << cmd2 /* << toks[2].d_line */;
    }else
        qWarning() << "unknown command" << cmd /* << toks[1].d_line */;

    return true;
}

static bool isBinaryChar( const Lexer::Token& t )
{
    return Lexer::isBinaryTokType(t.d_type);
}

Ast::Ref<Method> Parser::readMethod(Class* c, bool classLevel )
{
    static const char* msg1 = "invalid method header";
    Ref<Method> m = new Method();
    QList<Lexer::Token> toks;
    Lexer::Token t = d_lex->next();
    while( t.isValid() )
    {
        if( t.d_type == Lexer::Bang )
        {
            m->d_endPos = t.d_pos - 1;
            break;
        }
        toks << t;
        t = d_lex->next();
    }
    if( toks.isEmpty() )
    {
        error(msg1,t.d_pos);
        return 0;
    }

    int bodyStart = 0;

    m->d_pos = toks.first().d_pos;

    if( isBinaryChar(toks.first()) )
    {
        // binarySelector
        QByteArray str;
        while( bodyStart < toks.size() && isBinaryChar( toks[bodyStart] ) )
            str += toks[bodyStart++].d_val;
        m->d_pattern << str;
        if( !toks[bodyStart].d_type == Lexer::Ident )
            error(msg1,toks.first().d_pos);
        else
        {
            Ref<Variable> l = new Variable();
            l->d_kind = Variable::Argument;
            l->d_pos = toks[bodyStart].d_pos;
            l->d_name = toks[bodyStart].d_val;
            m->addVar( l.data() );
            bodyStart++;
        }
        m->d_patternType = Ast::BinaryPattern;
    }else if( toks.first().d_type == Lexer::Ident )
    {
        if( toks.size() > 1 && toks[1].d_type == Lexer::Colon )
        {
            // Keyword selector
            m->d_patternType = Ast::KeywordPattern;
            while( toks.size() - bodyStart >= 3 && toks[bodyStart].d_type == Lexer::Ident &&
                   toks[bodyStart+1].d_type == Lexer::Colon && toks[bodyStart+2].d_type == Lexer::Ident )
            {
                m->d_pattern += toks[bodyStart].d_val;
                Ref<Variable> l = new Variable();
                l->d_kind = Variable::Argument;
                l->d_pos = toks[bodyStart+2].d_pos;
                l->d_name = toks[bodyStart+2].d_val;
                m->addVar( l.data() );
                bodyStart += 3;
            }
        }else
        {
            // unary selector
            bodyStart++;
            m->d_pattern << toks.first().d_val;
            m->d_patternType = Ast::UnaryPattern;
        }
    }else
    {
        error(msg1,toks.first().d_pos);
        return 0;
    }
    Q_ASSERT( !m->d_pattern.isEmpty() );
    m->updateName();

    m->d_classLevel = classLevel;
    c->addMethod(m.data());

    TokStream ts(toks,bodyStart);
    parseMethodBody( m.data(), ts );

    return m;
}

Class*Parser::getClass(const QByteArray& name, quint32 pos)
{
    if( d_curClass->d_name.constData() == name.constData() )
        return d_curClass.data();
    // else
    qWarning() << "try to modify other than current class" << name << d_curClass->d_name << d_lex->getLine();
    Class* c = d_classes.value(name.constData()).data();
    if( c == 0 )
        error("unknown class",pos);
    return c;
}

bool Parser::addFields(const QByteArray& str, bool classLevel, quint32 pos)
{
    if( str.isEmpty() )
        return true;
    QByteArrayList names = str.split(' ');
    foreach( const QByteArray& n, names )
    {
        if( d_curClass->findVar(n) )
            return error("duplicate field name", pos );

        Ref<Variable> f = new Variable();
        f->d_name = Lexer::getSymbol(n);
        f->d_kind = classLevel ? Variable::ClassLevel : Variable::InstanceLevel;
        f->d_pos = pos;
        d_curClass->addVar(f.data());
    }
    return true;
}

bool Parser::parseMethodBody(Method* m, TokStream& ts)
{
    Lexer::Token t = ts.peek();
    if( t.d_type == Lexer::Bar )
    {
        // declare locals
        parseLocals( m, ts );
        t = ts.peek();
    }
    while( !ts.atEnd() && t.isValid() )
    {
        switch( t.d_type )
        {
        case Lexer::Ident:
        case Lexer::Hash:
        case Lexer::Symbol:
        case Lexer::Lpar:
        case Lexer::Lbrack:
        case Lexer::Number:
        case Lexer::String:
        case Lexer::Char:
        case Lexer::Minus:
            {
                Ref<Expression> e = parseExpression(m,ts);
                if( !e.isNull() )
                    m->d_body.append(e);
            }
            break;
        case Lexer::Lt:
            parsePrimitive(m,ts);
            break;
        case Lexer::Hat:
            {
                Ref<Expression> e = parseReturn(m,ts);
                if( !e.isNull() )
                    m->d_body.append(e);
            }
            break;
        case Lexer::Dot:
            // NOP
            ts.next();
            break;
        default:
            return error("expecting statement", ts.peek().d_pos );
        }
        t = ts.peek();
    }
    return true;
}

bool Parser::parseLocals(Method* m, Parser::TokStream& ts)
{
    Lexer::Token t = ts.next();
    Q_ASSERT( t.d_type == Lexer::Bar );

    t = ts.peek();
    while( t.isValid() && t.d_type == Lexer::Ident )
    {
        ts.next();
        if( m->findVar(t.d_val) )
            return error("duplicate local name", t.d_pos );
        Ref<Variable> l = new Variable();
        l->d_name = t.d_val;
        l->d_kind = Variable::Temporary;
        l->d_pos = t.d_pos;
        m->addVar(l.data());
        t = ts.peek();
    }
    if( t.d_type != Lexer::Bar )
        return error("expecting '|' after temps declaration", t.d_pos );
    // else
    ts.next();
    return true;
}

bool Parser::parsePrimitive(Method* m, Parser::TokStream& ts)
{
    static const char* s_msg = "invalid primitive";
    Lexer::Token t = ts.next();
    Q_ASSERT( t.d_type == Lexer::Lt );
    t = ts.next();
    if( t.d_type != Lexer::Ident || t.d_val.constData() != primitive.constData() )
        return error( s_msg, t.d_pos );
    t = ts.next();
    if( t.d_type != Lexer::Colon )
        return error( s_msg, t.d_pos );
    t = ts.next();
    if( t.d_type != Lexer::Number )
        return error( s_msg, t.d_pos );
    bool ok;
    const quint32 id = t.d_val.toUInt(&ok);
    if( !ok || id == 0 || id > 255 )
        return error( "invalid primitive id", t.d_pos );
    if( m->d_primitive != 0 )
        return error( "method with more than one primitive", t.d_pos );
    m->d_primitive = id;
    t = ts.next();
    if( t.d_type != Lexer::Gt )
        return error( s_msg, t.d_pos );
    return true;
}

Ref<Expression> Parser::parseExpression(Ast::Function* scope,Parser::TokStream& ts, bool dontApplyKeywords )
{
    Lexer::Token t = ts.peek();
    Ref<Expression> lhs;
    switch( t.d_type )
    {
    case Lexer::Ident:
        if( ts.peek(2).d_type == Lexer::Assig )
        {
            lhs = parseAssig(scope, ts);
            if( lhs.isNull() )
                return 0;
        }
        else
        {
            lhs = new Ident( t.d_val, t.d_pos, scope->getMethod() );
            ts.next();
        }
        break;
    case Lexer::Minus:
        ts.next(); // eat '-'
        t = ts.peek();
        if( t.d_type == Lexer::Number )
        {
            t.d_val = "-" + t.d_val;
            lhs = new Number( t.d_val, t.d_pos );
            ts.next();
        }else
        {
            error("expecting number after '-'",t.d_pos);
            return lhs.data();
        }
        break;
    case Lexer::Number:
        lhs = new Number( t.d_val, t.d_pos );
        ts.next();
        break;
    case Lexer::String:
        lhs = new String( t.d_val, t.d_pos );
        ts.next();
        break;
    case Lexer::Char:
        Q_ASSERT( t.d_val.size() == 1 );
        lhs = new Char( t.d_val[0], t.d_pos );
        ts.next();
        break;
    case Lexer::Hash:
        if( ts.peek(2).d_type == Lexer::Lpar )
        {
            ts.next(); // eat hash
            lhs = parseArray(scope,ts);
        }else
        {
            // lhs = parseSymbol(ts,inKeywordSequence);
            error("expecting '('", t.d_pos );
            return lhs.data();
        }
        break;
    case Lexer::Symbol:
        lhs = new Symbol( t.d_val, t.d_pos );
        ts.next();
        break;
    case Lexer::Lpar:
        ts.next(); // eat lpar
        lhs = parseExpression(scope,ts);
        t = ts.next();
        if( t.d_type != Lexer::Rpar )
        {
            error("expecting ')'", t.d_pos );
            return lhs.data();
        }
        break;
    case Lexer::Lbrack:
        lhs = parseBlock(scope,ts);
        break;
    default:
        error("invalid expression",t.d_pos);
        return 0;
    }

    t = ts.peek();

    Q_ASSERT( !lhs.isNull() );

    Ref<Cascade> casc;

    while( t.isValid() && ( isBinaryChar(t) || t.d_type == Lexer::Ident ) )
    {
        Ref<MsgSend> c;
        if( isBinaryChar(t) )
        {
            // binarySelector
            c = new MsgSend();
            c->d_pos = lhs->d_pos;
            c->d_inMethod = scope->getMethod();
            QByteArray str;
            const quint32 pos = t.d_pos;
            str.append(ts.next().d_val);
            t = ts.peek();
            while( isBinaryChar( t ) )
            {
                str += ts.next().d_val;
                t = ts.peek();
            }
            c->d_pattern << qMakePair(str,pos);
            Ref<Expression> e = parseExpression(scope,ts,false);
            if( e.isNull() )
                return c.data();
            c->d_args << e;
            c->d_patternType = Ast::BinaryPattern;
        }else if( t.d_type == Lexer::Ident )
        {
            if( ts.peek(2).d_type == Lexer::Colon )
            {
                // Keyword selector

                if( dontApplyKeywords )
                    return lhs.data();

                c = new MsgSend();
                c->d_pos = lhs->d_pos;
                c->d_inMethod = scope->getMethod();
                c->d_patternType = Ast::KeywordPattern;
                while( t.d_type == Lexer::Ident )
                {
                    if( ts.peek(2).d_type != Lexer::Colon )
                    {
                        error("invalid keyword call", t.d_pos );
                        return c.data();
                    }
                    c->d_pattern << qMakePair(t.d_val,t.d_pos);
                    ts.next();
                    ts.next();
                    Ref<Expression> e = parseExpression(scope,ts,true);
                    if( e.isNull() )
                        return c.data();
                    c->d_args << e;
                    t = ts.peek();
                }
            }else
            {
                // unary selector
                c = new MsgSend();
                c->d_pos = lhs->d_pos;
                c->d_inMethod = scope->getMethod();
                c->d_pattern << qMakePair(t.d_val,t.d_pos);
                c->d_patternType = Ast::UnaryPattern;
                ts.next();
            }
        } // else no call

        t = ts.peek();

        if( t.d_type == Lexer::Semi )
        {
            // start or continue a cascade
            ts.next(); // eat semi
            t = ts.peek();
            if( !isBinaryChar(t) && t.d_type != Lexer::Ident )
            {
                error("expecting selector after ';'", t.d_pos);
                return lhs.data();
            }
            Q_ASSERT( !c.isNull() );
            if( casc.isNull() )
            {
                casc = new Cascade();
                casc->d_pos = c->d_pos;
                c->d_receiver = lhs;
                casc->d_calls.append(c);
                lhs = casc.data();
            }else
            {
                Q_ASSERT( !casc.isNull() && !casc->d_calls.isEmpty() );
                c->d_receiver = casc->d_calls.first()->d_receiver;
                casc->d_calls.append(c);
            }
        }else if( !casc.isNull() )
        {
            // this is the last element of the cascade
            Q_ASSERT( !casc.isNull() && !casc->d_calls.isEmpty() );
            c->d_receiver = casc->d_calls.first()->d_receiver;
            casc->d_calls.append(c);
            casc = 0; // close the cascade
        }else
        {
            c->d_receiver = lhs;
            lhs = c.data();
        }
    }

    return lhs;
}

Ast::Ref<Expression> Parser::parseBlock(Ast::Function* outer,Parser::TokStream& ts)
{
    Lexer::Token t = ts.next();
    Q_ASSERT( t.d_type == Lexer::Lbrack );
    Ref<Block> b = new Block();
    b->d_pos = t.d_pos;
    b->d_func->d_owner = outer;
    parseBlockBody( b->d_func.data(), ts );
    return b.data();
}

Ast::Ref<Expression> Parser::parseArray(Ast::Function* scope,Parser::TokStream& ts)
{
    static const char* msg = "invalid array element";
    Lexer::Token t = ts.next();
    Q_ASSERT( t.d_type == Lexer::Lpar );
    Ref<ArrayLiteral> a = new ArrayLiteral();
    a->d_pos = t.d_pos;
    t = ts.peek();
    while( t.isValid() && t.d_type != Lexer::Rpar )
    {
        switch( t.d_type )
        {
        case Lexer::Number:
        case Lexer::Minus:
        case Lexer::String:
        case Lexer::Char:
            {
                Ref<Expression> e = parseExpression(scope, ts,true);
                if( e.isNull() )
                    return a.data();
                a->d_elements << e.data();
            }
            break;
        case Lexer::Ident:
            // TODO: these idents seem to be symbols, not real idents
            if( ts.peek(2).d_type == Lexer::Colon )
            {
                // Ref<Selector> s = new Selector();
                //s->d_pos = t.d_pos;
                //a->d_elements << s.data();
                // selector literal
                QByteArray str;
                while( t.isValid() && t.d_type == Lexer::Ident && ts.peek(2).d_type == Lexer::Colon )
                {
                    str += t.d_val + ":";
                    ts.next();
                    ts.next();
                    t = ts.peek();
                }
                // s->d_pattern = Lexer::getSymbol(str);
                a->d_elements << new Symbol( Lexer::getSymbol(str), t.d_pos );
            }else
            {
                // normal ident
                //a->d_elements << new Ident( t.d_val, t.d_pos );
                a->d_elements << new Symbol( t.d_val, t.d_pos );
                ts.next();
            }
            break;
        case Lexer::Hash:
            if( ts.peek(2).d_type == Lexer::Ident )
            {
                Ref<Expression> e = parseExpression(scope, ts);
                if( e.isNull() )
                    return a.data();
                a->d_elements << e;
            }else
            {
                ts.next();
                error( msg, t.d_pos );
                return a.data();
            }
            break;
        case Lexer::Lpar:
            {
                Ref<Expression> e = parseArray(scope,ts);
                if( e.isNull() )
                    return a.data();
                a->d_elements << e;
            }
            break;
        default:
            error( msg, t.d_pos );
            ts.next();
            return a.data();
        }
        t = ts.peek();
    }
    if( t.d_type != Lexer::Rpar )
        error( "non-terminated array literal", a->d_pos );
    ts.next(); // eat rpar
    return a.data();
}

Ast::Ref<Expression> Parser::parseAssig(Ast::Function* scope,Parser::TokStream& ts)
{
    Lexer::Token t = ts.next();
    Q_ASSERT( t.d_type == Lexer::Ident );
    Ref<Assig> a = new Assig();
    a->d_pos = t.d_pos;
    a->d_lhs << new Ident(t.d_val,t.d_pos, scope->getMethod() );
    ts.next(); // eat assig
    while( ts.peek(2).d_type == Lexer::Assig )
    {
        t = ts.next();
        if( t.d_type != Lexer::Ident )
        {
            error("can only assign to idents", t.d_pos );
            return a.data();
        }
        a->d_lhs << new Ident(t.d_val,t.d_pos, scope->getMethod() );
        ts.next(); // eat assig
    }
    a->d_rhs = parseExpression(scope, ts);
    return a.data();
}

Ast::Ref<Expression> Parser::parseReturn(Ast::Function* scope,Parser::TokStream& ts)
{
    Lexer::Token t = ts.next();
    Q_ASSERT( t.d_type == Lexer::Hat );
    Ref<Return> r = new Return();
    r->d_pos = t.d_pos;
    r->d_what = parseExpression(scope, ts);
    return r.data();
}

bool Parser::parseBlockBody(Function* block, Parser::TokStream& ts)
{
    // TODO: similar to parseMethodBody. Maybe unify?

    Lexer::Token t = ts.peek();
    while( t.isValid() && t.d_type == Lexer::Colon )
    {
        ts.next();
        t = ts.next();
        if( t.d_type != Lexer::Ident )
            return error("expecting ident in block argument declaration", t.d_pos );
        if( block->findVar(t.d_val) )
            return error("block argument names must be unique", t.d_pos );
        Ref<Variable> l = new Variable();
        l->d_kind = Variable::Argument;
        l->d_pos = t.d_pos;
        l->d_name = t.d_val;
        block->addVar(l.data());
        t = ts.peek();
    }
    if( t.d_type == Lexer::Bar )
    {
        ts.next();
        t = ts.peek();
    }
    while( !ts.atEnd() && t.isValid() )
    {
        switch( t.d_type )
        {
        case Lexer::Ident:
        case Lexer::Hash:
        case Lexer::Symbol:
        case Lexer::Lpar:
        case Lexer::Lbrack:
        case Lexer::Number:
        case Lexer::String:
        case Lexer::Char:
        case Lexer::Minus:
            {
                Ref<Expression> e = parseExpression(block, ts);
                if( !e.isNull() )
                    block->d_body.append(e);
            }
            break;
        case Lexer::Bar:
            /*
            if( localsAllowed )
            {
                localsAllowed = false;
                parseLocals( m, ts );
            }else*/
                return error("temp declaration not allowed here", ts.peek().d_pos );
            break;
                /*
        case Lexer::Lt:
            parsePrimitive(m,ts);
            break;
            */
        case Lexer::Hat:
            {
                Ref<Expression> e = parseReturn(block,ts);
                if( !e.isNull() )
                    block->d_body.append(e);
            }
            break;
        case Lexer::Dot:
            // NOP
            ts.next();
            break;
        case Lexer::Rbrack:
            ts.next();
            return true; // end of block
        default:
            return error("expecting statement", ts.peek().d_pos );
        }
        t = ts.peek();
    }
    return true;
}

Lexer::Token Parser::TokStream::next()
{
    if( d_pos < d_toks.size() )
        return d_toks[d_pos++];
    else
        return Lexer::Token();
}

Lexer::Token Parser::TokStream::peek(int la) const
{
    Q_ASSERT( la > 0 );
    if( ( d_pos + la - 1 ) < d_toks.size() )
        return d_toks[ d_pos + la - 1 ];
    else
        return Lexer::Token();
}
