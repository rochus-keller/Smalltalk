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

#include "StImageViewer.h"
#include "StObjectMemory.h"
#include <QAbstractItemModel>
#include <QApplication>
#include <QFileDialog>
#include <QTreeView>
#include <QtDebug>
using namespace St;

class ImageViewer::Model : public QAbstractItemModel
{
public:
    explicit Model(QTreeView *parent = 0):QAbstractItemModel(parent)
    {
        d_knowns.insert(65535,"objectMinusOne");
        d_knowns.insert(1,"objectZero");
        d_knowns.insert(3,"objectOne");
        d_knowns.insert(5,"objectTwo");
        d_knowns.insert(0x02,"nil");
        d_knowns.insert(0x04,"false");
        d_knowns.insert(0x06,"true");
        d_knowns.insert(0x08,"processor");
        d_knowns.insert(0x12,"smalltalk");

        d_knowns.insert(0x0c,"SmallInteger");
        d_knowns.insert(0x0e,"String");
        d_knowns.insert(0x10,"Array");
        d_knowns.insert(0x14,"Float");
        d_knowns.insert(0x16,"MethodContext");
        d_knowns.insert(0x18,"BlockContext");
        d_knowns.insert(0x1a,"Point");
        d_knowns.insert(0x1c,"LargePositiveInteger");
        d_knowns.insert(0x1e,"DisplayBitmap");
        d_knowns.insert(0x20,"Message");
        d_knowns.insert(0x22,"CompiledMethod");
        d_knowns.insert(0x26,"Semaphore");
        d_knowns.insert(0x28,"Character");

        d_knowns.insert(0x0a,"symbolTable");
        d_knowns.insert(0x2a,"symbolDoesNotUnderstand");
        d_knowns.insert(0x2c,"symbolCannotReturn");
        d_knowns.insert(0x2e,"symbolMonitor");
        d_knowns.insert(0x24,"symbolUnusedOop18");
        d_knowns.insert(0x34,"symbolMustBeBoolean");
        d_knowns.insert(0x30,"specialSelectors");
        d_knowns.insert(0x32,"characterTable");

        // empirical additions, not in the Blue Book
        d_knowns.insert(0x38, "Symbol");
        d_knowns.insert(0x84, "Association");
    }

    QTreeView* getParent() const
    {
        return static_cast<QTreeView*>(QObject::parent());
    }

    void setOm( ObjectMemory* om )
    {
        beginResetModel();
        d_root = Slot();
        d_om = om;
        fillTop();
        endResetModel();
    }

    quint16 getValue(const QModelIndex& index) const
    {
        if( !index.isValid() || d_om == 0 )
            return 0;
        Slot* s = static_cast<Slot*>( index.internalPointer() );
        Q_ASSERT( s != 0 );
        return s->d_oop;
    }

    // overrides
    int columnCount ( const QModelIndex & parent = QModelIndex() ) const { return 3; }

    QVariant headerData(int section, Qt::Orientation orientation, int role ) const
    {
        if( orientation == Qt::Horizontal && role ==  Qt::DisplayRole )
        {
            switch(section)
            {
            case 0:
                return "oop";
            case 1:
                return "class";
            case 2:
                return "value";
            }
        }
        return QVariant();
    }

    QVariant data ( const QModelIndex & index, int role = Qt::DisplayRole ) const
    {
        if( !index.isValid() || d_om == 0 )
            return QVariant();

        Slot* s = static_cast<Slot*>( index.internalPointer() );
        Q_ASSERT( s != 0 );
        switch( role )
        {
        case Qt::DisplayRole:
            switch( index.column() )
            {
            case 0:
                if( s->d_kind == Slot::Bytecode )
                    return "bytecode";
                else
                {
                    QByteArray str = d_knowns.value(s->d_oop);
                    if( !str.isEmpty() )
                        return str;
                    else if( s->d_kind == Slot::Continuation )
                        return "...";
                    else
                        return QByteArray::number( s->d_oop, 16 );
                }
                break;
            case 1:
                if( s->d_kind != Slot::Continuation && s->d_kind != Slot::Bytecode )
                {
                    const quint16 cls = d_om->fetchClassOf(s->d_oop);
                    QByteArray str = d_knowns.value(cls);
                    if( !str.isEmpty() )
                        return str;
                    else
                    {
                        const quint16 nameId = d_om->fetchWordOfObject(6,cls);
                        const quint16 nameCls = d_om->fetchClassOf(nameId);
                        if( nameCls == 0x38 )
                        {
                            str = (const char*)d_om->fetchByteString(nameId).d_bytes;
                            if( !str.isEmpty() )
                                return str;
                        }else if( nameCls == cls )
                        {
                            const quint16 nameId2 = d_om->fetchWordOfObject(6,s->d_oop);
                            const quint16 nameCls2 = d_om->fetchClassOf(nameId2);
                            if( nameCls2 == 0x38 )
                            {
                                str = (const char*)d_om->fetchByteString(nameId2).d_bytes;
                                if( !str.isEmpty() )
                                    return str + " class";
                            }
                        }
                        return QByteArray::number( cls, 16 );
                    }
                }
                break;
            case 2:
                {
                    switch( s->d_kind )
                    {
                    case Slot::String:
                        return "\"" + QByteArray((const char*)d_om->fetchByteString(s->d_oop).d_bytes) + "\"";
                    case Slot::Character:
                        {
                            quint16 ch = d_om->fetchWordOfObject(0,s->d_oop);
                            ch = ch >> 1;
                            if( ::isprint(ch) )
                                return "'" + QByteArray(1,ch) + "'";
                            else
                                return "0x" + QByteArray::number(ch,16);
                        }
                        break;
                    case Slot::Chunk:
                        {
                            QByteArray str((const char*)d_om->fetchByteString(s->d_oop).d_bytes);
                            const int len = 16;
                            if( str.size() > len )
                                return str.left(len).toHex() + "... (" + QByteArray::number(str.size()) + " bytes)";
                            else
                                return str.toHex() + " (" + QByteArray::number(str.size()) + " bytes)";
                        }
                        break;
                    case Slot::Bytecode:
                        {
                            QByteArray str((const char*)d_om->methodBytecodes(s->d_oop).d_bytes);
                            const int len = 16;
                            if( str.size() > len )
                                return str.left(len).toHex() + "... (" + QByteArray::number(str.size()) + " bytes)";
                            else
                                return str.toHex() + " (" + QByteArray::number(str.size()) + " bytes)";
                        }
                        break;
                    case Slot::Int:
                        return QString::number(ObjectMemory::toInt(s->d_oop));
                    case Slot::Continuation:
                        return QString("another %1 entries").arg(s->d_oop);
                    }
                }
            }
            break;
        case Qt::ToolTipRole:
            if( s->d_kind != Slot::Continuation && s->d_kind != Slot::Bytecode )
            {
                const quint16 cls = d_om->fetchClassOf(s->d_oop);
                switch( index.column() )
                {
                case 0:
                    return QString("object oop %1").arg(s->d_oop,0,16);
                case 1:
                    return QString("class oop %1").arg(cls,0,16);
                }
            }
            break;
        default:
            break;
        }
        return QVariant();
    }

    QModelIndex parent ( const QModelIndex & index ) const
    {
        if( index.isValid() )
        {
            Slot* s = static_cast<Slot*>( index.internalPointer() );
            Q_ASSERT( s != 0 );
            if( s->d_parent == &d_root )
                return QModelIndex();
            // else
            Q_ASSERT( s->d_parent != 0 );
            Q_ASSERT( s->d_parent->d_parent != 0 );
            return createIndex( s->d_parent->d_parent->d_children.indexOf( s->d_parent ), 0, s->d_parent );
        }else
            return QModelIndex();
    }

    int rowCount ( const QModelIndex & parent ) const
    {
        if( parent.isValid() )
        {
            Slot* s = static_cast<Slot*>( parent.internalPointer() );
            Q_ASSERT( s != 0 );
            return s->d_children.size();
        }else
            return d_root.d_children.size();
    }

    QModelIndex index ( int row, int column, const QModelIndex & parent ) const
    {
        const Slot* s = &d_root;
        if( parent.isValid() )
        {
            s = static_cast<Slot*>( parent.internalPointer() );
            Q_ASSERT( s != 0 );
        }
        if( row < s->d_children.size() && column < columnCount( parent ) )
            return createIndex( row, column, s->d_children[row] );
        else
            return QModelIndex();
    }

    bool hasChildren( const QModelIndex & parent ) const
    {
        if( d_om == 0 )
            return false;
        if( !parent.isValid() )
            return true;

        Slot* s = static_cast<Slot*>( parent.internalPointer() );
        Q_ASSERT( s != 0 );
        if( !s->d_children.isEmpty() )
            return true;

        switch( s->d_kind )
        {
        case Slot::String:
        case Slot::Character:
        case Slot::Chunk:
        case Slot::Int:
        case Slot::Continuation:
        case Slot::Bytecode:
            return false;
        }

        const quint16 size = d_om->fetchWordLenghtOf( s->d_oop );
        if( size == 0 )
            return false;

        const quint16 cls = d_om->fetchClassOf(s->d_oop);

        const_cast<Model*>(this)->fill(s, cls, size);
        return true;
    }

    Qt::ItemFlags flags( const QModelIndex & index ) const
    {
        Q_UNUSED(index)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable; //  | Qt::ItemIsDragEnabled;
    }

private:
    struct Slot
    {
        enum Kind { Frame, Int, String, Character, Chunk, Method, Continuation, Bytecode };
        quint16 d_oop;
        quint8 d_kind;
        QList<Slot*> d_children;
        Slot* d_parent;
        Slot(Slot* p = 0):d_parent(p),d_kind(Frame){ if( p ) p->d_children.append(this); }
        ~Slot() { foreach( Slot* s, d_children ) delete s; }
    };

    void setKind( Slot* s, quint16 cls )
    {
        switch( cls )
        {
        case ObjectMemory::classString:
        case 0x38: // Symbol
            s->d_kind = Slot::String;
            break;
        case ObjectMemory::classCharacter: // Char
            s->d_kind = Slot::Character;
            break;
        case ObjectMemory::classCompiledMethod:
            s->d_kind = Slot::Method;
            break;
        case ObjectMemory::classSmallInteger:
            s->d_kind = Slot::Int;
            break;
        default:
            if( !d_om->hasPointerMembers(s->d_oop) )
                s->d_kind = Slot::Chunk;
            else
            {
                int size = d_om->fetchWordLenghtOf(s->d_oop);
                if( size > 1000 )
                    qWarning() << "pointer object oop" << QByteArray::number(s->d_oop,16) << "with"
                               << size << "slots";
            }
            break;
        }
    }

    void fill(Slot* super, quint16 cls, quint16 size )
    {
        if( cls == ObjectMemory::classCompiledMethod )
        {
            for( int i = 0; i < d_om->methodLiteralCount(super->d_oop); i++ )
            {
                Slot* s = new Slot();
                s->d_parent = super;
                s->d_oop = d_om->methodLiteral(super->d_oop, i);
                setKind(s,d_om->fetchClassOf(s->d_oop));
                super->d_children.append( s );
            }
            Slot* s = new Slot();
            s->d_parent = super;
            s->d_oop = super->d_oop;
            s->d_kind = Slot::Bytecode;
            super->d_children.append( s );
        }else
        {
            const quint16 max = 50;
            Q_ASSERT( super->d_kind == Slot::Frame );
            for( int i = 0; i < qMin(size,max); i++ )
            {
                Slot* s = new Slot();
                s->d_parent = super;
                s->d_oop = d_om->fetchWordOfObject(i,super->d_oop);
                setKind(s,d_om->fetchClassOf(s->d_oop));
                super->d_children.append( s );
            }
            if( size > max )
            {
                Slot* s = new Slot();
                s->d_parent = super;
                s->d_oop = size - max;
                s->d_kind = Slot::Continuation;
                super->d_children.append( s );
            }
        }
    }

    void fillTop()
    {
        if( d_om == 0 )
            return;
        QList<quint16> oops = d_om->getAllValidOop();
        foreach( quint16 oop, oops )
        {
            quint16 cls = d_om->fetchClassOf(oop);
            if( cls == 0x38 || cls == 0x28 || cls == 0x0e || cls == 0x84 || cls == 0x14 || cls == 0xcb0
                    || cls == 0x1a || cls == 0x1c )
                continue; // no toplevel Symbol, String or Char, or Association, or Float, or Point, or Rectangle
            // or LargePositiveInteger
            Slot* s = new Slot();
            s->d_parent = &d_root;
            s->d_oop = oop;
            setKind(s, cls);
            d_root.d_children.append( s );

            //const quint16 size = d_om->fetchWordLenghtOf( s->d_oop );
            //fill( s, cls, size );
        }
    }

    Slot d_root;
    ObjectMemory* d_om;
    QHash<quint16,QByteArray> d_knowns;
};

ImageViewer::ImageViewer()
{
    d_om = new ObjectMemory(this);
    d_tree = new QTreeView(this);
    d_tree->setAlternatingRowColors(true);
    d_mdl = new Model(d_tree);
    d_tree->setModel(d_mdl);
    setCentralWidget(d_tree);

    showMaximized();
    setWindowTitle(tr("%1 %2").arg(qApp->applicationName()).arg(qApp->applicationVersion()));
}

bool ImageViewer::parse(const QString& path)
{
    QFile in(path);
    if( !in.open(QIODevice::ReadOnly) )
        return false;
    const bool res = d_om->readFrom(&in);
    d_mdl->setOm(d_om);
    return res;
}


int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationName("me@rochus-keller.ch");
    a.setOrganizationDomain("github.com/rochus-keller/Smalltalk");
    a.setApplicationName("Smalltalk 80 Image Viewer");
    a.setApplicationVersion("0.2");
    a.setStyle("Fusion");

    ImageViewer w;

    if( a.arguments().size() > 1 )
        w.parse( a.arguments()[1] );
    else
    {
        const QString path = QFileDialog::getOpenFileName(&w,ImageViewer::tr("Open Smalltalk-80 Image File"),
                                                          QString(), "VirtualImage *.image *.im" );
        if( path.isEmpty() )
            return 0;
        w.parse(path);
    }
    return a.exec();
}
