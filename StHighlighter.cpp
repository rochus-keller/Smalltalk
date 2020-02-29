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

#include "StHighlighter.h"
#include "StLexer.h"
using namespace St;

Highlighter::Highlighter(QTextDocument* parent) : QSyntaxHighlighter(parent)
{
    for( int i = 0; i < C_Max; i++ )
    {
        d_format[i].setFontWeight(QFont::Normal);
        d_format[i].setForeground(Qt::black);
        d_format[i].setBackground(Qt::transparent);
    }
    // see https://www.rapidtables.com/web/color/brown-color.html
    static const QColor darkGreen(7, 131, 7);
    static const QColor brown(165,42,42);
    static const QColor darkOrange(255,140,0);
    static const QColor darkRed(153, 0, 0);
    static const QColor chocolate(210,105,30);

    d_format[C_Num].setForeground(QColor(0, 153, 153));
    d_format[C_Str].setForeground(QColor(208, 16, 64));
    d_format[C_Cmt].setForeground(darkGreen); // QColor(51, 153, 51)); // QColor(153, 153, 136));
    d_format[C_Kw].setForeground(QColor(68, 85, 136));
    d_format[C_Kw].setFontWeight(QFont::Bold);
    d_format[C_Op].setForeground(darkRed);
    d_format[C_Op].setFontWeight(QFont::Bold);
    d_format[C_Type].setForeground(QColor(153, 0, 115));
    // d_format[C_Type].setFontWeight(QFont::Bold);
    d_format[C_Sym].setForeground(chocolate);

    d_keywords << "self" << "super" << "thisContext" << "true" << "false" << "nil";
}

void Highlighter::highlightBlock(const QString& text)
{
    const int previousBlockState_ = previousBlockState();
    int lexerState = 0, initialBraceDepth = 0;
    if (previousBlockState_ != -1) {
        lexerState = previousBlockState_ & 0xff;
        initialBraceDepth = previousBlockState_ >> 8;
    }

    int braceDepth = initialBraceDepth;


    int start = 0;
    if( lexerState == 1 )
    {
        // wir sind in einem Multi Line Comment
        // suche das Ende
        QTextCharFormat f = d_format[C_Cmt];
        int pos = text.indexOf("\"");
        if( pos == -1 ) // comments don't have embedded ""!
        {
            // the whole block ist part of the comment
            setFormat( start, text.size(), f );
            setCurrentBlockState( (braceDepth << 8) | lexerState);
            return;
        }else
        {
            // End of Comment found
            pos += 1;
            setFormat( start, pos , f );
            lexerState = 0;
            braceDepth--;
            start = pos;
        }
    }else if( lexerState == 2 )
    {
        // wir sind in einem Multi Line String
        // suche das Ende
        QTextCharFormat f = d_format[C_Str];
        int pos = text.indexOf("'");
        if( pos == -1 || ( ( pos + 1 ) < text.size() && text[pos+1] == '\'' ) )
        {
            // the whole block ist part of the string
            setFormat( start, text.size(), f );
            setCurrentBlockState( (braceDepth << 8) | lexerState);
            return;
        }else
        {
            // End of string found
            pos += 1;
            setFormat( start, pos , f );
            lexerState = 0;
            braceDepth--;
            start = pos;
        }
    }


    Lexer lex;
    lex.setFragMode(true);
    lex.setEatComments(false);

    QString text2 = text;
    text2.replace("←", "_" );
    text2.replace("↑", "^" );
    QList<Lexer::Token> tokens =  lex.tokens(text2.mid(start).toUtf8());
    for( int i = 0; i < tokens.size(); ++i )
    {
        Lexer::Token &t = tokens[i];
        t.d_pos += start;

        QTextCharFormat f;
        if( t.d_type == Lexer::LCmt )
        {
            braceDepth++;
            f = d_format[C_Cmt];
            lexerState = 1;
        }else if( t.d_type == Lexer::Comment )
            f = d_format[C_Cmt];
        else if( t.d_type == Lexer::LStr )
        {
            braceDepth++;
            f = d_format[C_Str];
            lexerState = 2;
        }else if( t.d_type == Lexer::String || t.d_type == Lexer::Char )
            f = d_format[C_Str];
        else if( t.d_type == Lexer::Number )
            f = d_format[C_Num];
        else if( t.d_type == Lexer::Symbol )
        {
            QTextCharFormat fb = d_format[C_Sym];
            fb.setFontWeight(QFont::Bold);
            setFormat( t.d_pos, 1, fb );
            t.d_pos++; t.d_len--;
            f = d_format[C_Sym];
        }else if( t.d_type >= Lexer::Colon && t.d_type <= Lexer::Rbrack )
            f = d_format[C_Op];
        else if( t.d_type == Lexer::Ident )
        {
            //f = d_format[C_Type];
            if( i + 1 < tokens.size() && tokens[i+1].d_type == Lexer::Colon )
                f = d_format[C_Type];
            else if( d_keywords.contains(t.d_val) )
                f = d_format[C_Kw];
            else
                f = d_format[C_Ident];
        }

        if( f.isValid() )
        {
            setFormat( t.d_pos, t.d_len, f );
        }
    }

    setCurrentBlockState((braceDepth << 8) | lexerState );
}

