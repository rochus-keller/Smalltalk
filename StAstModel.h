#ifndef ST_AST_MODEL_H
#define ST_AST_MODEL_H

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
#include <QSharedData>
#include <QHash>
#include <QMap>
#include <QSet>

class QIODevice;

namespace St
{
namespace Ast
{
    template <class T>
    struct Ref : public QExplicitlySharedDataPointer<T>
    {
        Ref(T* t = 0):QExplicitlySharedDataPointer<T>(t) {}
        bool isNull() const { return QExplicitlySharedDataPointer<T>::constData() == 0; }
    };

    struct MsgSend; struct Return; struct ArrayLiteral; struct Variable; struct Class; struct Method;
    struct Block; struct Variable; struct Cascade; struct Assig; struct Char; struct String;
    struct Number; struct Symbol; struct Ident; struct Selector; struct Scope; struct Named; struct Function;

    struct AstVisitor
    {
        virtual void visit( MsgSend* ) {}
        virtual void visit( Return* ) {}
        virtual void visit( ArrayLiteral* ) {}
        virtual void visit( Variable* ) {}
        virtual void visit( Class* ) {}
        virtual void visit( Method* ) {}
        virtual void visit( Block* ) {}
        virtual void visit( Cascade* ) {}
        virtual void visit( Assig* ) {}
        virtual void visit( Char* ) {}
        virtual void visit( String* ) {}
        virtual void visit( Number* ) {}
        virtual void visit( Symbol* ) {}
        virtual void visit( Ident* ) {}
        virtual void visit( Selector* ) {}
    };

    struct Thing : public QSharedData
    {
        enum Tag { T_Thing, T_Class, T_Variable, T_Method, T_Block, T_Return, T_Cascade, T_Func,
                   T_MsgSend, T_Assig, T_Array, T_Char, T_String, T_Number, T_Symbol, T_Ident, T_Selector,
                 T_MAX };

        quint32 d_pos;
        Thing():d_pos(0) {}
        virtual int getTag() const { return T_Thing; }
        virtual void accept(AstVisitor* v){}
        virtual quint32 getLen() const { return 0; }
    };

    struct Expression : public Thing
    {

    };
    typedef QList<Ref<Expression> > ExpList;

    struct Ident : public Expression
    {
        enum Use { Undefined, AssigTarget, MsgReceiver, Rhs, Declaration };

        QByteArray d_ident;
        Named* d_resolved;
        Method* d_inMethod;
        bool d_keyword;
        quint8 d_use;

        Ident( const QByteArray& ident, quint32 pos, Method* f = 0 ):d_ident(ident),d_resolved(0),
            d_use(Undefined),d_keyword(false),d_inMethod(f) { d_pos = pos; }
        int getTag() const { return T_Ident; }
        quint32 getLen() const { return d_ident.size(); }
        void accept(AstVisitor* v) { v->visit(this); }
    };

    struct Symbol : public Expression
    {
        QByteArray d_sym;

        Symbol( const QByteArray& sym, quint32 pos ):d_sym(sym) { d_pos = pos; }
        int getTag() const { return T_Symbol; }
        quint32 getLen() const { return d_sym.size(); }
        void accept(AstVisitor* v) { v->visit(this); }
    };

    struct Selector : public Expression
    {
        QByteArray d_pattern;
        int getTag() const { return T_Selector; }
        quint32 getLen() const { return d_pattern.size(); }
        void accept(AstVisitor* v) { v->visit(this); }
    };

    struct Number : public Expression
    {
        QByteArray d_num;
        Number( const QByteArray& num, quint32 pos ):d_num(num) { d_pos = pos; }
        int getTag() const { return T_Number; }
        quint32 getLen() const { return d_num.size(); }
        void accept(AstVisitor* v) { v->visit(this); }
    };

    struct String : public Expression
    {
        QByteArray d_str;
        String( const QByteArray& str, quint32 pos ):d_str(str) { d_pos = pos; }
        int getTag() const { return T_String; }
        quint32 getLen() const { return d_str.size() + 2; } // +2 because of ''
        void accept(AstVisitor* v) { v->visit(this); }
    };

    struct Char : public Expression
    {
        char d_ch;
        Char( char ch, quint32 pos ):d_ch(ch) { d_pos = pos; }
        int getTag() const { return T_Char; }
        quint32 getLen() const { return 2; } // 2 because of $ prefix
        void accept(AstVisitor* v) { v->visit(this); }
    };

    struct ArrayLiteral : public Expression
    {
        ExpList d_elements;
        int getTag() const { return T_Array; }
        void accept(AstVisitor* v) { v->visit(this); }
    };

    struct Assig : public Expression
    {
        QList<Ref<Ident> > d_lhs; // Ident designates a Variable
        Ref<Expression> d_rhs;
        int getTag() const { return T_Assig; }
        void accept(AstVisitor* v) { v->visit(this); }
    };

    enum PatternType { NoPattern, UnaryPattern, BinaryPattern, KeywordPattern };

    struct MsgSend : public Expression
    {
        quint8 d_patternType;
        QList< QPair<QByteArray,quint32> > d_pattern; // name, pos
        ExpList d_args;
        Ref<Expression> d_receiver;
        Method* d_inMethod;
        MsgSend():d_patternType(NoPattern),d_inMethod(0){}
        int getTag() const { return T_MsgSend; }
        void accept(AstVisitor* v) { v->visit(this); }
        QByteArray prettyName(bool withSpace = true) const;
    };

    struct Cascade : public Expression
    {
        // NOTE: all d_calls must point to the same d_receiver Expression!
        QList< Ref<MsgSend> > d_calls;
        int getTag() const { return T_Cascade; }
        void accept(AstVisitor* v) { v->visit(this); }
    };

    struct Return : public Expression
    {
        Ref<Expression> d_what;
        int getTag() const { return T_Return; }
        void accept(AstVisitor* v) { v->visit(this); }
    };

    struct Named : public Thing
    {
        QByteArray d_name;
        Scope* d_owner;
        Named():d_owner(0){}
        virtual bool classLevel() const { return false; }
    };

    struct Scope : public Named
    {
        QHash<const char*,QList<Named*> > d_varNames, d_methodNames;

        QList<Named*> findVars(const QByteArray& name, bool recursive = true ) const;
        QList<Named*> findMeths(const QByteArray& name, bool recursive = true ) const;
        Method* getMethod() const;
        Class* getClass() const;
    };

    struct GlobalScope : public Scope
    {
        QList< Ref<Variable> > d_vars;
    };

    struct Function : public Scope
    {
        QList< Ref<Variable> > d_vars;
        ExpList d_body;
        Variable* findVar( const QByteArray& ) const;
        int getTag() const { return T_Func; }
        void addVar(Variable*);
    };

    struct Block : public Expression
    {
        Ref<Function> d_func;
        Block();
        int getTag() const { return T_Block; }
        void accept(AstVisitor* v) { v->visit(this); }
    };

    struct Method : public Function
    {
        quint8 d_patternType;
        bool d_classLevel;
        quint8 d_primitive; // specified primitive id or zero
        QByteArrayList d_pattern; // compact form in d_name
        quint32 d_endPos;
        QByteArray d_category;
        ExpList d_helper;

        Method():d_patternType(NoPattern),d_classLevel(false),d_endPos(0),d_primitive(0){}
        static QByteArray prettyName(const QByteArrayList& pattern, quint8 kind, bool withSpace = true );
        QByteArray prettyName(bool withSpace = true) const;
        void updateName();
        Variable* findVar( const QByteArray& ) const;
        int getTag() const { return T_Method; }
        Expression* findByPos( quint32 ) const;
        void accept(AstVisitor* v) { v->visit(this); }
        bool classLevel() const { return d_classLevel; }
    };
    typedef Ref<Method> MethodRef;

    struct Variable : public Named
    {
        enum { InstanceLevel, ClassLevel, Argument, Temporary, Global };
        quint8 d_kind;
        Variable():d_kind(InstanceLevel){}
        int getTag() const { return T_Variable; }
        void accept(AstVisitor* v) { v->visit(this); }
        bool classLevel() const { return d_kind == ClassLevel; }
    };
    typedef Ref<Variable> VarRef;

    struct Class : public Scope
    {
        QByteArray d_superName;
        QByteArray d_category;
        QByteArray d_comment, d_classComment;

        QList< Ref<Variable> > d_vars;
        QList< Ref<Method> > d_methods;
        QList< Ref<Class> > d_subs;

        Method* findMethod(const QByteArray& ) const;
        Variable* findVar( const QByteArray& ) const;
        int getTag() const { return T_Class; }
        Class* getSuper() const;
        void addMethod(Method*);
        void addVar(Variable*);
        void accept(AstVisitor* v) { v->visit(this); }
    };
    typedef Ref<Class> ClassRef;
}

class Model : public QObject
{
    Q_OBJECT
public:
    typedef QMap<QByteArray, Ast::Ref<Ast::Class> > Classes;
    typedef QHash< const char*, Ast::Ref<Ast::Class> > Classes2;
    typedef QMap<QByteArray, QList<Ast::Class*> > ClassCats;
    typedef QHash<const char*, QList<Ast::Method*> > MethodXref;
    typedef QHash<const char*, QList<Ast::Variable*> > VariableXref;
    typedef QHash<quint16, QList<Ast::Method*> > PrimitiveXref;
    typedef QHash<Ast::Named*, QList<Ast::Ident*> > IdentXref;
    typedef QHash<const char*, QList<Ast::MsgSend*> > PatternXref;

    struct Error
    {
        QByteArray d_msg;
        quint32 d_pos;
        Error( const QByteArray& msg, quint32 pos ):d_msg(msg),d_pos(pos){}
    };

    explicit Model(QObject *parent = 0);

    bool parse( QIODevice* );
    void clear();

    const Classes& getClasses() const { return d_classes; }
    const ClassCats& getCats() const { return d_cats; }
    const MethodXref& getMxref() const { return d_mx; }
    const PrimitiveXref& getPxref() const { return d_px; }
    const IdentXref& getIxref() const { return d_ix; }
    const VariableXref& getVxref() const { return d_vx; }
    const PatternXref& getTxref() const { return d_tx; }
    const QList<Error>& getErrs() const { return d_errs; }
protected:
    void fillGlobals();
private:
    class ResolveIdents;
    QList<Error> d_errs;
    Classes d_classes;
    Classes2 d_classes2;
    ClassCats d_cats;
    MethodXref d_mx;
    VariableXref d_vx; // only global, instance and class vars
    PrimitiveXref d_px;
    IdentXref d_ix;
    PatternXref d_tx;
    QByteArray nil;
    Ast::GlobalScope d_globals;
    QSet<const char*> d_keywords;
};
}

Q_DECLARE_METATYPE( St::Ast::ClassRef )
Q_DECLARE_METATYPE( St::Ast::MethodRef )
Q_DECLARE_METATYPE( St::Ast::VarRef )

#endif // ST_AST_MODEL_H
