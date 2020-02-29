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

#include "StLexer.h"
#include <QBuffer>
#include <QIODevice>
#include <QtDebug>
using namespace St;

//#define _ST_LEXER_SUPPORT_UNDERSCORE_IN_IDENTS_

const char* Lexer::s_typeName[] = {
    "Invalid", "Error", "EOF",
    "EOC",
    "Colon", "Bang", "Hat", "Hash", "Assig", "Tilde", "At", "Percent", "Ampers", "Star", "Minus", "Plus",
    "Eq", "Bar", "Bslash", "Lt", "Gt", "Comma", "Qmark", "Slash", "Dot", "Semi",
    "Lpar", "Rpar", "Lbrack", "Rbrack",
    "String", "Char", "Ident", "Number", "Comment", "LCmt", "LStr", "Symbol"
};

QHash<QByteArray,QByteArray> Lexer::d_symbols;

Lexer::Lexer() : d_in(0), d_eatComments(true), d_fragMode(false)
{

}

void Lexer::setDevice(QIODevice* in)
{
    d_in = in;
    d_line = 0;
}

Lexer::Token Lexer::next()
{
    Token t;
    if( !d_buffer.isEmpty() )
    {
        t = d_buffer.first();
        d_buffer.pop_front();
    }else
        t = nextImp();

    if( t.d_type == Lexer::Comment && d_eatComments )
        t = next();

    return t;
}

Lexer::Token Lexer::nextImp()
{
    if( d_in == 0 )
        return token(Error,"no input");
    if( d_in->atEnd() )
        return token(EoF);

    skipWhite();

    const char ch = get();
    switch( ch )
    {
    case 0:
        return token(EoF); // at the end of the file there are a couple of \0 which we ignore
    case '\'':
        return string();
    case '!':
        return token(Bang,ch); // we need the glyph for special chars
    case 12:
        return token(EoC); // there is a form feed character between each class
    case '"':
        return comment();
    case '_':
#ifdef _ST_LEXER_SUPPORT_UNDERSCORE_IN_IDENTS_
        return ident(ch);
#else
        return token(Assig,ch);
#endif
    case '~':
        return token(Tilde,ch);
    case '@':
        return token(At,ch);
    case '%':
        return token(Percent,ch);
    case '&':
        return token(Ampers,ch);
    case '*':
        return token(Star,ch);
    case '-':
        return token(Minus,ch);
    case '+':
        return token(Plus,ch);
    case '=':
        return token(Eq,ch);
    case '\\':
        return token(Bslash,ch);
    case '<':
        return token(Lt,ch);
    case '>':
        return token(Gt,ch);
    case ',':
        return token(Comma,ch);
    case '?':
        return token(Qmark,ch);
    case '/':
        return token(Slash,ch);
    case ':':
        return token(Colon);
    case ';':
        return token(Semi);
    case '#':
        return symbol();
    case '^':
        return token(Hat);
    case '|':
        return token(Bar,ch);
    case '.':
        return token(Dot);
    case '(':
        return token(Lpar);
    case ')':
        return token(Rpar);
    case '[':
        return token(Lbrack);
    case ']':
        return token(Rbrack);
    case '$':
        begin();
        return commit(Char,QByteArray(1,get()));

    }

    if( ::isalpha(ch) )
        return ident(ch);

    if( ::isdigit(ch) )
        return number(ch);

    return token(Error,"unexpected char");
}

Lexer::Token Lexer::peek(quint8 lookAhead)
{
    Q_ASSERT( lookAhead > 0 );
    while( d_buffer.size() < lookAhead )
        d_buffer.push_back( nextImp() );
    return d_buffer[ lookAhead - 1 ];
}

QList<Lexer::Token> Lexer::tokens(const QByteArray& code)
{
    QBuffer in;
    in.setData( code );
    in.open(QIODevice::ReadOnly);
    setDevice( &in );

    QList<Token> res;
    Token t = next();
    while( t.isValid() )
    {
        res << t;
        t = next();
    }
    return res;
}

char Lexer::get()
{
    char ch;
    d_pos = d_in->pos();
    if( !d_in->getChar(&ch) )
        return 0;
    if( ch == '\r' || ch == '\n' || ch == 12 ) // 12 = Form Feed
        d_line++;
    if( ch == '!' )
    {
        if( peekChar() == '!' )
            get(); // eat second '!' because doubled in chunks
    }
    return ch;
}

char Lexer::peekChar(int n)
{
    const int maxPeek = 4;
    if( n < 1 || n >= maxPeek )
        return 0;
    char buf[maxPeek];
    if( d_in->peek(buf,n) < n )
        return 0;
    return buf[n-1];
}

Lexer::Token Lexer::string()
{
    begin();
    QByteArray str;
    char ch = get();
    while( ch )
    {
        const char ch2 = peekChar();
        if( ch == '\'' && ch2 == '\'' )
        {
            ch = get();
            str.append(ch);
        }else
        {
            if( ch == '\'' )
                return commit(String,str);
            str.append(ch);
        }
        ch = get();
    }
    if( d_fragMode )
        return commit(LStr,str);
    return commit(Error,"non-terminated string");
}

Lexer::Token Lexer::comment()
{
    begin();
    QByteArray str;
    char ch = get();
    while( ch )
    {
        if( ch == '"' )
            return commit(Comment,str);
        str.append(ch);
        ch = get();
    }
    if( d_fragMode )
        return commit(LCmt,str);
    return commit(Error,"non-terminated comment");
}

Lexer::Token Lexer::symbol()
{
    begin();
    char ch = peekChar();
    if( ch == '(' )
    {
        return commit(Hash);
    }else if( isBinaryChar(ch) )
    {
        QByteArray str;
        str += get();
        while( isBinaryChar(peekChar()) )
            str += get();
        return commit(Symbol,getSymbol(str));
    }else if( ::isalpha(ch) )
    {
        QByteArray str;
        str += get();
        while( true )
        {
            const char c = peekChar();
            if( !::isalnum(c) && c != '_' && c != ':' )
                break;
            str += c;
            get();
        }
        if( str.contains(':') && !str.endsWith(':') )
            return commit(Error,"invalid symbol");
        else
            return commit(Symbol,getSymbol(str));
    }
#if 0
    // not used in original St80
    else if( ch == '\'' )
    {
        QByteArray str;
        str += get();
        ch = get();
        while( ch )
        {
            const char ch2 = peekChar();
            if( ch == '\'' && ch2 == '\'' )
            {
                ch = get();
                str.append(ch);
            }else
            {
                if( ch == '\'' )
                {
                    str += ch;
                    break;
                }
                str.append(ch);
            }
            ch = get();
        }
        return commit(Symbol,str);
    }
#endif
    return commit(Error,"invalid symbol");
}

Lexer::Token Lexer::ident(char ch)
{
    begin();
    QByteArray str;
    str += ch;
    while( true )
    {
        const char c = peekChar();
        if( !::isalnum(c)
#ifdef _ST_LEXER_SUPPORT_UNDERSCORE_IN_IDENTS_
                && c != '_'
#endif
                )
            break;
        str += c;
        get();
    }

#ifdef _ST_LEXER_SUPPORT_UNDERSCORE_IN_IDENTS_
    if( str == "_" && ::isspace(peekChar()) )
        return commit( Assig, str );
    else
#endif
        return commit( Ident, getSymbol(str) );
}

enum { Default, Decimal, Octal, Hex, Binary };

static bool checkDigit( quint8 kind, char ch )
{
    if( ::isdigit(ch) )
    {
        if( kind == Octal )
            return ( ch - '0' ) <= 7;
        if( kind == Binary )
            return ( ch - '0' ) <= 1;
        return true;
    }
    return ch >= 'A' && ch <= 'F';
}

Lexer::Token Lexer::number(char ch)
{
    begin();
    QByteArray str;
    str += ch;
    while( true )
    {
        ch = peekChar();
        if( !::isdigit(ch) )
            break;
        str += get();
    }
    quint8 kind = Default;
    if( ch == 'r' )
    {
        switch( str.toUInt() )
        {
        case 10:
            kind = Decimal;
            break;
        case 16:
            kind = Hex;
            break;
        case 8:
            kind = Octal;
            break;
        case 2:
            kind = Binary;
            break;
        default:
            return commit(Error,"invalid number format");
        }
        str += get();
    }
    if( kind != Default )
    {
        ch = peekChar();
        if( !checkDigit(kind,ch) && ch != '-' )
            return commit(Error,"invalid number format");
        str += get();
        if( ch == '-' )
        {
            ch = peekChar();
            if( !checkDigit(kind,ch) )
                return commit(Error,"invalid number format");
            str += get();
        }
        while( true )
        {
            ch = peekChar();
            if( !checkDigit(kind,ch) )
                break;
            str += get();
        }
    }
    if( ch == '.' )
    {
        const char ch2 = peekChar(2);
        if( ::isspace(ch2) || ch2 == '!' || ch2 == 0 )
            return commit(Number,str);
        str += get();
        ch = peekChar();
        if( !checkDigit(kind,ch) )
            return commit(Error,"invalid number format");
        str += get();
        while( true )
        {
            ch = peekChar();
            if( !checkDigit(kind,ch) )
                break;
            str += get();
        }
    }
    if( ch == 'e' )
    {
        str += get();
        ch = peekChar();
        if( !checkDigit(kind,ch) && ch != '-' )
            return commit(Error,"invalid number format");
        str += get();
        if( ch == '-' )
        {
            ch = peekChar();
            if( !checkDigit(kind,ch) )
                return commit(Error,"invalid number format");
            str += get();
        }
        while( true )
        {
            ch = peekChar();
            if( !checkDigit(kind,ch) )
                break;
            str += get();
        }
    }
    return commit(Number,str);
}

Lexer::Token Lexer::token(quint8 type, char ch)
{
    return token(type, QByteArray(1,ch));
}

Lexer::Token Lexer::token(quint8 type, const QByteArray& val)
{
    Token t(type,val);
    t.d_pos = d_pos;
    t.d_len = d_in->pos() - d_pos;
#ifdef _DEBUG
    t.d_line = d_line+1; // +1 so it fits with text editor
#endif
    return t;
}

void Lexer::begin()
{
    d_startPos = d_pos;
    d_startLine = d_line+1;
}

Lexer::Token Lexer::commit(quint8 type, const QByteArray& val)
{
    Token t(type,val);
    t.d_pos = d_startPos;
    t.d_len = d_in->pos() - d_startPos;
#ifdef _DEBUG
    t.d_line = d_startLine;
#endif
    return t;
}

void Lexer::skipWhite()
{
    while( ::isspace(peekChar()) )
        get();
}

QByteArray Lexer::getSymbol(const QByteArray& str)
{
    if( str.isEmpty() )
        return str;
    QByteArray& sym = d_symbols[str];
    if( sym.isEmpty() )
        sym = str;
    return sym;
}

bool Lexer::isBinaryTokType(quint8 t)
{
    switch( t )
    {
    case Lexer::Minus:
    case Lexer::Bang:
    case Lexer::Ampers:
    case Lexer::Star:
    case Lexer::Plus:
    case Lexer::Comma:
    case Lexer::Slash:
    case Lexer::Lt:
    case Lexer::Gt:
    case Lexer::Eq:
    case Lexer::Qmark:
    case Lexer::At:
    case Lexer::Bslash:
    case Lexer::Tilde:
    case Lexer::Bar:
        return true;
    default:
        return false;
    }
}

bool Lexer::isBinaryChar(char ch)
{
    switch( ch )
    {
    case '-':
    case '!':
    case '&':
    case '*':
    case '+':
    case ',':
    case '/':
    case '<':
    case '>':
    case '=':
    case '?':
    case '@':
    case '\\':
    case '~':
    case '|':
        return true;
    default:
        return false;
    }
}
