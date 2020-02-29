#ifndef ST_PARSER_H
#define ST_PARSER_H

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

#include <QObject>
#include <Smalltalk/StLexer.h>
#include <Smalltalk/StAstModel.h>

class QIODevice;

namespace St
{
class Parser
{
public:
    typedef QHash< const char*, Ast::Ref<Ast::Class> > Classes;

    struct Error
    {
        QByteArray d_msg;
        quint32 d_pos;
        Error( const QByteArray& msg, quint32 pos ):d_msg(msg),d_pos(pos){}
    };

    explicit Parser(Lexer*);

    bool readFile();
    bool readClass();
    // bool readExpression();

    const Classes& getClasses() const { return d_classes; }
    const QList<Error>& getErrs() const { return d_errs; }

    static void convertFile( QIODevice*, const QString& to );

protected:
    bool error( const QByteArray& msg, quint32 pos );
    bool readClassExpr();
    Ast::Ref<Ast::Method> readMethod(Ast::Class* c, bool classLevel);
    Ast::Class* getClass( const QByteArray& name, quint32 pos );
    bool addFields( const QByteArray&, bool classLevel, quint32 pos );

    struct TokStream
    {
        typedef QList<Lexer::Token> Toks;
        Toks d_toks;
        quint32 d_pos;
        TokStream(const Toks& t = Toks(), quint32 pos = 0 ):d_toks(t),d_pos(pos){}
        Lexer::Token next();
        Lexer::Token peek(int la = 1) const;
        bool atEnd() const { return d_pos >= d_toks.size(); }
    };

    bool parseMethodBody( Ast::Method*, TokStream& );
    bool parseLocals( Ast::Method*, TokStream& );
    bool parsePrimitive( Ast::Method*, TokStream& );
    Ast::Ref<Ast::Expression> parseExpression(Ast::Function*, TokStream&, bool dontApplyKeywords = false);
    Ast::Ref<Ast::Expression> parseBlock(Ast::Function*, TokStream&);
    Ast::Ref<Ast::Expression> parseArray(Ast::Function*, TokStream&);
    Ast::Ref<Ast::Expression> parseAssig(Ast::Function*, TokStream&);
    Ast::Ref<Ast::Expression> parseReturn(Ast::Function*, TokStream&);
    bool parseBlockBody(Ast::Function* block, TokStream& );
private:
    Lexer* d_lex;
    QByteArray subclass, comment, methodsFor, instanceVariableNames, classVariableNames,
        poolDictionaries, category, class_, variableSubclass, variableByteSubclass, variableWordSubclass, primitive;
    QList<Error> d_errs;
    Ast::Ref<Ast::Class> d_curClass;
    Classes d_classes;
};
}

#endif // ST_PARSER_H
