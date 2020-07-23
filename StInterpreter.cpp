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

#include "StDisplay.h"
#include "StInterpreter.h"
#include "StImageViewer.h"
#include <QtDebug>
#include <math.h>
#include <QDateTime> 
#include <QPainter>
#include <QEventLoop>
#include <QVBoxLayout>
using namespace St;

//#define ST_DO_TRACING
#define ST_DO_TRACE2
#define ST_TRACE3_PRIMITIVES
#define ST_TRACE_SYSTEM_ERRORS
//#define ST_DO_SCREEN_RECORDING

#ifdef ST_DO_TRACING
#ifdef ST_DO_TRACE2
#define ST_TRACE_BYTECODE(msg) qDebug() << "Bytecode" << ( "<" + QByteArray::number(currentBytecode) + ">" ).constData() \
    << "\t" << __FUNCTION__ << "\t" << msg;
#define ST_TRACE_PRIMITIVE(msg) qDebug() << "Primitive" << ( "#" + QByteArray::number(primitiveIndex)  ).constData() \
    << __FUNCTION__ << msg;
#define ST_TRACE_METHOD_CALL qDebug() << level << ( "[cycle=" + QByteArray::number(cycleNr) + "]" ).constData() << \
    memory->prettyValue(stackValue(argumentCount) ).constData() << \
    QByteArray::number(memory->getRegister(NewMethod),16).constData() << \
    memory->fetchByteArray(memory->getRegister(MessageSelector)).constData() << prettyArgs_().constData();
#define ST_RETURN_BYTECODE(msg)
#else
#define ST_TRACE_METHOD_CALL qDebug() << level /*<< QByteArray(level,'\t').constData() */ << \
    ( "[cycle=" + QByteArray::number(cycleNr) + "]" ).constData() << \
    memory->prettyValue(stackValue(argumentCount) ).constData() << \
    QByteArray::number(memory->getRegister(NewMethod),16).constData() << \
    memory->fetchByteArray(memory->getRegister(MessageSelector)).constData() << prettyArgs_().constData();
#define ST_RETURN_BYTECODE(msg) qDebug() << level /*<< QByteArray(level,'\t').constData() */ << \
    "^" << ( "<" + QByteArray::number(currentBytecode) + ">" ).constData() << msg;
#define ST_TRACE_BYTECODE(msg)
#ifdef ST_TRACE3_PRIMITIVES
#define ST_TRACE_PRIMITIVE(msg) qDebug() << level << "Primitive" << ( "#" + QByteArray::number(primitiveIndex)  ).constData() \
    << __FUNCTION__ << msg;
#else
#define ST_TRACE_PRIMITIVE(msg)
#endif
#endif
#else
#define ST_TRACE_BYTECODE(msg)
#define ST_TRACE_PRIMITIVE(msg)
#define ST_TRACE_METHOD_CALL
#define ST_RETURN_BYTECODE(msg)
#endif

Interpreter::Interpreter(QObject* p):QObject(p),memory(0),stackPointer(0),instructionPointer(0),
    newProcessWaiting(false),cycleNr(0), level(0), toSignal(0)
{
    d_timer.setSingleShot(true);
    connect( &d_timer, SIGNAL(timeout()), this, SLOT(onTimeout()) );
}

static ObjectMemory2::OOP findDisplay(ObjectMemory2* memory)
{
    ObjectMemory2::OOP sysdict = memory->fetchPointerOfObject(1, ObjectMemory2::smalltalk );
    for( int i = 1; i < memory->fetchWordLenghtOf( sysdict ); i++ )
    {
        ObjectMemory2::OOP assoc = memory->fetchPointerOfObject(i,sysdict);
        if( assoc != ObjectMemory2::objectNil )
        {
            ObjectMemory2::OOP sym = memory->fetchPointerOfObject(0,assoc);
            QByteArray name = memory->fetchByteArray(sym);
            if( name == "Display" )
                return memory->fetchPointerOfObject(1,assoc);
        }
    }
    return 0;
}

static Bitmap fetchBitmap( ObjectMemory2* memory, Interpreter::OOP form )
{
    if( form == ObjectMemory2::objectNil )
        return Bitmap();
    Interpreter::OOP bitmap = memory->fetchPointerOfObject(0,form);
    qint16 width = memory->integerValueOf(memory->fetchPointerOfObject(1,form));
    qint16 height = memory->integerValueOf(memory->fetchPointerOfObject(2,form));
    // Q_ASSERT( memory->fetchByteLenghtOf(bitmap) == width * height / 8 );
    ObjectMemory2::ByteString bs = memory->fetchByteString(bitmap);
    Q_ASSERT( bs.d_bytes != 0 );
    return Bitmap( const_cast<quint8*>(bs.d_bytes), bs.getWordLen(), width, height );
}

void Interpreter::setOm(ObjectMemory2* om)
{
    memory = om;

    if( om )
    {
        const OOP display = findDisplay(memory); // 0x340;
        Display::inst()->setBitmap( fetchBitmap(memory, display) );
    }else
        Display::inst()->setBitmap(Bitmap());
    disconnect( Display::inst(), SIGNAL(sigEventQueue()), this, SLOT(onEvent()) );
    connect( Display::inst(), SIGNAL(sigEventQueue()), this, SLOT(onEvent()) );
}

void Interpreter::interpret()
{
    cycleNr = 0;
    level = 0;
    const quint32 startTime = Display::inst()->getTicks();
    //Display::inst()->setLog(true); // TEST
    newActiveContext( firstContext() ); // BB: When Smalltalk is started up, ...

    // apparently the system is in ScreenController ->startUp->controlLoop->controlActivity->yellowButtonActivity
    //     SystenDictionary ->quit->saveAs:thenQuit:->snapshotAs:thenQuit:
    /*                  justSnapped & quitIfTrue
                            ifTrue:
                                [self quitPrimitive] "last action in 1983"
                            ifFalse:
                                [Delay postSnapshot. "first action in 2020"
                                DisplayScreen displayHeight: height.
                                self install].*/
    // top: BlockContext->newProcess, ControllManager->activeController: SystemDictionary->install

    while( Display::s_run ) // && cycleNr < 121000 ) // trace2 < 500 trace3 < 2000
    {
        cycle();
        Display::processEvents();
        if( Display::s_break )
            onBreak();
    }
    const quint32 endTime = Display::inst()->getTicks();
    qWarning() << "runtime [ms]:" << ( endTime - startTime );
}

qint16 Interpreter::instructionPointerOfContext(Interpreter::OOP contextPointer)
{
    return fetchIntegerOfObject(InstructionPointerIndex, contextPointer );
}

void Interpreter::storeInstructionPointerValueInContext(qint16 value, Interpreter::OOP contextPointer)
{
    storeIntegerOfObjectWithValue( InstructionPointerIndex, contextPointer, value );
}

qint16 Interpreter::stackPointerOfContext(Interpreter::OOP contextPointer)
{
    return fetchIntegerOfObject( StackPointerIndex, contextPointer );
}

void Interpreter::storeStackPointerValueInContext(qint16 value, Interpreter::OOP contextPointer)
{
    storeIntegerOfObjectWithValue( StackPointerIndex, contextPointer, value );
}

qint16 Interpreter::argumentCountOfBlock(Interpreter::OOP blockPointer)
{
    return fetchIntegerOfObject( BlockArgumentCountIndex, blockPointer );
}

bool Interpreter::isBlockContext(Interpreter::OOP contextPointer)
{
    const OOP methodOrArguments = memory->fetchPointerOfObject(MethodIndex, contextPointer);
    return memory->isIntegerObject(methodOrArguments);
}

void Interpreter::fetchContextRegisters()
{
    OOP activeContext = memory->getRegister(ActiveContext);
    OOP homeContext = 0;
    if( isBlockContext(activeContext) )
        homeContext = memory->fetchPointerOfObject(HomeIndex,activeContext);
    else
        homeContext = activeContext;
    memory->setRegister(HomeContext,homeContext);
    memory->setRegister(Receiver, memory->fetchPointerOfObject(ReceiverIndex,homeContext) );
    memory->setRegister(Method, memory->fetchPointerOfObject(MethodIndex,homeContext) );
    instructionPointer = instructionPointerOfContext(activeContext) - 1;
    stackPointer = stackPointerOfContext(activeContext) + TempFrameStart - 1;
}

void Interpreter::storeContextRegisters()
{
    OOP activeContext = memory->getRegister(ActiveContext);
    if( activeContext ) // deviation from BB since activeContext is null on first call
    {
        storeInstructionPointerValueInContext( instructionPointer + 1, activeContext );
        storeStackPointerValueInContext( stackPointer - TempFrameStart + 1, activeContext );
    }
}

void Interpreter::push(Interpreter::OOP object)
{
    if( object == 0 )
    {
        qWarning() << "WARNING: pushing zero oop to stack, replaced by nil";
        object = ObjectMemory2::objectNil;
    }
    stackPointer++;
    memory->storePointerOfObject( stackPointer, memory->getRegister(ActiveContext), object);
}

Interpreter::OOP Interpreter::popStack()
{
    OOP stackTop = memory->fetchPointerOfObject( stackPointer, memory->getRegister(ActiveContext) );
    stackPointer--;
    return stackTop;
}

Interpreter::OOP Interpreter::stackTop()
{
    return memory->fetchPointerOfObject( stackPointer, memory->getRegister(ActiveContext) );
}

Interpreter::OOP Interpreter::stackValue(qint16 offset)
{
    return memory->fetchPointerOfObject( stackPointer - offset, memory->getRegister(ActiveContext) );
}

void Interpreter::pop(quint16 number)
{
    stackPointer -= number;
}

void Interpreter::unPop(quint16 number)
{
    stackPointer += number;
}

void Interpreter::newActiveContext(Interpreter::OOP aContext)
{
    Q_ASSERT( aContext );
    storeContextRegisters();
    // decreaseReferencesTo: activeContext
    memory->setRegister(ActiveContext, aContext);
    // increaseReferencesTo: activeContext
    fetchContextRegisters();
}

Interpreter::OOP Interpreter::sender()
{
    return memory->fetchPointerOfObject(SenderIndex, memory->getRegister(HomeContext) );
}

Interpreter::OOP Interpreter::caller()
{
    return memory->fetchPointerOfObject(CallerIndex, memory->getRegister(ActiveContext) ); // BB states SenderIndex instead
}

Interpreter::OOP Interpreter::temporary(qint16 offset)
{
    return memory->fetchPointerOfObject( offset + TempFrameStart, memory->getRegister(HomeContext) );
}

Interpreter::OOP Interpreter::literal(qint16 offset)
{
    return memory->literalOfMethod( offset, memory->getRegister(Method) );
}

static inline quint16 _hash(Interpreter::OOP objectPointer)
{
    return objectPointer >> 1;
}

bool Interpreter::lookupMethodInDictionary(Interpreter::OOP dictionary)
{
    const int SelectorStart = 2;
    const int MethodArrayIndex = 1;
    OOP messageSelector = memory->getRegister(MessageSelector);
#if 0

    // Just a trivial linear scan; not the more fancy hash lookup described in the Blue Book
    int length = memory->fetchWordLenghtOf(dictionary);
    for( int index = SelectorStart; index < length; index++ )
    {
        OOP selector = memory->fetchPointerOfObject(index,dictionary);
        if( selector == messageSelector )
        {
            OOP methodArray = memory->fetchPointerOfObject(MethodArrayIndex,dictionary);
            OOP newMethod = memory->fetchPointerOfObject(index-SelectorStart,methodArray);
            memory->setRegister(NewMethod,newMethod);
            primitiveIndex = memory->primitiveIndexOf(newMethod);
            return true;
        }
    }
    return false;
#else
    // this version is about nine times faster than the linear version
    const quint16 length = memory->fetchWordLenghtOf(dictionary);
    const quint16 mask = length - SelectorStart - 1;
    quint16 index = ( mask & _hash(messageSelector) ) + SelectorStart;
    bool wrapAround = false;
    while( true )
    {
        OOP nextSelector = memory->fetchPointerOfObject(index, dictionary);
        if( nextSelector == ObjectMemory2::objectNil )
            return false;
        if( nextSelector == messageSelector )
        {
            const OOP methodArray = memory->fetchPointerOfObject(MethodArrayIndex, dictionary);
            OOP newMethod = memory->fetchPointerOfObject(index - SelectorStart, methodArray);
            memory->setRegister(NewMethod,newMethod);
            primitiveIndex = memory->primitiveIndexOf(newMethod);
            return true;
        }
        index = index + 1;
        if( index == length )
        {
            if( wrapAround )
                return false;
            wrapAround = true;
            index = SelectorStart;
        }
    }
#endif
}

bool Interpreter::lookupMethodInClass(Interpreter::OOP cls)
{
    OOP currentClass = cls;
    while( currentClass != ObjectMemory2::objectNil )
    {
        OOP dictionary = memory->fetchPointerOfObject(MessageDictionaryIndex, currentClass);
        if( lookupMethodInDictionary( dictionary ) )
        {
            //qDebug() << "found method" << memory->fetchByteArray(memory->getRegister(MessageSelector))
            //         << "in" << memory->fetchClassName(currentClass);
            return true;
        }
        currentClass = superclassOf(currentClass);
    }
    if( memory->getRegister(MessageSelector) == ObjectMemory2::symbolDoesNotUnderstand )
    {
        qCritical() << "ERROR: Recursive not understood error encountered";
        // BB self error:
        return false;
    }
    createActualMessage();
    OOP selector = memory->getRegister(MessageSelector);
    memory->setRegister(MessageSelector, ObjectMemory2::symbolDoesNotUnderstand );
    qCritical() << "ERROR: class" << memory->prettyValue(cls) << "doesNotUnderstand"
                << memory->prettyValue( selector );
    // exit(-1);
    return lookupMethodInClass(cls);
}

Interpreter::OOP Interpreter::superclassOf(Interpreter::OOP cls)
{
    if( cls == ObjectMemory2::objectNil )
    {
        qWarning() << "WARNING: asking for superclass of nil";
        return cls;
    }
    return memory->fetchPointerOfObject(SuperClassIndex,cls);
}

Interpreter::OOP Interpreter::instanceSpecificationOf(Interpreter::OOP classPointer)
{
    return memory->fetchPointerOfObject(InstanceSpecIndex,classPointer);
}

bool Interpreter::isPointers(Interpreter::OOP classPointer)
{
    return instanceSpecificationOf(classPointer) & 0x8000;
}

bool Interpreter::isWords(Interpreter::OOP classPointer)
{
    return instanceSpecificationOf(classPointer) & 0x4000;
}

bool Interpreter::isIndexable(Interpreter::OOP classPointer)
{
    return instanceSpecificationOf(classPointer) & 0x2000;
}

qint16 Interpreter::fixedFieldsOf(Interpreter::OOP classPointer)
{
    return ( ( instanceSpecificationOf(classPointer) >> 1 ) & 0x7ff );
}

quint8 Interpreter::fetchByte()
{
    Q_ASSERT( instructionPointer >= 0 );
    return memory->fetchByteOfObject(instructionPointer++, memory->getRegister(Method) );
}

void Interpreter::cycle()
{
    // not in BB:
    if( Display::s_copy )
    {
        Display::s_copy = false;
        OOP text = memory->fetchPointerOfObject(1, ObjectMemory2::currentSelection );
        if( text != ObjectMemory2::objectNil )
        {
            OOP string = memory->fetchPointerOfObject(0, text );
            if( string != ObjectMemory2::objectNil )
                Display::copyToClipboard( memory->fetchByteArray(string) );
        }
    }
    // end not in BB

    checkProcessSwitch();
    currentBytecode = fetchByte();
    cycleNr++;
    dispatchOnThisBytecode();
}

void Interpreter::BREAK(bool immediate)
{
    if( immediate )
        onBreak();
    else
        Display::s_break = true;
}

void Interpreter::onEvent()
{
    OOP sema = memory->getRegister(InputSemaphore);
    if( sema )
    {
        asynchronousSignal(sema);
    }else
        Display::inst()->nextEvent();
}

void Interpreter::onTimeout()
{
    // qWarning() << "onTimeout";
    if( toSignal )
        asynchronousSignal(toSignal);
}

void Interpreter::onBreak()
{
    memory->collectGarbage();
    QEventLoop loop;
    ImageViewer v;
    connect( &v, SIGNAL(sigClosing()), &loop, SLOT(quit()) );
    ImageViewer::Registers r;
    r["activeContext"] = memory->getRegister(ActiveContext);
    r["homeContext"] = memory->getRegister(HomeContext);
    r["method"] = memory->getRegister(Method);
    r["receiver"] = memory->getRegister(Receiver);
    r["messageSelector"] = memory->getRegister(MessageSelector);
    r["newMethod"] = memory->getRegister(NewMethod);
    r["newProcess"] = memory->getRegister(NewProcess);
    r["inputSemaphore"] = memory->getRegister(InputSemaphore);
    r["stackPointer"] = memory->integerObjectOf(stackPointer);
    r["instructionPointer"] = memory->integerObjectOf(instructionPointer);
    r["argumentCount"] = memory->integerObjectOf(argumentCount);
    r["primitiveIndex"] = memory->integerObjectOf(primitiveIndex);
    // r["cycleNumber"] = cycleNr;
    r["currentBytecode"] = memory->integerObjectOf(currentBytecode);
    r["success"] = success ? ObjectMemory2::objectTrue : ObjectMemory2::objectFalse;
    r["newProcessWaiting"] = newProcessWaiting ? ObjectMemory2::objectTrue : ObjectMemory2::objectFalse;

    v.show(memory, r);
    loop.exec();
    if( v.isNextStep() )
        qWarning() << "next step";
    else
    {
        Display::s_break = false;
        qWarning() << "break finished";
    }
}

void Interpreter::checkProcessSwitch()
{
    while( !semaphoreList.isEmpty() )
    {
        synchronousSignal( semaphoreList.back() );
        semaphoreList.pop_back();
    }
    if( newProcessWaiting )
    {
        newProcessWaiting = false;
        OOP activeProcess_ = activeProcess();
        if( activeProcess_ )
            memory->storePointerOfObject(SuspendedContextIndex, activeProcess_, memory->getRegister(ActiveContext));
        OOP scheduler = schedulerPointer();
        OOP newProcess = memory->getRegister(NewProcess);
        memory->storePointerOfObject(ActiveProcessIndex, scheduler, newProcess );
        newActiveContext( memory->fetchPointerOfObject( SuspendedContextIndex, newProcess ));
        memory->setRegister(NewProcess, 0);
    }
}

void Interpreter::dispatchOnThisBytecode()
{
    const quint8 b = currentBytecode;
    if( ( b >= 0 && b <= 119 ) || ( b >= 128 && b <= 130 ) || ( b >= 135 && b <= 137 ) )
        stackBytecode();
    else if( b >= 120 && b <= 127 )
        returnBytecode();
    else if( ( b >= 131 && b <= 134 ) || ( b >= 176 && b <= 255 ) )
        sendBytecode();
    else if( b >= 144 && b <= 175 )
        jumpBytecode();
    else if( b >= 138 && b <= 143 )
        qWarning() << "WARNING: running unused bytecode" << b;
}

bool Interpreter::stackBytecode()
{
    const quint8 b = currentBytecode;
    if( b >= 0 && b <= 15 )
        return pushReceiverVariableBytecode();
    if( b >= 16 && b <= 31 )
        return pushTemporaryVariableBytecode();
    if( b >= 32 && b <= 63 )
        return pushLiteralConstantBytecode();
    if( b >= 64 && b <= 95 )
        return pushLiteralVariableBytecode();
    if( b >= 96 && b <= 103 )
        return storeAndPopReceiverVariableBytecode();
    if( b >= 104 && b <= 111 )
        return storeAndPopTemporaryVariableBytecode();
    if( b == 112 )
        return pushReceiverBytecode();
    if( b >= 113 && b <= 119 )
        return pushConstantBytecode();
    if( b == 128 )
        return extendedPushBytecode();
    if( b == 129 )
        return extendedStoreBytecode(false);
    if( b == 130 )
        return extendedStoreAndPopBytecode();
    if( b == 135 )
        return popStackBytecode(false);
    if( b == 136 )
        return duplicateTopBytecode();
    if( b == 137 )
        return pushActiveContextBytecode();
    return false;
}

bool Interpreter::returnBytecode()
{
    switch( currentBytecode )
    {
    // "Return (receiver, true, false, nil) [%1] From Message").arg( b & 0x3 ), 1 );
    case 120:
        returnValue( memory->getRegister(Receiver), sender() );
        break;
    case 121:
        returnValue( ObjectMemory2::objectTrue, sender() );
        break;
    case 122:
        returnValue( ObjectMemory2::objectFalse, sender() );
        break;
    case 123:
        returnValue( ObjectMemory2::objectNil, sender() );
        break;
    // "Return Stack Top From (Message, Block) [%1]").arg( b & 0x1 ), 1 );
    case 124:
        returnValue( popStack(), sender() );
        break;
    case 125:
        returnValue( popStack(), caller() );
        break;
    // unused
    default:
        qWarning() << "WARNING: executing unused bytecode" << currentBytecode;
        return false;
    }
    return true;
}

bool Interpreter::sendBytecode()
{
    const quint8 b = currentBytecode;
    if( b >= 131 && b <= 134 )
        return extendedSendBytecode();

    if( b >= 176 && b <= 207 )
        return sendSpecialSelectorBytecode();

    if( b >= 208 && b <= 255 )
        return sendLiteralSelectorBytecode();
    return false;
}

bool Interpreter::jumpBytecode()
{
    const quint8 b = currentBytecode;
    if( b >= 144 && b <= 151 )
        return shortUnconditionalJump();
    if( b >= 152 && b <= 159 )
        return shortContidionalJump();
    if( b >= 160 && b <= 167 )
        return longUnconditionalJump();
    if( b >= 168 && b <= 175 )
        return longConditionalJump();
    return false;
}

bool Interpreter::pushReceiverVariableBytecode()
{
    OOP receiver = memory->getRegister(Receiver);
    ST_TRACE_BYTECODE("receiver:" << memory->prettyValue(receiver).constData());
    push( memory->fetchPointerOfObject( extractBits( 12, 15, currentBytecode ), receiver ) );
    // "Push Receiver Variable #%1").arg( b & 0xf ), 1 );
    return true;
}

bool Interpreter::pushTemporaryVariableBytecode()
{
    const quint16 var = extractBits( 12, 15, currentBytecode );
    const OOP val = temporary( var );
    ST_TRACE_BYTECODE( "variable:" << var << "value:" << memory->prettyValue(val).constData() );
    // "Push Temporary Location #%1").arg( b & 0xf ), 1 );
    push( val );
    return true;
}

bool Interpreter::pushLiteralConstantBytecode()
{
    const quint16 fieldIndex = extractBits( 11, 15, currentBytecode );
    const OOP literalConstant = literal( fieldIndex );
    ST_TRACE_BYTECODE( "literal:" << fieldIndex << "value:" << memory->prettyValue(literalConstant).constData() <<
                       "of method:" << QByteArray::number( memory->getRegister(Method), 16 ).constData() );
    // "Push Literal Constant #%1").arg( b & 0x1f ), 1 );
    push( literalConstant );
    return true;
}

static const quint16 ValueIndex = 1;

bool Interpreter::pushLiteralVariableBytecode()
{
    // "Push Literal Variable #%1").arg( b & 0x1f ), 1 );
    const quint16 fieldIndex = extractBits( 11, 15, currentBytecode );
    const OOP association = literal( fieldIndex );
    const OOP value = memory->fetchPointerOfObject( ValueIndex, association );
    ST_TRACE_BYTECODE("literal:" << fieldIndex << "value:" << memory->prettyValue(value).constData() <<
                      "of method:" << QByteArray::number( memory->getRegister(Method), 16 ).constData());
    push( value );
    return true;
}

bool Interpreter::storeAndPopReceiverVariableBytecode()
{
    // "Pop and Store Receiver Variable #%1").arg( b & 0x7 ), 1 );
    const quint16 variableIndex = extractBits( 13, 15, currentBytecode );
    OOP val = popStack();
    ST_TRACE_BYTECODE("var:" << variableIndex << "val:" << memory->prettyValue(val).constData() );
    memory->storePointerOfObject( variableIndex, memory->getRegister(Receiver), val );
    return true;
}

bool Interpreter::storeAndPopTemporaryVariableBytecode()
{
    // "Pop and Store Temporary Location #%1").arg( b & 0x7 ), 1 );
    const quint16 variableIndex = extractBits( 13, 15, currentBytecode );
    OOP val = popStack();
    ST_TRACE_BYTECODE("var:" << variableIndex << "val:" << memory->prettyValue(val).constData() );
    memory->storePointerOfObject( variableIndex + TempFrameStart, memory->getRegister(HomeContext), val );
    return true;
}

bool Interpreter::pushReceiverBytecode()
{
    OOP val = memory->getRegister(Receiver);
    ST_TRACE_BYTECODE("receiver:" << memory->prettyValue(val).constData());
    push( val );
    return true;
}

static const char* s_constNames[] =
{
    "???", "true", "false", "nil", "-1", "0", "1", "2"
};
bool Interpreter::pushConstantBytecode()
{
    // "Push (receiver, true, false, nil, -1, 0, 1, 2) [%1]").arg( b & 0x7 ), 1 );
    OOP val;
    switch( currentBytecode )
    {
    case 113:
        val = ObjectMemory2::objectTrue;
        break;
    case 114:
        val = ObjectMemory2::objectFalse;
        break;
    case 115:
        val = ObjectMemory2::objectNil;
        break;
    case 116:
        val = ObjectMemory2::objectMinusOne;
        break;
    case 117:
        val = ObjectMemory2::objectZero;
        break;
    case 118:
        val = ObjectMemory2::objectOne;
        break;
    case 119:
        val = ObjectMemory2::objectTwo;
        break;
    default:
        Q_ASSERT( false );
        break;
    }
    ST_TRACE_BYTECODE("val:" << memory->prettyValue(val).constData());
    push( val );
    return true;
}

bool Interpreter::extendedPushBytecode()
{
    // "Push (Receiver Variable, Temporary Location, Literal Constant, Literal Variable) [%1] #%2").
                              // arg( ( bc[pc+1] >> 6 ) & 0x3 ).arg( bc[pc+1] & 0x3f), 2 );
    const quint16 descriptor = fetchByte();
    const quint16 variableType = extractBits( 8, 9, descriptor );
    const quint16 variableIndex = extractBits( 10, 15, descriptor );
    OOP val;
    switch( variableType )
    {
    case 0:
        val = memory->fetchPointerOfObject( variableIndex, memory->getRegister(Receiver) );
        break;
    case 1:
        val = temporary( variableIndex );
        break;
    case 2:
        val = literal( variableIndex );
        break;
    case 3:
        val = memory->fetchPointerOfObject( ValueIndex, literal( variableIndex ) );
        break;
    default:
        Q_ASSERT( false );
    }
    ST_TRACE_BYTECODE("val:" << memory->prettyValue(val).constData());
    push(val);
    return true;
}

bool Interpreter::extendedStoreBytecode(bool subcall)
{
    if( !subcall )
    {
        ST_TRACE_BYTECODE("");
    }
    // "Store (Receiver Variable, Temporary Location, Illegal, Literal Variable) [%1] #%2").
                              // arg( ( bc[pc+1] >> 6 ) & 0x3 ).arg( bc[pc+1] & 0x3f), 2 );
    const quint16 descriptor = fetchByte();
    const quint16 variableType = extractBits( 8, 9, descriptor );
    const quint16 variableIndex = extractBits( 10, 15, descriptor );
    switch( variableType )
    {
    case 0:
        memory->storePointerOfObject(variableIndex,memory->getRegister(Receiver),stackTop());
        break;
    case 1:
        memory->storePointerOfObject(variableIndex+TempFrameStart,memory->getRegister(HomeContext),stackTop());
        break;
    case 2:
        qCritical() << "ERROR: illegal store";
        // BB: self error:
        break;
    case 3:
        memory->storePointerOfObject(ValueIndex, literal(variableIndex), stackTop() );
        break;
    default:
        Q_ASSERT( false );
    }
    return true;
}

bool Interpreter::extendedStoreAndPopBytecode()
{
    ST_TRACE_BYTECODE("");
    // "Pop and Store (Receiver Variable, Temporary Location, Illegal, Literal Variable) [%1] #%2").
                             // arg( ( bc[pc+1] >> 6 ) & 0x3 ).arg( bc[pc+1] & 0x3f), 2 );
    extendedStoreBytecode(true);
    return popStackBytecode(true);
}

bool Interpreter::popStackBytecode(bool subcall)
{
    if( !subcall )
    {
        ST_TRACE_BYTECODE("");
    }
    // "Pop Stack Top" ), 1 );
    popStack();
    return true;
}

bool Interpreter::duplicateTopBytecode()
{
    // "Duplicate Stack Top" ), 1 );
    OOP val = stackTop();
    ST_TRACE_BYTECODE("val:" << memory->prettyValue(val).constData());
    push( val );
    return true;
}

bool Interpreter::pushActiveContextBytecode()
{
    ST_TRACE_BYTECODE("");
    // "Push Active Context" ), 1 );
    push( memory->getRegister( ActiveContext ) );
    return true;
}

bool Interpreter::shortUnconditionalJump()
{
     // "Jump %1 + 1 (i.e., 1 through 8)").arg( b & 0x7 ), 1 );
    const qint16 offset = extractBits( 13, 15, currentBytecode );
    ST_TRACE_BYTECODE("offset:" << offset + 1 );
    jump( offset + 1 );
    return true;
}

bool Interpreter::shortContidionalJump()
{
    // "Pop and Jump 0n False %1 +1 (i.e., 1 through 8)").arg( b & 0x7 ), 1 );
    const qint16 offset = extractBits( 13, 15, currentBytecode );
    ST_TRACE_BYTECODE("offset:" << offset + 1 );
    jumpif( ObjectMemory2::objectFalse, offset + 1 );
    return true;
}

bool Interpreter::longUnconditionalJump()
{
    // "Jump(%1 - 4) *256+%2").arg( b & 0x7 ).arg( bc[pc+1] ), 2 );
    qint16 offset = extractBits( 13, 15, currentBytecode );
    offset = ( offset - 4 ) * 256 + fetchByte();
    ST_TRACE_BYTECODE("offset:" << offset );
    jump( offset );
    return true;
}

bool Interpreter::longConditionalJump()
{
    // "Pop and Jump On True %1 *256+%2").arg( b & 0x3 ).arg( bc[pc+1] ), 2 );
    // "Pop and Jump On False %1 *256+%2").arg( b & 0x3 ).arg( bc[pc+1] ), 2 );

    qint16 offset = extractBits( 14, 15, currentBytecode );
    offset = offset * 256 + fetchByte();
    ST_TRACE_BYTECODE("offset:" << offset );
    if( currentBytecode >= 168 && currentBytecode <= 171 )
        jumpif( ObjectMemory2::objectTrue, offset );
    if( currentBytecode >= 172 && currentBytecode <= 175 )
        jumpif( ObjectMemory2::objectFalse, offset );
    return true;
}

bool Interpreter::extendedSendBytecode()
{
    switch( currentBytecode )
    {
    case 131:
        return singleExtendedSendBytecode();
    case 132:
        return doubleExtendedSendBytecode();
    case 133:
        return singleExtendedSuperBytecode();
    case 134:
        return doubleExtendedSuperBytecode();
    default:
        Q_ASSERT(false);
        return false;
    }
}

bool Interpreter::singleExtendedSendBytecode()
{
    // "Send Literal Selector #%2 With %1 Arguments").arg( ( bc[pc+1] >> 5 ) & 0x7 ).arg( bc[pc+1] & 0x1f), 2 );
    const quint16 descriptor = fetchByte();
    const quint16 selectorIndex = extractBits( 11, 15, descriptor );
    //Q_ASSERT( selectorIndex == ( descriptor & 0x1f ) );
    const quint16 _argumentCount = extractBits( 8, 10, descriptor );
    //Q_ASSERT( tmp == ( ( descriptor >> 5 ) & 0x7 ) );
    OOP selector = literal(selectorIndex);
    ST_TRACE_BYTECODE("selector:"<< memory->prettyValue(selector).constData()
                      << "count:" << _argumentCount );
    sendSelector( selector, _argumentCount );
    return true;
}

bool Interpreter::doubleExtendedSendBytecode()
{
    // "Send Literal Selector #%2 With %1 Arguments").arg( bc[pc+1] ).arg( bc[pc+2]), 3 );
    const quint8 count = fetchByte();
    const OOP selector = literal( fetchByte() );
    ST_TRACE_BYTECODE("selector:"<< memory->prettyValue(selector).constData()
                      << "count:" << count );
    sendSelector( selector, count );
    return true;
}

bool Interpreter::singleExtendedSuperBytecode()
{
    // "Send Literal Selector #%2 To Superclass With %1 Arguments").arg( ( bc[pc+1] >> 5 ) & 0x7 ).arg( bc[pc+1] & 0x1f), 2 );
    const quint16 descriptor = fetchByte();
    argumentCount = extractBits( 8, 10, descriptor );
    const quint16 selectorIndex = extractBits( 11, 15, descriptor );
    const OOP selector = literal( selectorIndex );
    memory->setRegister( MessageSelector, selector);
    const OOP method = memory->getRegister(Method);
    const OOP methodClass = memory->methodClassOf( method );
    const OOP super = superclassOf(methodClass);
    ST_TRACE_BYTECODE("selector:"<< memory->prettyValue(selector).constData()
                      << "super:" << memory->prettyValue(super).constData() );
    sendSelectorToClass( super );
    return true;
}

bool Interpreter::doubleExtendedSuperBytecode()
{
    ST_TRACE_BYTECODE("");
    // "Send Literal Selector #%2 To Superclass With %1 Arguments").arg( bc[pc+1] ).arg( bc[pc+2]), 3 );
    argumentCount = fetchByte();
    const OOP selector = literal( fetchByte() );
    memory->setRegister( MessageSelector, selector);
    OOP methodClass = memory->methodClassOf( memory->getRegister(Method) );
    const OOP super = superclassOf(methodClass);
    ST_TRACE_BYTECODE("selector:"<< memory->prettyValue(selector).constData()
                      << "super:" << memory->prettyValue(super).constData() );
    sendSelectorToClass( super );
    return true;
}

bool Interpreter::sendSpecialSelectorBytecode()
{
    // see array 0x30 specialSelectors
    // "Send Arithmetic Message #%1" ).arg( b & 0xf ), 1 );
    // "Send Special Message #%1" ).arg( b & 0xf ), 1 );
    if( !specialSelectorPrimitiveResponse() )
    {
        const quint16 selectorIndex = ( currentBytecode - 176 ) * 2;
        OOP selector = memory->fetchPointerOfObject(selectorIndex, ObjectMemory2::specialSelectors );
        const quint16 count = fetchIntegerOfObject( selectorIndex + 1, ObjectMemory2::specialSelectors );
        ST_TRACE_BYTECODE("selector:"<< memory->prettyValue(selector).constData()
                          << "count:" << count );
        sendSelector( selector, count );
    }else
        ST_TRACE_BYTECODE("primitive");
    return true;
}

bool Interpreter::sendLiteralSelectorBytecode()
{
    // "Send Literal Selector #%1 With No Arguments" ).arg( b & 0xf ), 1 );
    // "Send Literal Selector #%1 With 1 Argument" ).arg( b & 0xf ), 1 );
    // "Send Literal Selector #%1 With 2 Arguments" ).arg( b & 0xf ), 1 );
    const quint16 litNr = extractBits( 12, 15, currentBytecode );
    const OOP selector = literal( litNr );
    const quint16 argumentCount = extractBits( 10, 11, currentBytecode ) - 1;
    ST_TRACE_BYTECODE("selector:"<< memory->prettyValue(selector).constData()
                      << "count:" << argumentCount );
    sendSelector( selector, argumentCount );
    return true;
}

void Interpreter::jump(qint32 offset)
{
    instructionPointer += offset;
}

void Interpreter::jumpif(quint16 condition, qint32 offset)
{
    const quint16 boolean = popStack();
    if( boolean == condition )
        jump(offset);
    else if( !( boolean == ObjectMemory2::objectTrue || boolean == ObjectMemory2::objectFalse ) )
    {
        unPop(1);
        sendMustBeBoolean();
    }
}

void Interpreter::sendSelector(Interpreter::OOP selector, quint16 count)
{
    memory->setRegister(MessageSelector, selector );
    argumentCount = count;
    OOP newReceiver = stackValue(argumentCount);
    if( newReceiver ) // deviation from BB
        sendSelectorToClass( memory->fetchClassOf(newReceiver) );
    else
    {
        qCritical() << "ERROR: sendSelector" << memory->fetchByteArray(selector) <<
                       "to zero receiver at stack slot" << stackPointer - count;
        dumpStack_("sendSelector");
    }
}

void Interpreter::sendSelectorToClass(Interpreter::OOP classPointer)
{
    // deviation from BB, we currently don't have a methodCache, original: findNewMethodInClass
    lookupMethodInClass(classPointer);
    executeNewMethod();
}

void Interpreter::executeNewMethod()
{
    ST_TRACE_METHOD_CALL;
    if( !primitiveResponse() )
        activateNewMethod();
}

bool Interpreter::primitiveResponse()
{
    if( primitiveIndex == 0 )
    {
        quint8 flagValue = memory->flagValueOf( memory->getRegister(NewMethod) );
        switch( flagValue )
        {
        case 5:
            // NOP quickReturnSelf();
            return true;
        case 6:
            quickInstanceLoad();
            return true;
        }
    }else
    {
        initPrimitive();
        dispatchPrimitives();
        return success;
    }
    return false;
}

void Interpreter::activateNewMethod()
{
    level++;
#ifdef ST_TRACE_SYSTEM_ERRORS
    if( memory->getRegister(MessageSelector ) == 0x11a ) // error:
    {
        qCritical() << "ERROR:" << memory->fetchByteArray(stackTop());
        // exit(-1);
    }
#endif
    quint16 contextSize = TempFrameStart;
    OOP newMethod = memory->getRegister(NewMethod);
    if( memory->largeContextFlagOf( newMethod ) )
        contextSize += 32;
    else
        contextSize += 12;
    OOP newContext = memory->instantiateClassWithPointers(ObjectMemory2::classMethodContext,contextSize);
    // qDebug() << "new MethodContext for method" << QByteArray::number(newMethod,16).constData() << "level" << level; // TEST
    OOP activeContext = memory->getRegister(ActiveContext);
    memory->storePointerOfObject(SenderIndex, newContext, activeContext );
    storeInstructionPointerValueInContext( memory->initialInstructionPointerOfMethod( newMethod ), newContext );
    storeStackPointerValueInContext( memory->temporaryCountOf( newMethod ), newContext );
    memory->storePointerOfObject(MethodIndex,newContext,newMethod);
    transfer( argumentCount + 1, stackPointer - argumentCount, activeContext, ReceiverIndex, newContext );
    pop( argumentCount + 1 );
    newActiveContext(newContext);
}

void Interpreter::transfer(quint32 count, quint16 firstFrom, Interpreter::OOP fromOop, quint16 firstTo, Interpreter::OOP toOop)
{
    quint16 fromIndex = firstFrom;
    const quint16 lastFrom = firstFrom + count;
    quint16 toIndex = firstTo;
    while( fromIndex < lastFrom )
    {
        OOP oop = memory->fetchPointerOfObject(fromIndex, fromOop);
        memory->storePointerOfObject(toIndex,toOop,oop);
        memory->storePointerOfObject(fromIndex,fromOop,ObjectMemory2::objectNil );
        fromIndex += 1;
        toIndex += 1;
    }
}

bool Interpreter::specialSelectorPrimitiveResponse()
{
    initPrimitive();
    if( currentBytecode >= 176 && currentBytecode <= 191 )
        arithmeticSelectorPrimitive();
    else if( currentBytecode >= 192 && currentBytecode <= 207 )
        commonSelectorPrimitive();
    return success;
}

void Interpreter::nilContextFields()
{
    memory->storePointerOfObject( SenderIndex, memory->getRegister( ActiveContext ), ObjectMemory2::objectNil );
    memory->storePointerOfObject( InstructionPointerIndex, memory->getRegister( ActiveContext ), ObjectMemory2::objectNil );
}

void Interpreter::returnToActiveContext(Interpreter::OOP aContext)
{
    memory->addTemp(aContext); // increaseReferencesTo: aContext
    nilContextFields();
    memory->setRegister(ActiveContext,aContext);
    memory->removeTemp(aContext); // decreaseReferencesTo: activeContext
    fetchContextRegisters();
}

void Interpreter::returnValue(Interpreter::OOP resultPointer, Interpreter::OOP contextPointer)
{
    OOP activeContext = memory->getRegister(ActiveContext);

    ST_TRACE_BYTECODE("result:" << memory->prettyValue(resultPointer).constData()
                      << "context:" << memory->prettyValue(contextPointer).constData() );

    //ST_RETURN_BYTECODE(memory->prettyValue(resultPointer).constData() << "from" <<
    //                   memory->prettyValue(activeContext).constData());

#ifdef ST_DO_TRACING
    if( memory->fetchClassOf(activeContext) != ObjectMemory2::classBlockContext )
        level--;
#endif

    if( contextPointer == ObjectMemory2::objectNil )
    {
        push( activeContext );
        push( resultPointer );
        sendSelector(ObjectMemory2::symbolCannotReturn, 1 );
    }
    OOP sendersIP = memory->fetchPointerOfObject( InstructionPointerIndex, contextPointer );
    if( sendersIP == ObjectMemory2::objectNil )
    {
        push( activeContext );
        push( resultPointer );
        sendSelector(ObjectMemory2::symbolCannotReturn, 1 );
    }
    memory->addTemp(resultPointer); // increaseReferencesTo: resultPointer
    returnToActiveContext(contextPointer);
    push( resultPointer );
    memory->removeTemp(resultPointer); // decreaseReferencesTo: resultPointer
}

void Interpreter::initPrimitive()
{
    success = true;
}

void Interpreter::successUpdate(bool res)
{
    success &= res;
}

Interpreter::OOP Interpreter::primitiveFail()
{
    success = false;
    return 0;
}

qint16 Interpreter::popInteger()
{
    OOP integerPointer = popStack();
    successUpdate( memory->isIntegerObject(integerPointer) );
    if( success )
        return memory->integerValueOf(integerPointer);
    else
        return 0;
}

void Interpreter::pushInteger(qint16 integerValue)
{
    push( memory->integerObjectOf(integerValue));
}

Interpreter::OOP Interpreter::positive16BitIntegerFor(quint16 integerValue)
{
    if( extractBits( 0, 1, integerValue ) == 0 ) // BB error, fixed like VIM
        return memory->integerObjectOf(integerValue);

    OOP newLargeInteger = memory->instantiateClassWithBytes(ObjectMemory2::classLargePositiveInteger, 2);

    memory->storeByteOfObject( 0, newLargeInteger, lowByteOf( integerValue ) );
    memory->storeByteOfObject( 1, newLargeInteger, highByteOf( integerValue ) );
    return newLargeInteger;
}

quint16 Interpreter::positive16BitValueOf(OOP integerPointer)
{
    if( memory->isIntegerObject(integerPointer) )
        return memory->integerValueOf(integerPointer);

    if( memory->fetchClassOf(integerPointer) != ObjectMemory2::classLargePositiveInteger )
        return primitiveFail();

    if( memory->fetchByteLenghtOf(integerPointer) != 2 )
        return primitiveFail();

    quint16 value = memory->fetchByteOfObject(1, integerPointer);
    value = value * 256 + memory->fetchByteOfObject(0, integerPointer);
    return value;
}

void Interpreter::arithmeticSelectorPrimitive()
{
    successUpdate( memory->isIntegerObject( stackValue(1) ) );
    if( !success )
        return;
    switch( currentBytecode )
    {
    case 176:
        primitiveAdd();
        break;
    case 177:
        primitiveSubtract();
        break;
    case 178:
        primitiveLessThan();
        break;
    case 179:
        primitiveGreaterThan();
        break;
    case 180:
        primitiveLessOrEqual();
        break;
    case 181:
        primitiveGreaterOrEqual();
        break;
    case 182:
        primitiveEqual();
        break;
    case 183:
        primitiveNotEqual();
        break;
    case 184:
        primitiveMultiply();
        break;
    case 185:
        primitiveDivide();
        break;
    case 186:
        primitiveMod();
        break;
    case 187:
        primitiveMakePoint();
        break;
    case 188:
        primitiveBitShift();
        break;
    case 189:
        primitiveDiv();
        break;
    case 190:
        primitiveBitAnd();
        break;
    case 191:
        primitiveBitOr();
        break;
    }
}

void Interpreter::commonSelectorPrimitive()
{
    argumentCount = fetchIntegerOfObject( (currentBytecode - 176) * 2 + 1, ObjectMemory2::specialSelectors );
    OOP receiverClass = memory->fetchClassOf( stackValue( argumentCount ) );
    switch( currentBytecode )
    {
    case 198:
        primitiveEquivalent();
        break;
    case 199:
        primitiveClass();
        break;
    case 200:
        successUpdate( receiverClass == ObjectMemory2::classMethodContext ||
                       receiverClass == ObjectMemory2::classBlockContext );
        if( success )
            primitiveBlockCopy();
        break;
    case 201:
    case 202:
        successUpdate( receiverClass == ObjectMemory2::classBlockContext );
        if( success )
            primitiveValue();
        break;
    default:
        primitiveFail();
        break;
    }
}

void Interpreter::primitiveAdd()
{
    ST_TRACE_PRIMITIVE("");
    _addSubMulImp('+');
}

void Interpreter::primitiveSubtract()
{
    ST_TRACE_PRIMITIVE("");
    _addSubMulImp('-');
}

void Interpreter::primitiveLessThan()
{
    ST_TRACE_PRIMITIVE("");
    _compareImp('<');
}

void Interpreter::primitiveGreaterThan()
{
    ST_TRACE_PRIMITIVE("");
    _compareImp('>');
}

void Interpreter::primitiveLessOrEqual()
{
    ST_TRACE_PRIMITIVE("");
    _compareImp('l');
}

void Interpreter::primitiveGreaterOrEqual()
{
    ST_TRACE_PRIMITIVE("");
    _compareImp('g');
}

void Interpreter::primitiveEqual()
{
    ST_TRACE_PRIMITIVE("");
    _compareImp('=');
}

void Interpreter::primitiveNotEqual()
{
    ST_TRACE_PRIMITIVE("");
    _compareImp('!');
}

void Interpreter::primitiveMultiply()
{
    ST_TRACE_PRIMITIVE("");
    _addSubMulImp('*');
}

void Interpreter::primitiveDivide()
{
    ST_TRACE_PRIMITIVE("");
    const qint16 integerArgument = popInteger();
    const qint16 integerReceiver = popInteger();
    successUpdate( integerArgument != 0 );
    successUpdate( integerArgument != 0 && integerReceiver % integerArgument == 0 );
    qint16 integerResult = 0;
    if( success )
    {
        integerResult = integerReceiver / integerArgument;
        successUpdate( memory->isIntegerValue(integerResult) );
    }
    if( success )
        push( memory->integerObjectOf( integerResult ) );
    else
        unPop(2);
}

static int MOD(int a, int b) // Source: http://lists.inf.ethz.ch/pipermail/oberon/2019/013353.html
{
    Q_ASSERT( b > 0 );
    if (a < 0)
        return (b - 1) + ((a - b + 1)) % b;
    else
        return a % b;
}

void Interpreter::primitiveMod()
{
    ST_TRACE_PRIMITIVE("");
    const qint16 integerArgument = popInteger();
    const qint16 integerReceiver = popInteger();
    successUpdate( integerArgument != 0 );
    qint16 integerResult = 0;
    if( success )
    {
        integerResult = MOD(integerReceiver,integerArgument);
        successUpdate( memory->isIntegerValue(integerResult) );
    }
    if( success )
        pushInteger( integerResult );
    else
        unPop(2);
}

void Interpreter::primitiveMakePoint()
{
    ST_TRACE_PRIMITIVE("");

    OOP integerArgument = popStack();
    OOP integerReceiver = popStack();
    successUpdate( memory->isIntegerObject(integerArgument) ); // BB error; apparently arg is an OOP, see also primitiveMousePoint
    successUpdate( memory->isIntegerObject(integerReceiver) );

    if( success )
    {
        OOP pointResult = memory->instantiateClassWithPointers( ObjectMemory2::classPoint, ClassPointSize );
        memory->storePointerOfObject( XIndex, pointResult, integerReceiver );
        memory->storePointerOfObject( YIndex, pointResult, integerArgument );
        push( pointResult );
    }
    else
    {
        // qDebug() << memory->prettyValue(integerArgument) << memory->prettyValue(integerReceiver);
        // everytime we arrived here so far integerArgument and integerReceiver were Float
        unPop(2);
    }
}

void Interpreter::primitiveBitShift()
{
    ST_TRACE_PRIMITIVE("");
    const qint16 integerArgument = popInteger();
    const qint16 integerReceiver = popInteger();

    qint16 integerResult = 0;
    if( success )
    {
        integerResult = ObjectMemory2::bitShift( integerReceiver, integerArgument );
        successUpdate( memory->isIntegerValue(integerResult) );
    }
    if( success )
        pushInteger( integerResult );
    else
        unPop(2);
}

static int DIV(int a, int b) // Source: http://lists.inf.ethz.ch/pipermail/oberon/2019/013353.html
{
    if (a < 0)
        return (a - b + 1) / b;
    else
        return a / b;
}

void Interpreter::primitiveDiv()
{
    ST_TRACE_PRIMITIVE("");
    const qint16 integerArgument = popInteger();
    const qint16 integerReceiver = popInteger();
    successUpdate( integerArgument != 0 );
    qint16 integerResult = 0;
    if( success )
    {
        integerResult = DIV( integerReceiver, integerArgument );
        successUpdate( memory->isIntegerValue(integerResult) );
    }
    if( success )
        pushInteger( integerResult );
    else
        unPop(2);
}

void Interpreter::primitiveBitAnd()
{
    ST_TRACE_PRIMITIVE("");
    _bitImp('&');
}

void Interpreter::primitiveBitOr()
{
    ST_TRACE_PRIMITIVE("");
    _bitImp('|');
}

qint16 Interpreter::fetchIntegerOfObject(quint16 fieldIndex, Interpreter::OOP objectPointer)
{
    OOP integerPointer = memory->fetchPointerOfObject(fieldIndex,objectPointer);
    if( memory->isIntegerObject(integerPointer) )
        return memory->integerValueOf(integerPointer);
    else
        return primitiveFail();
}

void Interpreter::storeIntegerOfObjectWithValue(quint16 fieldIndex, Interpreter::OOP objectPointer, int integerValue)
{
    if( memory->isIntegerValue( integerValue ) )
    {
        OOP integerPointer = memory->integerObjectOf(integerValue);
        memory->storePointerOfObject(fieldIndex,objectPointer,integerPointer);
    }else
        primitiveFail();
}

void Interpreter::primitiveEquivalent()
{
    ST_TRACE_PRIMITIVE("");
    OOP otherObject = popStack();
    OOP thisObject = popStack();
    if( thisObject == otherObject )
        push( ObjectMemory2::objectTrue );
    else
        push( ObjectMemory2::objectFalse );
}

void Interpreter::primitiveClass()
{
    ST_TRACE_PRIMITIVE("");
    OOP instance = popStack();
    push( memory->fetchClassOf(instance) );
}

void Interpreter::primitiveBlockCopy()
{
    ST_TRACE_PRIMITIVE("");
    OOP blockArgumentCount = popStack();
    OOP context = popStack();
    OOP methodContext = 0;
    if( isBlockContext(context) )
        methodContext = memory->fetchPointerOfObject(HomeIndex,context);
    else
        methodContext = context;
    int contextSize = memory->fetchWordLenghtOf(methodContext);
    OOP newContext = memory->instantiateClassWithPointers( ObjectMemory2::classBlockContext, contextSize );
    OOP initialIP = memory->integerObjectOf(instructionPointer+3);
    memory->storePointerOfObject(InitialIPIndex, newContext, initialIP);
    memory->storePointerOfObject(InstructionPointerIndex, newContext, initialIP);
    storeStackPointerValueInContext(0,newContext);
    memory->storePointerOfObject(BlockArgumentCountIndex, newContext, blockArgumentCount );
    memory->storePointerOfObject(HomeIndex, newContext, methodContext );
    push(newContext);
}

void Interpreter::primitiveValue()
{
    ST_TRACE_PRIMITIVE("");
    OOP blockContext = stackValue(argumentCount);
    OOP blockArgumentCount = argumentCountOfBlock(blockContext);
    successUpdate( argumentCount == blockArgumentCount );
    if( success )
    {
        transfer(argumentCount, stackPointer-argumentCount+1,
                 memory->getRegister(ActiveContext), TempFrameStart, blockContext );
        pop(argumentCount+1);
        OOP initialIP = memory->fetchPointerOfObject(InitialIPIndex, blockContext);
        memory->storePointerOfObject(InstructionPointerIndex, blockContext, initialIP);
        storeStackPointerValueInContext(argumentCount, blockContext);
        memory->storePointerOfObject(CallerIndex, blockContext, memory->getRegister(ActiveContext) );
        newActiveContext(blockContext);
    }
}

void Interpreter::quickInstanceLoad()
{
    OOP thisReceiver = popStack();
    quint16 fieldIndex = memory->fieldIndexOf( memory->getRegister(NewMethod) );
    OOP val = memory->fetchPointerOfObject( fieldIndex, thisReceiver );
    // qDebug() << "quickInstanceLoad index" << fieldIndex << "value" << memory->prettyValue(val).constData();
    push( val );
}

void Interpreter::dispatchPrimitives()
{
    if( primitiveIndex < 60 )
        dispatchArithmeticPrimitives();
    else if( primitiveIndex < 68 )
        dispatchSubscriptAndStreamPrimitives();
    else if( primitiveIndex < 80 )
        dispatchStorageManagementPrimitives();
    else if( primitiveIndex < 90 )
        dispatchControlPrimitives();
    else if( primitiveIndex < 110 )
        dispatchInputOutputPrimitives();
    else if( primitiveIndex < 128 )
        dispatchSystemPrimitives();
    else if( primitiveIndex < 256 )
        dispatchPrivatePrimitives();
    else
        primitiveFail();
}

void Interpreter::dispatchArithmeticPrimitives()
{
    if( primitiveIndex < 20 )
        dispatchIntegerPrimitives();
    else if( primitiveIndex < 40 )
        dispatchLargeIntegerPrimitives();
    else if( primitiveIndex < 60 )
        dispatchFloatPrimitives();
    else
        primitiveFail();
}

void Interpreter::dispatchSubscriptAndStreamPrimitives()
{
    switch( primitiveIndex )
    {
    case 60:
        primitiveAt();
        break;
    case 61:
        primitiveAtPut();
        break;
    case 62:
        primitiveSize();
        break;
    case 63:
        primitiveStringAt();
        break;
    case 64:
        primitiveStringAtPut();
        break;
    case 65:
        primitiveNext();
        break;
    case 66:
        primitiveNextPut();
        break;
    case 67:
        primitiveAtEnd();
        break;
    default:
        primitiveFail();
        break;
    }
}

void Interpreter::dispatchStorageManagementPrimitives()
{
    switch( primitiveIndex )
    {
    case 68:
        primitiveObjectAt();
        break;
    case 69:
        primitiveObjectAtPut();
        break;
    case 70:
        primitiveNew();
        break;
    case 71:
        primitiveNewWithArg();
        break;
    case 72:
        primitiveBecome();
        break;
    case 73:
        primitiveInstVarAt();
        break;
    case 74:
        primitiveInstVarAtPut();
        break;
    case 75:
        primitiveAsOop();
        break;
    case 76:
        primitiveAsObject();
        break;
    case 77:
        primitiveSomeInstance();
        break;
    case 78:
        primitiveNextInstance();
        break;
    case 79:
        primitiveNewMethod();
        break;
    default:
        primitiveFail();
        break;
    }
}

void Interpreter::dispatchControlPrimitives()
{
    switch( primitiveIndex )
    {
    case 80:
        primitiveBlockCopy();
        break;
    case 81:
        primitiveValue();
        break;
    case 82:
        primitiveValueWithArgs();
        break;
    case 83:
        primitivePerform();
        break;
    case 84:
        primitivePerformWithArgs();
        break;
    case 85:
        primitiveSignal();
        break;
    case 86:
        primitiveWait();
        break;
    case 87:
        primitiveResume();
        break;
    case 88:
        primitiveSuspend();
        break;
    case 89:
        primitiveFlushCache();
        break;
    default:
        primitiveFail();
        break;
    }
}

void Interpreter::dispatchInputOutputPrimitives()
{
    switch( primitiveIndex )
    {
    case 90:
        primitiveMousePoint();
        break;
    case 91:
        primitiveCursorLocPut();
        break;
    case 92:
        primitiveCursorLink();
        break;
    case 93:
        primitiveInputSemaphore();
        break;
    case 94:
        primitiveSamleInterval();
        break;
    case 95:
        primitiveInputWord();
        break;
    case 96:
        primitiveCopyBits();
        break;
    case 97:
        //primitiveSnapshot();
        qWarning() << "WARNING: primitiveSnapshot not yet implemented";
        //primitiveFail();
        break;
    case 98:
        primitiveTimeWordsInto();
        break;
    case 99:
        primitiveTickWordsInto();
        break;
    case 100:
        primitiveSignalAtTick();
        break;
    case 101:
        primitiveBeCursor();
        break;
    case 102:
        primitiveBeDisplay();
        break;
    case 103:
        //primitiveScanCharacters();
        primitiveFail(); // optional primitive not implemented
        break;
    case 104:
        //primitiveDrawLoop();
        primitiveFail(); // optional primitive not implemented
        break;
    case 105:
        primitiveStringReplace();
        break;
    default:
        primitiveFail();
        break;
    }
}

void Interpreter::dispatchSystemPrimitives()
{
    switch( primitiveIndex )
    {
    case 110:
        primitiveEquivalent();
        break;
    case 111:
        primitiveClass();
        break;
    case 112:
        //primitiveCoreLeft(); // number of unallocated Words in object space
        pushInteger(0xefff); // phantasy number
        break;
    case 113:
        primitiveQuit();
        break;
    case 114:
        qWarning() << "WARNING: primitiveExitToDebugger not yet implemnted";
        //primitiveExitToDebugger();
        break;
    case 115:
        //primitiveOopsLeft();
        pushInteger( memory->getOopsLeft() );
        break;
    case 116:
        primitiveSignalAtOopsLeftWordsLeft();
        break;
    default:
        primitiveFail();
        break;
    }
}

void Interpreter::dispatchPrivatePrimitives()
{
    ST_TRACE_PRIMITIVE("");
    switch( primitiveIndex )
    {
    case 128:
        primitiveAltoFile();
        break;
    default:
        qWarning() << "WARNING: private primitive" << primitiveIndex << "not yet implemented";
        primitiveFail();
        break;
    }
}

void Interpreter::dispatchIntegerPrimitives()
{
    switch( primitiveIndex )
    {
    case 1:
        primitiveAdd();
        break;
    case 2:
        primitiveSubtract();
        break;
    case 3:
        primitiveLessThan();
        break;
    case 4:
        primitiveGreaterThan();
        break;
    case 5:
        primitiveLessOrEqual();
        break;
    case 6:
        primitiveGreaterOrEqual();
        break;
    case 7:
        primitiveEqual();
        break;
    case 8:
        primitiveNotEqual();
        break;
    case 9:
        primitiveMultiply();
        break;
    case 10:
        primitiveDivide();
        break;
    case 11:
        primitiveMod();
        break;
    case 12:
        primitiveDiv();
        break;
    case 13:
        primitiveQuo();
        break;
    case 14:
        primitiveBitAnd();
        break;
    case 15:
        primitiveBitOr();
        break;
    case 16:
        primitiveBitXor();
        break;
    case 17:
        primitiveBitShift();
        break;
    case 18:
        primitiveMakePoint();
        break;
    default:
        primitiveFail();
        break;
    }
}

void Interpreter::dispatchLargeIntegerPrimitives()
{
    primitiveFail();
}

void Interpreter::dispatchFloatPrimitives()
{
    switch( primitiveIndex )
    {
    case 40:
        primitiveAsFloat();
        break;
    case 41:
        primitiveFloatAdd();
        break;
    case 42:
        primitiveFloatSubtract();
        break;
    case 43:
        primitiveFloatLessThan();
        break;
    case 44:
        primitiveFloatGreaterThan();
        break;
    case 45:
        primitiveFloatLessOrEqual();
        break;
    case 46:
        primitiveFloatGreaterOrEqual();
        break;
    case 47:
        primitiveFloatEqual();
        break;
    case 48:
        primitiveFloatNotEqual();
        break;
    case 49:
        primitiveFloatMultiply();
        break;
    case 50:
        primitiveFloatDivide();
        break;
    case 51:
        primitiveTruncated();
        break;
    case 52:
        primitiveFractionalPart();
        break;
    case 53:
        primitiveExponent();
        break;
    case 54:
        primitiveTimesTwoPower();
        break;
    default:
        primitiveFail();
        break;
    }
}

void Interpreter::_addSubMulImp(char op)
{
    const int integerArgument = popInteger();
    const int integerReceiver = popInteger();
    int integerResult = 0;
    if( success )
    {
        switch(op)
        {
        case '+':
            integerResult = integerReceiver + integerArgument;
            break;
        case '-':
            integerResult = integerReceiver - integerArgument;
            break;
        case '*':
            integerResult = integerReceiver * integerArgument;
            break;
        default:
            Q_ASSERT( false );
        }
        successUpdate( memory->isIntegerValue(integerResult) );
    }
    if( success )
        pushInteger( integerResult );
    else
        unPop(2);
}

void Interpreter::_floatOpImp(char op)
{
    const float floatArgument = popFloat();
    const float floatReceiver = popFloat();
    double floatResult = 0.0;
    if( success )
    {
        switch(op)
        {
        case '+':
            floatResult = floatReceiver + floatArgument;
            break;
        case '-':
            floatResult = floatReceiver - floatArgument;
            break;
        case '*':
            floatResult = floatReceiver * floatArgument;
            break;
        case '/':
            floatResult = floatReceiver / floatArgument;
            break;
        default:
            Q_ASSERT( false );
        }
    }
    if( success )
        pushFloat( floatResult );
    else
        unPop(2);
}

void Interpreter::_floatCompImp(char op)
{
    const float floatArgument = popFloat();
    const float floatReceiver = popFloat();
    if( success )
    {
        switch( op )
        {
        case '=':
            push( floatReceiver == floatArgument ? ObjectMemory2::objectTrue : ObjectMemory2::objectFalse );
            break;
        case '!':
            push( floatReceiver != floatArgument ? ObjectMemory2::objectTrue : ObjectMemory2::objectFalse );
            break;
        case '<':
            push( floatReceiver < floatArgument ? ObjectMemory2::objectTrue : ObjectMemory2::objectFalse );
            break;
        case 'l':
            push( floatReceiver <= floatArgument ? ObjectMemory2::objectTrue : ObjectMemory2::objectFalse );
            break;
        case '>':
            push( floatReceiver > floatArgument ? ObjectMemory2::objectTrue : ObjectMemory2::objectFalse );
            break;
        case 'g':
            push( floatReceiver >= floatArgument ? ObjectMemory2::objectTrue : ObjectMemory2::objectFalse );
            break;
        }
    }else
        unPop(2);
}

float Interpreter::popFloat()
{
    OOP f = popStack();
    successUpdate( memory->fetchClassOf(f) == ObjectMemory2::classFloat );
    if( success )
        return memory->fetchFloat(f);
    else
        return NAN;
}

void Interpreter::pushFloat(float v)
{
    OOP f = memory->instantiateClassWithWords(ObjectMemory2::classFloat, 2);
    memory->storeFloat(f,v);
    push(f);
}

void Interpreter::primitiveAt()
{
    ST_TRACE_PRIMITIVE("");
    OOP tmp = popStack();
    int index = positive16BitValueOf( tmp );
    OOP array = popStack();
    OOP arrayClass = memory->fetchClassOf(array);
    checkIndexableBoundsOf(index, array);
    OOP result = 0;

//    qDebug() << "primitiveAt receiver" << memory->prettyValue(array).constData()
//             << "index" << memory->prettyValue(index).constData();

    if( success )
    {
        index  = index + fixedFieldsOf(arrayClass);
        result = subscriptWith(array,index);
    }
    if( success )
        push(result);
    else
        unPop(2);
}

void Interpreter::primitiveAtPut()
{
    ST_TRACE_PRIMITIVE("");
    OOP value = popStack();
    int index = positive16BitValueOf( popStack() );
    OOP array = popStack();
    OOP arrayClass = memory->fetchClassOf(array);
    checkIndexableBoundsOf(index,array);

//    qDebug() << "primitiveAtPut receiver" << memory->prettyValue(array).constData()
//             << "index" << memory->prettyValue(index).constData()
//             << "value" << memory->prettyValue(value).constData();

    if( success )
    {
        index = index + fixedFieldsOf(arrayClass);
        subscriptWithStoring(array,index,value);
    }
    if( success )
        push(value);
    else
        unPop(3);
}

void Interpreter::primitiveSize()
{
    OOP array = popStack();
    OOP cls = memory->fetchClassOf(array);
    OOP length = positive16BitIntegerFor( lengthOf(array) - fixedFieldsOf(cls) );
    ST_TRACE_PRIMITIVE("size" << memory->integerValueOf(length));
    if( success )
        push(length);
    else
        unPop(1);
}

void Interpreter::primitiveStringAt()
{
    ST_TRACE_PRIMITIVE("");
    int index = positive16BitValueOf( popStack() );
    OOP array = popStack();
    checkIndexableBoundsOf(index,array);
    OOP character = 0;
    if( success )
    {
        qint16 ascii = memory->integerValueOf( subscriptWith(array,index) );
        character = memory->fetchPointerOfObject(ascii, ObjectMemory2::characterTable );
    }
    if( success )
        push(character);
    else
        unPop(2);
}

void Interpreter::primitiveStringAtPut()
{
    ST_TRACE_PRIMITIVE("");
    OOP character = popStack();
    int index = positive16BitValueOf( popStack() );
    OOP array = popStack();
    checkIndexableBoundsOf(index, array);
    successUpdate( memory->fetchClassOf(character) == ObjectMemory2::classCharacter );
    if( success )
    {
        OOP ascii = memory->fetchPointerOfObject(0,character);
        subscriptWithStoring(array,index,ascii);
    }
    if( success )
        push(character);
    else
        unPop(3); // BB error, VIM fixed
}

void Interpreter::primitiveNext()
{
    ST_TRACE_PRIMITIVE("");
    OOP stream = popStack();
    OOP array = memory->fetchPointerOfObject(StreamArrayIndex,stream);
    OOP arrayClass = memory->fetchClassOf(array);
    int index = fetchIntegerOfObject(StreamIndexIndex,stream);
    int limit = fetchIntegerOfObject(StreamReadLimitIndex,stream);
    successUpdate( index < limit );
    successUpdate( arrayClass == ObjectMemory2::classArray || arrayClass == ObjectMemory2::classString );
    checkIndexableBoundsOf( index + 1, array);
    OOP result = 0;
    if( success )
    {
        index++;
        result = subscriptWith(array,index);
    }
    if( success )
        storeIntegerOfObjectWithValue(StreamIndexIndex,stream,index);
    if( success )
    {
        if( arrayClass == ObjectMemory2::classArray )
            push(result);
        else
        {
            int ascii = memory->integerValueOf(result);
            push( memory->fetchPointerOfObject( ascii, ObjectMemory2::characterTable ) );
        }
    }else
        unPop(1);
}

void Interpreter::primitiveNextPut()
{
    ST_TRACE_PRIMITIVE("");
    OOP value = popStack();
    OOP stream = popStack();
    OOP array = memory->fetchPointerOfObject(StreamArrayIndex, stream);
    OOP arrayClass = memory->fetchClassOf(array);
    int index = fetchIntegerOfObject(StreamIndexIndex,stream);
    int limit = fetchIntegerOfObject(StreamWriteLimitIndex, stream);
    successUpdate( index < limit );
    successUpdate( arrayClass == ObjectMemory2::classArray || arrayClass == ObjectMemory2::classString );
    checkIndexableBoundsOf( index + 1, array);
    if( success )
    {
        index++;
        if( arrayClass == ObjectMemory2::classArray )
            subscriptWithStoring(array,index,value);
        else
        {
            OOP ascii = memory->fetchPointerOfObject(0, value); // character value index 0
            subscriptWithStoring(array, index, ascii );
        }
    }
    if( success )
        storeIntegerOfObjectWithValue(StreamIndexIndex, stream, index);
    if( success )
        push(value);
    else
        unPop(2);
}

void Interpreter::primitiveAtEnd()
{
    ST_TRACE_PRIMITIVE("");
    OOP stream = popStack();
    OOP array = memory->fetchPointerOfObject(StreamArrayIndex, stream);
    OOP arrayClass = memory->fetchClassOf(array);
    int lenght = lengthOf(array);
    int index = fetchIntegerOfObject(StreamIndexIndex,stream);
    int limit = fetchIntegerOfObject(StreamReadLimitIndex, stream);
    successUpdate( index < limit );
    successUpdate( arrayClass == ObjectMemory2::classArray || arrayClass == ObjectMemory2::classString );
    if( success )
    {
        if( index >= limit || index >= lenght )
            push( ObjectMemory2::objectTrue );
        else
            push( ObjectMemory2::objectFalse );
    }else
        unPop(1);
}

void Interpreter::checkIndexableBoundsOf(int index, Interpreter::OOP array)
{
    OOP cls = memory->fetchClassOf(array);
    successUpdate( index >= 1 );
    successUpdate( index + fixedFieldsOf(cls) <= lengthOf(array) );
}

int Interpreter::lengthOf(Interpreter::OOP array)
{
    if( isWords( memory->fetchClassOf(array) ) )
        return memory->fetchWordLenghtOf(array);
    else
        return memory->fetchByteLenghtOf(array);
}

Interpreter::OOP Interpreter::subscriptWith(Interpreter::OOP array, int index)
{
    OOP cls = memory->fetchClassOf(array);
    if( isWords(cls) )
    {
        if( isPointers(cls) )
            return memory->fetchPointerOfObject(index-1,array);
        else
        {
            const quint16 value = memory->fetchWordOfObject(index-1,array);
            return positive16BitIntegerFor(value);
        }
    }else
    {
        quint8 value = memory->fetchByteOfObject(index-1,array);
        return memory->integerObjectOf(value);
    }
}

void Interpreter::subscriptWithStoring(Interpreter::OOP array, int index, Interpreter::OOP value)
{
    OOP cls = memory->fetchClassOf(array);
    if( isWords(cls) )
    {
        if( isPointers(cls) )
            memory->storePointerOfObject(index-1, array, value );
        else
        {
            successUpdate( memory->isIntegerObject(value) );
            if( success )
                memory->storeWordOfObject(index-1, array, positive16BitValueOf(value) );
            // else never observed so far
        }
    }else
    {
        successUpdate( memory->isIntegerObject(value) );
        if( success )
            memory->storeByteOfObject(index-1, array, lowByteOf( memory->integerValueOf(value) ) );
    }
}

void Interpreter::primitiveObjectAt()
{
    ST_TRACE_PRIMITIVE("");
    qint16 index = popInteger();
    OOP thisReceiver = popStack();
    successUpdate( index > 0);
    successUpdate( index <= memory->objectPointerCountOf(thisReceiver) );
    if( success )
        push( memory->fetchPointerOfObject(index-1, thisReceiver) );
    else
        unPop(2);
}

void Interpreter::primitiveObjectAtPut()
{
    ST_TRACE_PRIMITIVE("");
    OOP newValue = popStack();
    qint16 index = popInteger();
    OOP thisReceiver = popStack();
    successUpdate( index > 0);
    successUpdate( index <= ( memory->objectPointerCountOf(thisReceiver) ) );

//    qDebug() << "primitiveObjectAtPut receiver" << memory->prettyValue(thisReceiver).constData()
//             << "index" << memory->prettyValue(index).constData()
//             << "value" << memory->prettyValue(newValue).constData();

    if( success )
    {
        memory->storePointerOfObject(index-1, thisReceiver, newValue );
        push( newValue );
    }else
        unPop(3);
}

void Interpreter::primitiveNew()
{
    ST_TRACE_PRIMITIVE("");
    OOP cls = popStack();
    int size = fixedFieldsOf(cls);
    successUpdate( !isIndexable(cls) );
    if( success )
    {
        if( isPointers(cls) )
            push( memory->instantiateClassWithPointers(cls,size) );
        else
        {
            // qWarning() << "primitiveNew word" << memory->fetchClassName(cls).constData() << size;
            // never called
            push( memory->instantiateClassWithWords(cls,size) );
        }
    }else
        unPop(1);
}

void Interpreter::primitiveNewWithArg()
{
    ST_TRACE_PRIMITIVE("");
    int size = positive16BitValueOf( popStack() );
    OOP cls = popStack();
    successUpdate( isIndexable(cls) );
    if( success )
    {
        size += fixedFieldsOf(cls);
        if( isPointers(cls) )
            push( memory->instantiateClassWithPointers(cls,size) );
        else if( isWords(cls) )
        {
            // qWarning() << "primitiveNewWithArg word" << memory->fetchClassName(cls).constData() << size;
            // WordArray, DisplayBitmap
            push( memory->instantiateClassWithWords(cls,size) );
        }else
        {
            // qWarning() << "primitiveNewWithArg bytes" << memory->fetchClassName(cls).constData() << size;
            // LargePositiveInteger (0,1,2 or 3 bytes observed), String
            push( memory->instantiateClassWithBytes(cls,size) );
        }
    }else
        unPop(2);
}

void Interpreter::primitiveBecome()
{
    // primitive 72
    // called for a DisplayScreen and a Set
    ST_TRACE_PRIMITIVE("");
    OOP otherPointer = popStack();
    OOP thisReceiver = popStack();
    successUpdate( !memory->isIntegerObject(otherPointer) );
    successUpdate( !memory->isIntegerObject(thisReceiver) );
    // qDebug() << "primitiveBecome" << memory->prettyValue(thisReceiver) << memory->prettyValue(otherPointer);
    if( success )
    {
        memory->swapPointersOf(thisReceiver,otherPointer);
        push(thisReceiver);
    }else
        unPop(2);
}

void Interpreter::primitiveInstVarAt()
{
    ST_TRACE_PRIMITIVE("");
    int index = popInteger();
    OOP thisReceiver = popStack();
    checkInstanceVariableBoundsOf(index,thisReceiver);
    OOP value = 0;
    if( success )
        value = subscriptWith(thisReceiver,index);
    if( success )
        push(value);
    else
        unPop(2);
}

void Interpreter::primitiveInstVarAtPut()
{
    ST_TRACE_PRIMITIVE("");
    OOP newValue = popStack();
    int index = popInteger();
    OOP thisReceiver = popStack();
    checkInstanceVariableBoundsOf(index,thisReceiver);

//    qDebug() << "primitiveInstVarAtPut receiver" << memory->prettyValue(thisReceiver).constData()
//             << "index" << memory->prettyValue(index).constData()
//             << "value" << memory->prettyValue(newValue).constData();

    if( success )
        subscriptWithStoring(thisReceiver,index,newValue);
    if( success )
        push(newValue);
    else
        unPop(3);
}

void Interpreter::primitiveAsOop()
{
    ST_TRACE_PRIMITIVE("");
    OOP thisReceiver = popStack();
    successUpdate( !memory->isIntegerObject(thisReceiver) );
    if( success )
        push( thisReceiver | 0x1 );
    else
        unPop(1);
}

void Interpreter::primitiveAsObject()
{
    ST_TRACE_PRIMITIVE("");
    OOP thisReceiver = popStack();
    OOP newOop = thisReceiver & 0xfffe;
    successUpdate( memory->hasObject( newOop ) ); // hasObject is not documented in BB
    if( success )
        push( newOop );
    else
        unPop(1);
}

void Interpreter::primitiveSomeInstance()
{
    ST_TRACE_PRIMITIVE("");
    OOP cls = popStack();
    OOP next = memory->getNextInstance(cls);
    if( next )
        push(next);
    else
        primitiveFail();
}

void Interpreter::primitiveNextInstance()
{
    ST_TRACE_PRIMITIVE("");
    OOP object = popStack();
    OOP cls = memory->fetchClassOf(object);
    OOP next = memory->getNextInstance(cls, object);
    if( next )
        push(next);
    else
        primitiveFail();
}

void Interpreter::primitiveNewMethod()
{
    // primitive 79
    ST_TRACE_PRIMITIVE("");
    const OOP header = popStack();
    const int bytecodeCount = popInteger();
    const OOP cls = popStack();
    const int literalCount = literalCountOfHeader(header);
    const int size = ( literalCount + 1 ) * 2 + bytecodeCount;
    OOP newMethod = memory->instantiateClassWithBytes(cls,size);
    memory->storeWordOfObject(0, newMethod, header); // BB error: this line got obviously lost
    for( int i = 0; i < literalCount; i++ )
        memory->storePointerOfObject(1 + i, newMethod, ObjectMemory2::objectNil );  // BB error, VIM fixed
    push( newMethod );
}

void Interpreter::checkInstanceVariableBoundsOf(int index, Interpreter::OOP object)
{
    // OOP cls = memory->fetchClassOf(object); // BB makes this fetch, but not used
    successUpdate( index >= 1 );
    successUpdate( index <= lengthOf(object));
}

void Interpreter::primitiveValueWithArgs()
{
    ST_TRACE_PRIMITIVE("");
    OOP argumentArray = popStack();
    OOP blockContext = popStack();
    OOP blockArgumentCount = argumentCountOfBlock(blockContext);
    OOP arrayClass = memory->fetchClassOf(argumentArray);
    successUpdate( arrayClass == ObjectMemory2::classArray );
    OOP arrayArgumentCount = 0;
    if( success )
    {
        arrayArgumentCount = memory->fetchWordLenghtOf(argumentArray);
        successUpdate( arrayArgumentCount == blockArgumentCount );
    }
    if( success )
    {
        transfer( arrayArgumentCount, 0, argumentArray, TempFrameStart, blockContext );
        OOP initialIP = memory->fetchPointerOfObject(InitialIPIndex, blockContext);
        memory->storePointerOfObject(InstructionPointerIndex, blockContext, initialIP );
        storeStackPointerValueInContext( arrayArgumentCount, blockContext );
        memory->storePointerOfObject(CallerIndex, blockContext, memory->getRegister(ActiveContext) );
        newActiveContext( blockContext );
    }else
        unPop(2);
}

void Interpreter::primitivePerform()
{
    OOP performSelector = memory->getRegister(MessageSelector);
    OOP newSelector = stackValue(argumentCount-1);
    memory->setRegister(MessageSelector, newSelector);
    ST_TRACE_PRIMITIVE("selector" << memory->prettyValue(newSelector).constData());
    OOP newReceiver = stackValue(argumentCount);
    lookupMethodInClass( memory->fetchClassOf(newReceiver) );
    successUpdate( memory->argumentCountOf( memory->getRegister(NewMethod) ) == argumentCount - 1 );
    if( success )
    {
        int selectorIndex = stackPointer - argumentCount + 1;
        transfer( argumentCount -1 , selectorIndex + 1, memory->getRegister(ActiveContext),
                  selectorIndex, memory->getRegister(ActiveContext) );
        pop(1);
        argumentCount--;
        executeNewMethod();
    }else
        memory->setRegister(MessageSelector, performSelector );
}

void Interpreter::primitivePerformWithArgs()
{
    ST_TRACE_PRIMITIVE("");
    OOP argumentArray = popStack();
    int arraySize = memory->fetchWordLenghtOf(argumentArray);
    OOP arrayClass = memory->fetchClassOf(argumentArray);
    successUpdate( (stackPointer+arraySize) < memory->fetchWordLenghtOf( memory->getRegister(ActiveContext) ) );
    successUpdate( arrayClass == ObjectMemory2::classArray );
    if( success )
    {
        OOP performSelector = memory->getRegister(MessageSelector);
        memory->setRegister(MessageSelector, popStack());
        OOP thisReceiver = stackTop();
        argumentCount = arraySize;
        int index = 1;
        while( index <= argumentCount )
        {
            push( memory->fetchPointerOfObject(index-1, argumentArray) );
            index++;
        }
        lookupMethodInClass( memory->fetchClassOf(thisReceiver) );
        successUpdate( memory->argumentCountOf( memory->getRegister(NewMethod) ) == argumentCount );
        if( success )
            executeNewMethod();
        else
        {
            unPop(argumentCount);
            push( memory->getRegister(MessageSelector));
            push(argumentArray);
            argumentCount = 2;
            memory->setRegister(MessageSelector, performSelector );
        }
    }else
        unPop(1);
}

void Interpreter::primitiveSignal()
{
    OOP top = stackTop();
    ST_TRACE_PRIMITIVE("stackTop" << memory->prettyValue(top).constData());
    synchronousSignal( top );
}

void Interpreter::primitiveWait()
{
    ST_TRACE_PRIMITIVE("");
    OOP thisReceiver = stackTop();
    int excessSignals = fetchIntegerOfObject( ExcessSignalIndex, thisReceiver );
    if( excessSignals > 0 )
        storeIntegerOfObjectWithValue( ExcessSignalIndex, thisReceiver, excessSignals - 1 );
    else
    {
        addLastLinkToList( activeProcess(), thisReceiver );
        suspendActive();
    }
}

void Interpreter::primitiveResume()
{
    ST_TRACE_PRIMITIVE("");
    resume( stackTop() );
}

void Interpreter::primitiveSuspend()
{
    ST_TRACE_PRIMITIVE("");
    successUpdate( stackTop() == activeProcess() );
    if( success )
    {
        popStack();
        push( ObjectMemory2::objectNil );
        suspendActive();
    }
}

void Interpreter::primitiveFlushCache()
{
    ST_TRACE_PRIMITIVE("");
    // initializeMethodCache();
    // we don't have that
}

void Interpreter::asynchronousSignal(Interpreter::OOP aSemaphore)
{
    semaphoreList.push_back(aSemaphore);
}

bool Interpreter::isEmptyList(Interpreter::OOP aLinkedList)
{
    if( aLinkedList == ObjectMemory2::objectNil )
        return true;
    return memory->fetchPointerOfObject(FirstLinkIndex,aLinkedList) == ObjectMemory2::objectNil;
}

void Interpreter::synchronousSignal(Interpreter::OOP aSemaphore)
{
    if( isEmptyList(aSemaphore) )
    {
        int excessSignals = fetchIntegerOfObject(ExcessSignalIndex,aSemaphore);
        storeIntegerOfObjectWithValue(ExcessSignalIndex, aSemaphore, excessSignals + 1 );
    }else
        resume( removeFirstLinkOfList(aSemaphore) );
}

Interpreter::OOP Interpreter::removeFirstLinkOfList(Interpreter::OOP aLinkedList)
{
    OOP firstLink = memory->fetchPointerOfObject(FirstLinkIndex,aLinkedList);
    OOP lastLink = memory->fetchPointerOfObject(LastLinkIndex,aLinkedList);
    if( firstLink == lastLink )
    {
        memory->storePointerOfObject(FirstLinkIndex, aLinkedList, ObjectMemory2::objectNil );
        memory->storePointerOfObject(LastLinkIndex, aLinkedList, ObjectMemory2::objectNil );
    }else
    {
        OOP nextLink = memory->fetchPointerOfObject(NextLinkIndex, firstLink );
        memory->storePointerOfObject(FirstLinkIndex, aLinkedList, nextLink);
    }
    memory->storePointerOfObject(NextLinkIndex, firstLink, ObjectMemory2::objectNil );
    return firstLink;
}

void Interpreter::transferTo(Interpreter::OOP aProcess)
{
    newProcessWaiting = true;
    memory->setRegister(NewProcess,aProcess);
}

Interpreter::OOP Interpreter::activeProcess()
{
    if( newProcessWaiting )
        return memory->getRegister(NewProcess);
    else
        return memory->fetchPointerOfObject( ActiveProcessIndex, schedulerPointer() );
}

Interpreter::OOP Interpreter::schedulerPointer()
{
    return memory->fetchPointerOfObject(ValueIndex, ObjectMemory2::processor );
}

Interpreter::OOP Interpreter::firstContext()
{
    newProcessWaiting = false;
    return memory->fetchPointerOfObject( SuspendedContextIndex, activeProcess() );
}

void Interpreter::addLastLinkToList(Interpreter::OOP aLink, Interpreter::OOP aLinkedList)
{
    if( isEmptyList( aLinkedList ) )
        memory->storePointerOfObject( FirstLinkIndex, aLinkedList, aLink );
    else
    {
        OOP lastLink = memory->fetchPointerOfObject( LastLinkIndex, aLinkedList );
        memory->storePointerOfObject( NextLinkIndex, lastLink, aLink );
    }
    memory->storePointerOfObject( LastLinkIndex, aLinkedList, aLink );
    memory->storePointerOfObject( MyListIndex, aLink, aLinkedList );
}

Interpreter::OOP Interpreter::wakeHighestPriority()
{
    OOP processLists = memory->fetchPointerOfObject( ProcessListIndex, schedulerPointer() );
    int priority = memory->fetchWordLenghtOf( processLists );
    OOP processList = 0;
    while( true )
    {
        processList = memory->fetchPointerOfObject( priority - 1, processLists );
        if( isEmptyList( processList ) )
            priority--;
        else
            break;
    }
    return removeFirstLinkOfList( processList );
}

void Interpreter::sleep(Interpreter::OOP aProcess)
{
    int priority = fetchIntegerOfObject( PriorityIndex, aProcess );
    OOP processLists = memory->fetchPointerOfObject( ProcessListIndex, schedulerPointer() );
    OOP processList = memory->fetchPointerOfObject( priority - 1, processLists );
    addLastLinkToList( aProcess, processList );
}

void Interpreter::resume(Interpreter::OOP aProcess)
{
    OOP activeProcess_ = activeProcess();
    int activePriority = fetchIntegerOfObject( PriorityIndex, activeProcess_ );
    int newPriority = fetchIntegerOfObject( PriorityIndex, aProcess );
    if( newPriority > activePriority )
    {
        sleep( activeProcess_ );
        transferTo( aProcess );
    }else
        sleep( aProcess );
}

void Interpreter::suspendActive()
{
    transferTo( wakeHighestPriority() );
}

void Interpreter::primitiveQuit()
{
    ST_TRACE_PRIMITIVE("");
    Display::s_run = false;
    // Display::inst()->close();
}

void Interpreter::createActualMessage()
{
    OOP argumentArray = memory->instantiateClassWithPointers( ObjectMemory2::classArray, argumentCount );
    OOP message = memory->instantiateClassWithPointers( ObjectMemory2::classMessage, MessageSize );
    memory->storePointerOfObject( MessageSelectorIndex, message, memory->getRegister(MessageSelector) );
    memory->storePointerOfObject( MessageArgumentsIndex, message, argumentArray );
    transfer( argumentCount, stackPointer - (argumentCount - 1 ), memory->getRegister(ActiveContext), 0, argumentArray );
    pop( argumentCount );
    push( message );
    argumentCount = 1;
}

void Interpreter::sendMustBeBoolean()
{
    sendSelector( ObjectMemory2::symbolMustBeBoolean, 0 );
}

QByteArray Interpreter::prettyArgs_()
{
    QByteArray res;
    for( int i = 0; i < argumentCount; i++ )
    {
        if( i != 0 )
            res += " ";
        res += memory->prettyValue( stackValue(argumentCount- (i+1) ) );
    }
    return res;
}

void Interpreter::primitiveBeDisplay()
{
    ST_TRACE_PRIMITIVE("");
    OOP displayScreen = popStack();

    // OOP cls = memory->fetchClassOf(displayScreen);
    // qDebug() << memory->fetchClassName(cls);
    // checked that cls is in fact DisplayScreen
    Display::inst()->setBitmap( fetchBitmap(memory, displayScreen) );
}

void Interpreter::primitiveCopyBits()
{
    // primitive 96

    /* Stack:
    9 "<a BitBlt>" // there is always a BitBlt or one of its subclasses top of the stack
    8 "<a Form>"
    7 "3"
    6 "<a Rectangle>"
    */
    OOP bitblt = stackTop();

    Bitmap destBits = fetchBitmap(memory, memory->fetchPointerOfObject(0,bitblt) );
    Bitmap sourceBits = fetchBitmap(memory, memory->fetchPointerOfObject(1,bitblt) );
    Bitmap halftoneBits = fetchBitmap(memory, memory->fetchPointerOfObject(2,bitblt) );

    Display* disp = Display::inst();
    const bool drawToDisp = disp->getBitmap().isSameBuffer( destBits );
    ST_TRACE_PRIMITIVE( ( drawToDisp ? "to display" : "offscreen" ) );

    BitBlt::Input in;
    if( !destBits.isNull() )
        in.destBits = &destBits;
    if( !sourceBits.isNull() )
        in.sourceBits = &sourceBits;
    if( !halftoneBits.isNull() )
        in.halftoneBits = &halftoneBits;

    in.combinationRule = memory->integerValueOf( memory->fetchPointerOfObject(3,bitblt), true );
    in.destX = memory->integerValueOf( memory->fetchPointerOfObject(4,bitblt), true );
    in.destY = memory->integerValueOf( memory->fetchPointerOfObject(5,bitblt), true );
    in.width = memory->integerValueOf( memory->fetchPointerOfObject(6,bitblt), true );
    in.height = memory->integerValueOf( memory->fetchPointerOfObject(7,bitblt), true );
    in.sourceX = memory->integerValueOf( memory->fetchPointerOfObject(8,bitblt), true );
    in.sourceY = memory->integerValueOf( memory->fetchPointerOfObject(9,bitblt), true );
    in.clipX = memory->integerValueOf( memory->fetchPointerOfObject(10,bitblt), true );
    in.clipY = memory->integerValueOf( memory->fetchPointerOfObject(11,bitblt), true );
    in.clipWidth = memory->integerValueOf( memory->fetchPointerOfObject(12,bitblt), true );
    in.clipHeight = memory->integerValueOf( memory->fetchPointerOfObject(13,bitblt), true );

//    if( !halftoneBits.isNull() )
//        Q_ASSERT( halftoneBits.width() == 16 && halftoneBits.height() == 16 ); // this always holds

    BitBlt bb( in );
    bb.copyBits();

    if( drawToDisp )
    {
        const QRect dest(in.destX, in.destY, in.width, in.height);
        const QRect clip( in.clipX, in.clipY, in.clipWidth, in.clipHeight );
        disp->updateArea( dest & clip );
    }
//    static int count = 0;
//    destBits.toImage().save(QString("cppscreens/bitblt_%1.png").arg(++count,4, 10, QChar('0')));

#ifdef ST_DO_SCREEN_RECORDING
    if( disp->isRecOn() && drawToDisp )
    {
//        qDebug() << cycleNr << "updating screen" << in.destX << in.destY << in.width << in.height
//                 << "clipped at" << in.clipX << in.clipY << in.clipWidth << in.clipHeight;
        qDebug() << "copyBits" << in.destX << in.destY << in.width << in.height << in.combinationRule;
        disp->drawRecord( in.destX, in.destY, in.width, in.height );
        QImage img = destBits.toImage().convertToFormat(QImage::Format_RGB32);
        if( true )
        {
            QPainter p(&img);
            p.setPen(Qt::red);
            p.drawRect(in.clipX, in.clipY, in.clipWidth, in.clipHeight);
            p.setPen(Qt::green);
            p.drawRect(in.destX, in.destY, in.width, in.height);
        }
        img.save(QString("step_%1.png").arg(cycleNr,8,10,QChar('0')));
    }
#endif
}

void Interpreter::primitiveStringReplace()
{
    ST_TRACE_PRIMITIVE("");
    primitiveFail(); // optional, apparently has as smalltalk implementation
    // pop(4);
}

void Interpreter::dumpStack_(const char* title)
{
    qWarning() << "**********" << title << "argcount" << argumentCount << "begin stack:";
#if 0
    for( int i = 0; i < argumentCount; i++ )
        qWarning() << i << memory->prettyValue( stackValue(argumentCount- (i+1) ) );
#else
    for( int i = stackPointer; i > TempFrameStart; i-- )
        qWarning() << i << memory->prettyValue(
                        memory->fetchPointerOfObject( i, memory->getRegister(ActiveContext) ) ).constData();
#endif
    qWarning() << "***** end stack";
}

void Interpreter::primitiveBeCursor()
{
    ST_TRACE_PRIMITIVE("");
    OOP cursor = popStack();

    Display::inst()->setCursorBitmap( fetchBitmap(memory, cursor ) );

}

void Interpreter::primitiveCursorLink()
{
    ST_TRACE_PRIMITIVE("");
    qWarning() << "WARNING: primitiveCursorLink not supported" << memory->prettyValue(stackTop());
    popStack(); // bool
}

void Interpreter::primitiveInputSemaphore()
{
    ST_TRACE_PRIMITIVE("");
    memory->setRegister( InputSemaphore, popStack() );
}

void Interpreter::primitiveInputWord()
{
    ST_TRACE_PRIMITIVE("");
    pop(1);
    push( positive16BitIntegerFor( Display::inst()->nextEvent() ) );
}

void Interpreter::primitiveSamleInterval()
{
    ST_TRACE_PRIMITIVE("");
    qWarning() << "WARNING: primitiveSamleInterval not yet implemented";
    primitiveFail();
}

static qint16 cropPoint( int pos )
{
    if( pos > 16383 )
        pos = 16383;
    else if( pos < -16384 )
        pos = -16384;
    return pos;
}

void Interpreter::primitiveMousePoint()
{
    ST_TRACE_PRIMITIVE("");
#if 0
    QPoint pos = Display::inst()->getMousePos();

    OOP point = memory->instantiateClassWithPointers( ObjectMemory2::classPoint, ClassPointSize );
    memory->storePointerOfObject( XIndex, point, memory->integerObjectOf(cropPoint(pos.x())) );
    memory->storePointerOfObject( YIndex, point, memory->integerObjectOf(cropPoint(pos.y())) );
    push( point );
#else
    primitiveFail();
#endif

}

void Interpreter::primitiveSignalAtOopsLeftWordsLeft()
{
    ST_TRACE_PRIMITIVE("");
    OOP number1 = popStack();
    OOP number2 = popStack();
    OOP semaphore =  popStack();

    // BB error: not documented, neither in VIM
    qWarning() << "WARNING: primitiveSignalAtOopsLeftWordsLeft not implemented" << memory->prettyValue(number1).constData()
             << memory->prettyValue(number2).constData() << memory->prettyValue(semaphore).constData();
}

void Interpreter::primitiveCursorLocPut()
{
    ST_TRACE_PRIMITIVE("");
    OOP point = popStack();
    Display::inst()->setCursorPos( memory->integerValueOf( memory->fetchPointerOfObject( XIndex, point ) ),
                                   memory->integerValueOf( memory->fetchPointerOfObject( YIndex, point ) ) );
}

void Interpreter::primitiveTimeWordsInto()
{
    ST_TRACE_PRIMITIVE("");
    OOP oop = popStack();
    const quint32 diff = QDateTime( QDate( 1901, 1, 1 ), QTime( 0, 0, 0 ) ).secsTo(QDateTime::currentDateTime());
    // qDebug() << "primitiveTimeWordsInto" << diff;
    memory->storeByteOfObject(3,oop, ( diff >> 24 ) & 0xff );
    memory->storeByteOfObject(2,oop, ( diff >> 16 ) & 0xff );
    memory->storeByteOfObject(1,oop, ( diff >> 8 ) & 0xff );
    memory->storeByteOfObject(0,oop, diff & 0xff );
}

void Interpreter::primitiveTickWordsInto()
{
    ST_TRACE_PRIMITIVE("");
    OOP oop = popStack();
    const quint32 ticks = Display::inst()->getTicks();
    // qDebug() << "primitiveTickWordsInto" << ticks;
    // NOTE: unexpected byte order, but empirically validated with primitiveSignalAtTick that it's the right one
    memory->storeByteOfObject(3,oop, ( ticks >> 24 ) & 0xff );
    memory->storeByteOfObject(2,oop, ( ticks >> 16 ) & 0xff );
    memory->storeByteOfObject(1,oop, ( ticks >> 8 ) & 0xff );
    memory->storeByteOfObject(0,oop, ticks & 0xff );
}

void Interpreter::primitiveSignalAtTick()
{
    ST_TRACE_PRIMITIVE("");
    OOP oop = popStack();
    toSignal = popStack();
    quint32 time =  ( memory->fetchByteOfObject(3,oop) << 24 ) |
    ( memory->fetchByteOfObject(2,oop) << 16 ) |
    ( memory->fetchByteOfObject(1,oop) << 8 ) |
    memory->fetchByteOfObject(0,oop);
    quint32 ticks = Display::inst()->getTicks();
    const int diff = time - ticks;
    if( diff > 0 )
        d_timer.start(diff);
    else
        asynchronousSignal(toSignal);
}

void Interpreter::primitiveAltoFile()
{
    // primitive 128
    // dumpStack_("primitiveAltoFile");
    /********** primitiveAltoFile argcount 5 begin stack:
    15 <a Semaphore>    semaphore:
    14 <a ByteArray>    page: buffer 528 bytes class ByteArray
    13 18512L           command: diskCommand -> #CCR
    12 4096             address: diskAddress // see AltoFileDirectory>>virtualToReal: 1->4096
    11 0                diskNumber
    10 <a AltoFile>
    9 <a Semaphore>
    8 "read:"
    7 <a AltoFilePage>
    ***** end stack */

    // BREAK();

    const OOP semaphore = popStack();
    const OOP buffer = popStack();
    const int command = positive16BitValueOf( popStack() );
    const int address = positive16BitValueOf( popStack() );
    const int diskNumber = positive16BitValueOf( popStack() );

    ST_TRACE_PRIMITIVE("command:" << command << "address:" << address << "disk:" << diskNumber
                       << "bufsize:" << memory->fetchByteLenghtOf(buffer) );

    for( int i = 0; i < memory->fetchByteLenghtOf(buffer); i++ )
        memory->storeByteOfObject(i,buffer,0);
    asynchronousSignal(semaphore);

    // push(buffer);

    /* called by
    // -1: AltoFile dskprim
    // 0: AltoFile doCommand:page:error:
    // 1: FilePage doCommand:error:
    // 2: AltoFile read:
    // 3: File readPageNumber:
    // 4: AltoFileDirectory open
        nSectors ← 12.
        diskPages ← 812 * nSectors. // nTracks * nHeads * nSectors.
        totalPages ← 2 * diskPages. // nDisks * diskPages
        #Dirname = "SysDir."
    // 5: AltoFileDirectory reset
    */

    /*
    dskprim:
        diskNumber
        address: diskAddress
        command: diskCommand
        page: buffer
        semaphore: aSemaphore
    "Transfer a single record (page) to or from the Alto File System.  Fail if
    integer arguments are not 16-bit positive Integers.  Fail if a disk transfer
     is already in progress.  Fail if the buffer is not large enough or is
    pointer containing.  Fail if the last argument is not a Semaphore.  Xerox
    specific primitive.  See Object documentation what IsAPrimitive.

    diskNumber is 0 or 1,
    diskAddress is the starting Alto disk address (Integer),
    diskCommand is the disk command (usually CCR, CCW, CWW)
    (Integer), buffer is the string containing label and data,
    aSemaphore is signalled when the transfer completes.
    If disk primitive encounters an error, the receiver's instance variable
    named error is set to the DCB status.  This Integer is greater than 0 and is
    interpreted by errorString:.  Normally error is set to 0."
     */

}

void Interpreter::primitiveQuo()
{
    ST_TRACE_PRIMITIVE("");
    const double integerArgument = popInteger();
    const double integerReceiver = popInteger();
    successUpdate( integerArgument != 0 );
    qint16 integerResult = 0;
    if( success )
    {
        integerResult = qRound( integerReceiver / integerArgument );
        successUpdate( memory->isIntegerValue(integerResult) );
    }
    if( success )
        pushInteger( integerResult );
    else
        unPop(2);
}

void Interpreter::primitiveBitXor()
{
    ST_TRACE_PRIMITIVE("");
    _bitImp('^');
}

void Interpreter::_compareImp(char op)
{
    const qint16 integerArgument = popInteger();
    const qint16 integerReceiver = popInteger();
    if( success )
    {
        switch( op )
        {
        case '=':
            push( integerReceiver == integerArgument ? ObjectMemory2::objectTrue : ObjectMemory2::objectFalse );
            break;
        case '!':
            push( integerReceiver != integerArgument ? ObjectMemory2::objectTrue : ObjectMemory2::objectFalse );
            break;
        case '<':
            push( integerReceiver < integerArgument ? ObjectMemory2::objectTrue : ObjectMemory2::objectFalse );
            break;
        case 'l':
            push( integerReceiver <= integerArgument ? ObjectMemory2::objectTrue : ObjectMemory2::objectFalse );
            break;
        case '>':
            push( integerReceiver > integerArgument ? ObjectMemory2::objectTrue : ObjectMemory2::objectFalse );
            break;
        case 'g':
            push( integerReceiver >= integerArgument ? ObjectMemory2::objectTrue : ObjectMemory2::objectFalse );
            break;
        }
    }else
        unPop(2);
}

void Interpreter::_bitImp(char op)
{
    const qint16 integerArgument = popInteger();
    const qint16 integerReceiver = popInteger();
    qint16 integerResult = 0;
    if( success )
    {
        switch( op )
        {
        case '&':
            integerResult = integerReceiver & integerArgument;
            break;
        case '|':
            integerResult = integerReceiver | integerArgument;
            break;
        case '^':
            integerResult = integerReceiver ^ integerArgument;
            break;
        }
    }
    if( success )
        pushInteger( integerResult );
    else
        unPop(2);

}

void Interpreter::primitiveAsFloat()
{
    ST_TRACE_PRIMITIVE("");
    const qint16 integerReceiver = popInteger();
    if( success )
        pushFloat(integerReceiver);
    else
        unPop(1);
}

void Interpreter::primitiveFloatAdd()
{
    ST_TRACE_PRIMITIVE("");
    _floatOpImp('+');
}

void Interpreter::primitiveFloatSubtract()
{
    ST_TRACE_PRIMITIVE("");
    _floatOpImp('-');
}

void Interpreter::primitiveFloatLessThan()
{
    ST_TRACE_PRIMITIVE("");
    _floatCompImp('<');
}

void Interpreter::primitiveFloatGreaterThan()
{
    ST_TRACE_PRIMITIVE("");
    _floatCompImp('>');
}

void Interpreter::primitiveFloatLessOrEqual()
{
    ST_TRACE_PRIMITIVE("");
    _floatCompImp('l');
}

void Interpreter::primitiveFloatGreaterOrEqual()
{
    ST_TRACE_PRIMITIVE("");
    _floatCompImp('g');
}

void Interpreter::primitiveFloatEqual()
{
    ST_TRACE_PRIMITIVE("");
    _floatCompImp('=');
}

void Interpreter::primitiveFloatNotEqual()
{
    ST_TRACE_PRIMITIVE("");
    _floatCompImp('!');
}

void Interpreter::primitiveFloatMultiply()
{
    ST_TRACE_PRIMITIVE("");
    _floatOpImp('*');
}

void Interpreter::primitiveFloatDivide()
{
    ST_TRACE_PRIMITIVE("");
    _floatOpImp('/');
}

void Interpreter::primitiveTruncated()
{
    ST_TRACE_PRIMITIVE("");
    const float floatReceiver = popFloat();
    const qint32 res = floatReceiver;
    successUpdate( memory->isIntegerValue( res ) );
    if( success )
        pushInteger(res);
    else
        unPop(1);
}

void Interpreter::primitiveFractionalPart()
{
    ST_TRACE_PRIMITIVE("");
    const float floatReceiver = popFloat();
    if( success )
        pushFloat( floatReceiver - (int) floatReceiver );
    else
        unPop(1);
}

void Interpreter::primitiveExponent()
{
    ST_TRACE_PRIMITIVE("");
    primitiveFail(); // optional
}

void Interpreter::primitiveTimesTwoPower()
{
    ST_TRACE_PRIMITIVE("");
    primitiveFail(); // optional
}



