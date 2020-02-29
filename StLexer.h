#ifndef ST_LEXER_H
#define ST_LEXER_H

#include <QObject>
#include <QHash>

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

class QIODevice;

namespace St
{
class Lexer
{
public:
    enum TokenType { Invalid, Error, EoF,
                     EoC, // end of class (form feed)
                     Colon, Bang, Hat, Hash, Assig, Tilde, At, Percent, Ampers, Star, Minus, Plus,
                     Eq, Bar, Bslash, Lt, Gt, Comma, Qmark, Slash, Dot, Semi,
                     Lpar, Rpar, Lbrack, Rbrack,
                     String, Char, Ident, Number, Comment, LCmt, LStr, Symbol  };

    static const char* s_typeName[];

    struct Token
    {
        QByteArray d_val;
        quint32 d_pos;
#ifdef _DEBUG
        TokenType d_type;
        quint32 d_line;
#else
        quint8 d_type;
#endif
        quint16 d_len;

        Token():d_pos(0),d_type(Invalid),d_len(0){}
        Token(quint8 t, const QByteArray& v = QByteArray()):d_type(TokenType(t)),d_val(v),d_len(v.size()){}
        bool isValid() const { return d_type != Invalid && d_type != EoF && d_type != Error; }
        const char* typeName() const { return s_typeName[d_type]; }
    };

    explicit Lexer();
    void setDevice( QIODevice* );
    void setFragMode(bool on) { d_fragMode = on; }
    void setEatComments(bool on) { d_eatComments = on; }
    quint32 getLine() const { return d_line; }
    Token next();
    Token peek(quint8 lookAhead = 1);
    QList<Token> tokens( const QByteArray& code );
    static QByteArray getSymbol(const QByteArray& str);
    static bool isBinaryTokType( quint8 );
    static bool isBinaryChar( char );
protected:
    char get();
    char peekChar(int n = 1);
    Token string();
    Token comment();
    Token symbol();
    Token ident(char);
    Token number(char);
    Token token(quint8 type, char );
    Token token(quint8 type, const QByteArray& val = QByteArray() );
    Token nextImp();
    void begin();
    Token commit(quint8 type, const QByteArray& val = QByteArray() );
    void skipWhite();
private:
    QIODevice* d_in;
    quint32 d_pos;
    quint32 d_startPos;
    quint32 d_startLine;
    quint32 d_line;
    QList<Token> d_buffer;
    bool d_fragMode;
    bool d_eatComments;
    static QHash<QByteArray,QByteArray> d_symbols;
};
}

#endif // ST_LEXER_H
