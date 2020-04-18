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

#include "StInterpreter.h"
#include <QtDebug>
using namespace St;

Interpreter::Interpreter(QObject* p):QObject(p),d_om(0),stackPointer(0),instructionPointer(0),d_run(false)
{

}

void Interpreter::setOm(ObjectMemory2* om)
{
    d_om = om;
}

void Interpreter::interpret()
{
    d_run = true;
    while( d_run )
        cycle();
}

qint16 Interpreter::instructionPointerOfContext(Interpreter::OOP contextPointer)
{
    return ObjectMemory2::toInt(d_om->fetchWordOfObject(InstructionPointerIndex,contextPointer));
}

void Interpreter::storeInstructionPointerValueInContext(qint16 value, Interpreter::OOP contextPointer)
{
    d_om->storeWordOfObject( InstructionPointerIndex, contextPointer, ObjectMemory2::toPtr(value) );
}

qint16 Interpreter::stackPointerOfContext(Interpreter::OOP contextPointer)
{
    return ObjectMemory2::toInt(d_om->fetchWordOfObject(StackPointerIndex,contextPointer));
}

void Interpreter::storeStackPointerValueInContext(qint16 value, Interpreter::OOP contextPointer)
{
    d_om->storeWordOfObject( StackPointerIndex, contextPointer, ObjectMemory2::toPtr(value) );
}

qint16 Interpreter::argumentCountOfBlock(Interpreter::OOP blockPointer)
{
    return ObjectMemory2::toInt(d_om->fetchWordOfObject(BlockArgumentCountIndex,blockPointer));
}

bool Interpreter::isBlockContext(Interpreter::OOP contextPointer)
{
    const OOP methodOrArguments = d_om->fetchWordOfObject(MethodIndex, contextPointer);
    return !ObjectMemory2::isPointer(methodOrArguments);
}

void Interpreter::fetchContextRegisters()
{
    OOP activeContext = d_om->getRegister(ActiveContext);
    OOP homeContext = 0;
    if( isBlockContext(activeContext) )
        homeContext = d_om->fetchPointerOfObject(HomeIndex,activeContext);
    else
        homeContext = activeContext;
    d_om->setRegister(HomeContext,homeContext);
    d_om->setRegister(Receiver, d_om->fetchPointerOfObject(ReceiverIndex,homeContext) );
    d_om->setRegister(Method, d_om->fetchPointerOfObject(MethodIndex,homeContext) );
    instructionPointer = instructionPointerOfContext(activeContext) - 1;
    stackPointer = stackPointerOfContext(activeContext) + TempFrameStart - 1;
}

void Interpreter::storeContextRegisters()
{
    OOP activeContext = d_om->getRegister(ActiveContext);
    storeInstructionPointerValueInContext( instructionPointer + 1, activeContext );
    storeStackPointerValueInContext( stackPointer - TempFrameStart + 1, activeContext );
}

void Interpreter::push(Interpreter::OOP value)
{
    stackPointer++;
    d_om->storePointerOfObject( stackPointer, d_om->getRegister(ActiveContext), value);
}

Interpreter::OOP Interpreter::popStack()
{
    OOP stackTop = d_om->fetchPointerOfObject( stackPointer, d_om->getRegister(ActiveContext) );
    stackPointer--;
    return stackTop;
}

Interpreter::OOP Interpreter::stackTop()
{
    return d_om->fetchPointerOfObject( stackPointer, d_om->getRegister(ActiveContext) );
}

Interpreter::OOP Interpreter::stackValue(qint16 offset)
{
    return d_om->fetchPointerOfObject( stackPointer - offset, d_om->getRegister(ActiveContext) );
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
    storeContextRegisters();
    d_om->setRegister(ActiveContext, aContext);
    fetchContextRegisters();
}

Interpreter::OOP Interpreter::sender()
{
    return d_om->fetchPointerOfObject(SenderIndex, d_om->getRegister(HomeContext) );
}

Interpreter::OOP Interpreter::caller()
{
    return d_om->fetchPointerOfObject(CallerIndex, d_om->getRegister(HomeContext) );
}

Interpreter::OOP Interpreter::temporary(qint16 offset)
{
    return d_om->fetchPointerOfObject( offset + TempFrameStart, d_om->getRegister(HomeContext) );
}

Interpreter::OOP Interpreter::literal(qint16 offset)
{
    return d_om->methodLiteral( offset, d_om->getRegister(Method) );
}

Interpreter::OOP Interpreter::lookupMethodInDictionary(Interpreter::OOP dictionary, OOP selector)
{
    // Just a trivial linear scan; not the more fancy hash lookup described in the Blue Book
    OOP arr = d_om->fetchPointerOfObject(1,dictionary);
    const int SelectorStart = 2;
    for( int i = SelectorStart; i < d_om->fetchWordLenghtOf(dictionary); i++ )
    {
        OOP sym = d_om->fetchPointerOfObject(i,dictionary);
        if( sym == selector )
            return d_om->fetchPointerOfObject(i-SelectorStart,arr);
    }
    return 0;
}

Interpreter::OOP Interpreter::lookupMethodInClass(Interpreter::OOP cls, Interpreter::OOP selector)
{
    while( cls == ObjectMemory2::objectNil )
    {
        OOP res = lookupMethodInDictionary( d_om->fetchPointerOfObject(MessageDictIndex,cls), selector );
        if( res )
            return res;
        cls = lookupMethodInClass( superclassOf(cls), selector );
    }
    if( selector == ObjectMemory2::symbolDoesNotUnderstand )
    {
        qCritical() << "Recursive not understood error encountered";
        return 0;
    }
    // TODO createActualMessage
    return ObjectMemory2::symbolDoesNotUnderstand;
}

Interpreter::OOP Interpreter::superclassOf(Interpreter::OOP cls)
{
    return d_om->fetchPointerOfObject(SuperClassIndex,cls);
}

Interpreter::OOP Interpreter::instanceSpecificationOf(Interpreter::OOP cls)
{
    return d_om->fetchPointerOfObject(InstanceSpecIndex,cls);
}

bool Interpreter::isPointers(Interpreter::OOP cls)
{
    return instanceSpecificationOf(cls) & 0x8000;
}

bool Interpreter::isWords(Interpreter::OOP cls)
{
    return instanceSpecificationOf(cls) & 0x4000;
}

bool Interpreter::isIndexable(Interpreter::OOP cls)
{
    return instanceSpecificationOf(cls) & 0x2000;
}

qint16 Interpreter::fixedFieldsOf(Interpreter::OOP cls)
{
    return ( ( instanceSpecificationOf(cls) >> 1 ) & 0x7ff );
}

quint8 Interpreter::fetchByte()
{
    return d_om->fetchByteOfObject(instructionPointer++, d_om->getRegister(Method) );
}

void Interpreter::cycle()
{
    checkProcessSwitch();
    currentBytecode = fetchByte();
    dispatchOnThisBytecode();
}

void Interpreter::checkProcessSwitch()
{
    // TODO
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
        qWarning() << "hit unused bytecode";
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
        return storeAndPopTemoraryVariableBytecode();
    if( b == 112 )
        return pushConstantBytecode();
    if( b >= 113 && b <= 119 )
        return pushConstantBytecode();
    if( b == 128 )
        return extendedPushBytecode();
    if( b == 129 )
        return extendedStoreBytecode();
    if( b == 130 )
        return extendedStoreAndPopBytecode();
    if( b == 135 )
        return popStackBytecode();
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
        returnValue( d_om->getRegister(Receiver), sender() );
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
        qWarning() << "executing unused bytecode" << currentBytecode;
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
    push( d_om->fetchPointerOfObject( currentBytecode & 0xf, d_om->getRegister(Receiver) ) );
    // "Push Receiver Variable #%1").arg( b & 0xf ), 1 );
    return true;
}

bool Interpreter::pushTemporaryVariableBytecode()
{
    // "Push Temporary Location #%1").arg( b & 0xf ), 1 );
    push( temporary( currentBytecode & 0xf ) );
    return true;
}

bool Interpreter::pushLiteralConstantBytecode()
{
    // "Push Literal Constant #%1").arg( b & 0x1f ), 1 );
    push( literal( currentBytecode & 0x1f ) );
    return true;
}

static const quint16 ValueIndex = 1;

bool Interpreter::pushLiteralVariableBytecode()
{
    // "Push Literal Variable #%1").arg( b & 0x1f ), 1 );
    quint16 assoc = literal( currentBytecode & 0x1f );
    push( d_om->fetchPointerOfObject( ValueIndex, assoc ) );
    return true;
}

bool Interpreter::storeAndPopReceiverVariableBytecode()
{
    // "Pop and Store Receiver Variable #%1").arg( b & 0x7 ), 1 );
    d_om->storePointerOfObject( currentBytecode  & 0x7, d_om->getRegister(Receiver), popStack() );
    return true;
}

bool Interpreter::storeAndPopTemoraryVariableBytecode()
{
    // "Pop and Store Temporary Location #%1").arg( b & 0x7 ), 1 );
    d_om->storePointerOfObject( ( currentBytecode & 0x7 ) + TempFrameStart, d_om->getRegister(HomeContext), popStack() );
    return true;
}

bool Interpreter::pushReceiverBytecode()
{
    push( d_om->getRegister(Receiver) );
    return true;
}

bool Interpreter::pushConstantBytecode()
{
    // "Push (receiver, true, false, nil, -1, 0, 1, 2) [%1]").arg( b & 0x7 ), 1 );
    switch( currentBytecode )
    {
    case 113:
        push( ObjectMemory2::objectTrue );
        break;
    case 114:
        push( ObjectMemory2::objectFalse );
        break;
    case 115:
        push( ObjectMemory2::objectNil );
        break;
    case 116:
        push( ObjectMemory2::objectMinusOne );
        break;
    case 117:
        push( ObjectMemory2::objectZero );
        break;
    case 118:
        push( ObjectMemory2::objectOne );
        break;
    case 119:
        push( ObjectMemory2::objectTwo );
        break;
    default:
        Q_ASSERT( false );
        break;
    }
    return true;
}

bool Interpreter::extendedPushBytecode()
{
    // "Push (Receiver Variable, Temporary Location, Literal Constant, Literal Variable) [%1] #%2").
                              // arg( ( bc[pc+1] >> 6 ) & 0x3 ).arg( bc[pc+1] & 0x3f), 2 );
    const quint16 descriptor = fetchByte();
    const quint8 variableType = ( descriptor >> 6 & 0x3 );
    const quint8 variableIndex = descriptor & 0x3f;
    switch( variableType )
    {
    case 0:
        push( d_om->fetchPointerOfObject( variableIndex, d_om->getRegister(Receiver) ) );
        break;
    case 1:
        push( temporary( variableIndex ) );
        break;
    case 2:
        push( literal( variableIndex ) );
        break;
    case 3:
        push( d_om->fetchPointerOfObject( ValueIndex, literal( variableIndex ) ) );
        break;
    default:
        Q_ASSERT( false );
    }
    return true;
}

bool Interpreter::extendedStoreBytecode()
{
    // "Store (Receiver Variable, Temporary Location, Illegal, Literal Variable) [%1] #%2").
                              // arg( ( bc[pc+1] >> 6 ) & 0x3 ).arg( bc[pc+1] & 0x3f), 2 );
    const quint16 descriptor = fetchByte();
    const quint16 variableType = ( descriptor >> 6 ) & 0x3;
    const quint16 variableIndex = descriptor & 0x3f;
    switch( variableType )
    {
    case 0:
        d_om->storePointerOfObject(variableIndex,d_om->getRegister(Receiver),stackTop());
        break;
    case 1:
        d_om->storePointerOfObject(variableIndex+TempFrameStart,d_om->getRegister(HomeContext),stackTop());
        break;
    case 2:
        qCritical() << "illegal store";
        break;
    case 3:
        d_om->storePointerOfObject(ValueIndex, literal(variableIndex), stackTop() );
        break;
    default:
        Q_ASSERT( false );
    }
    return true;
}

bool Interpreter::extendedStoreAndPopBytecode()
{
    // "Pop and Store (Receiver Variable, Temporary Location, Illegal, Literal Variable) [%1] #%2").
                             // arg( ( bc[pc+1] >> 6 ) & 0x3 ).arg( bc[pc+1] & 0x3f), 2 );
    extendedStoreBytecode();
    return popStackBytecode();
}

bool Interpreter::popStackBytecode()
{
    // "Pop Stack Top" ), 1 );
    popStack();
    return true;
}

bool Interpreter::duplicateTopBytecode()
{
    // "Duplicate Stack Top" ), 1 );
    push( stackTop() );
    return true;
}

bool Interpreter::pushActiveContextBytecode()
{
    // "Push Active Context" ), 1 );
    push( d_om->getRegister( ActiveContext ) );
    return true;
}

bool Interpreter::shortUnconditionalJump()
{
     // "Jump %1 + 1 (i.e., 1 through 8)").arg( b & 0x7 ), 1 );
    const quint32 offset = currentBytecode & 0x7;
    jump( offset + 1 );
    return true;
}

bool Interpreter::shortContidionalJump()
{
    // "Pop and Jump 0n False %1 +1 (i.e., 1 through 8)").arg( b & 0x7 ), 1 );
    const quint32 offset = currentBytecode & 0x7;
    jumpif( ObjectMemory2::objectFalse, offset + 1 );
    return true;
}

bool Interpreter::longUnconditionalJump()
{
    // "Jump(%1 - 4) *256+%2").arg( b & 0x7 ).arg( bc[pc+1] ), 2 );
    const quint32 offset = currentBytecode & 0x7;
    jump( ( offset - 4 ) * 256 + fetchByte() );
    return true;
}

bool Interpreter::longConditionalJump()
{
    // "Pop and Jump On True %1 *256+%2").arg( b & 0x3 ).arg( bc[pc+1] ), 2 );
    // "Pop and Jump On False %1 *256+%2").arg( b & 0x3 ).arg( bc[pc+1] ), 2 );

    quint32 offset = currentBytecode & 0x3;
    offset = offset * 256 + fetchByte();
    if( currentBytecode <= 171 )
        jumpif( ObjectMemory2::objectTrue, offset + 1 ); // TODO: stimmt + 1? BB fehlt das
    else
        jumpif( ObjectMemory2::objectFalse, offset + 1 );
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
    const quint8 descriptor = fetchByte();
    const quint8 selectorIndex = descriptor & 0x1f;
    sendSelector( literal(selectorIndex), ( descriptor >> 5 ) & 0x7 );
    return true;
}

bool Interpreter::doubleExtendedSendBytecode()
{
    // "Send Literal Selector #%2 With %1 Arguments").arg( bc[pc+1] ).arg( bc[pc+2]), 3 );
    const quint8 count = fetchByte();
    const OOP selector = literal( fetchByte() );
    sendSelector( selector, count );
    return true;
}

bool Interpreter::singleExtendedSuperBytecode()
{
    // "Send Literal Selector #%2 To Superclass With %1 Arguments").arg( ( bc[pc+1] >> 5 ) & 0x7 ).arg( bc[pc+1] & 0x1f), 2 );
    const quint8 descriptor = fetchByte();
    argumentCount = ( descriptor >> 5 ) & 0x7;
    const quint16 selectorIndex = descriptor & 0x1f;
    d_om->setRegister( MessageSelector, literal( selectorIndex ));
    OOP methodClass = d_om->methodClassOf( d_om->getRegister(Method) );
    sendSelectorToClass( superclassOf(methodClass) );
    return true;
}

bool Interpreter::doubleExtendedSuperBytecode()
{
    // "Send Literal Selector #%2 To Superclass With %1 Arguments").arg( bc[pc+1] ).arg( bc[pc+2]), 3 );
    argumentCount = fetchByte();
    d_om->setRegister( MessageSelector, literal( fetchByte() ));
    OOP methodClass = d_om->methodClassOf( d_om->getRegister(Method) );
    sendSelectorToClass( superclassOf(methodClass) );
    return true;
}

bool Interpreter::sendSpecialSelectorBytecode()
{
    // see array 0x30 specialSelectors
    // "Send Arithmetic Message #%1" ).arg( b & 0xf ), 1 );
    // "Send Special Message #%1" ).arg( b & 0xf ), 1 );
    if( specialSelectorPrimitiveResponse() )
        return true;
    const quint16 selectorIndex = ( currentBytecode - 176 ) * 2;
    OOP selector = d_om->fetchPointerOfObject(selectorIndex, ObjectMemory2::specialSelectors );
    const quint16 count = ObjectMemory2::toInt( d_om->fetchWordOfObject(
                                                    selectorIndex + 1, ObjectMemory2::specialSelectors ) );
    sendSelector( selector, count );
    return true;
}

bool Interpreter::sendLiteralSelectorBytecode()
{
    // "Send Literal Selector #%1 With No Arguments" ).arg( b & 0xf ), 1 );
    // "Send Literal Selector #%1 With 1 Argument" ).arg( b & 0xf ), 1 );
    // "Send Literal Selector #%1 With 2 Arguments" ).arg( b & 0xf ), 1 );

    return true; // TODO
}

void Interpreter::jump(quint32 offset)
{
    instructionPointer += offset;
}

void Interpreter::jumpif(quint16 condition, quint32 offset)
{
    const quint16 boolean = popStack();
    if( boolean == condition )
        jump(offset);
    else if( !( boolean == ObjectMemory2::objectTrue || boolean == ObjectMemory2::objectFalse ) )
    {
        unPop(1);
        qCritical() << "must be boolean"; // TODO send
    }
}

void Interpreter::sendSelector(Interpreter::OOP selector, quint16 count)
{
    d_om->setRegister(MessageSelector, selector );
    argumentCount = count;
    OOP newReceiver = stackValue(argumentCount);
    sendSelectorToClass( d_om->fetchClassOf(newReceiver) );
}

void Interpreter::sendSelectorToClass(Interpreter::OOP classPointer)
{
    // original: findNewMethodInClass
    lookupMethodInClass(classPointer,d_om->getRegister(MessageSelector));
    executeNewMethod();
}

void Interpreter::executeNewMethod()
{
    if( !primitiveResponse() )
        activateNewMethod();
}

bool Interpreter::primitiveResponse()
{
    if( primitiveIndex == 0 )
    {
        quint8 flagValue = d_om->methodFlags( d_om->getRegister(NewMethod) );
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
    quint16 contextSize = TempFrameStart;
    OOP newMethod = d_om->getRegister(NewMethod);
    if( d_om->methodLargeContext( newMethod ) )
        contextSize += 32;
    else
        contextSize += 12;
    OOP newContext = d_om->instantiateClassWithPointers(ObjectMemory2::classMethodContext,contextSize);
    d_om->storePointerOfObject(SenderIndex, newContext, d_om->getRegister(ActiveContext) );
    storeInstructionPointerValueInContext( d_om->methodInitialInstructionPointer( newMethod ), newContext );
    storeStackPointerValueInContext( d_om->methodTemporaryCount( newMethod ), newContext );
    d_om->storePointerOfObject(MethodIndex,newContext,newMethod);
    transfer( argumentCount + 1, stackPointer - argumentCount, d_om->getRegister(ActiveContext),
              ReceiverIndex, newContext );
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
        OOP oop = d_om->fetchPointerOfObject(fromIndex, fromOop);
        d_om->storePointerOfObject(toIndex,toOop,oop);
        d_om->storePointerOfObject(fromIndex,fromOop,ObjectMemory2::objectNil );
        fromIndex += 1;
        toIndex += 1;
    }
}

bool Interpreter::specialSelectorPrimitiveResponse()
{
    if( currentBytecode >= 176 && currentBytecode <= 191 )
        arithmeticSelectorPrimitive();
    else if( currentBytecode >= 192 && currentBytecode <= 207 )
        commonSelectorPrimitive();
    return success;
}

void Interpreter::nilContextFields()
{
    d_om->storePointerOfObject( SenderIndex, d_om->getRegister( ActiveContext ), ObjectMemory2::objectNil );
    d_om->storePointerOfObject( InstructionPointerIndex, d_om->getRegister( ActiveContext ), ObjectMemory2::objectNil );
}

void Interpreter::returnToActiveContext(Interpreter::OOP aContext)
{
    d_om->addTemp(aContext);
    nilContextFields();
    d_om->removeTemp(aContext);
    d_om->setRegister(ActiveContext,aContext);
    fetchContextRegisters();
}

void Interpreter::returnValue(Interpreter::OOP resultPointer, Interpreter::OOP contextPointer)
{
    if( contextPointer == ObjectMemory2::objectNil )
    {
        push( d_om->getRegister(ActiveContext) );
        push( resultPointer );
        sendSelector(ObjectMemory2::symbolCannotReturn, 1 );
    }
    OOP sendersIP = d_om->fetchPointerOfObject( InstructionPointerIndex, contextPointer );
    if( sendersIP == ObjectMemory2::objectNil )
    {
        push( d_om->getRegister(ActiveContext) );
        push( resultPointer );
        sendSelector(ObjectMemory2::symbolCannotReturn, 1 );
    }
    d_om->addTemp(resultPointer);
    returnToActiveContext(contextPointer);
    push( resultPointer );
    d_om->removeTemp(resultPointer);
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
    OOP res = popStack();
    return ObjectMemory2::toInt(res);
}

void Interpreter::pushInteger(qint16 i)
{
    push( ObjectMemory2::toPtr(i));
}

bool Interpreter::isIntegerValue(int i)
{
    return i < 16834 && i > - 16834; // TODO error in BB?
}

Interpreter::OOP Interpreter::positive16BitIntegerFor(int i)
{
    if( i < 0 )
        return primitiveFail();
    if( isIntegerValue(i) )
        return ObjectMemory2::toPtr(i);
    OOP newLargeInteger = d_om->instantiateClassWithBytes(ObjectMemory2::classLargePositiveInteger, 2);
    d_om->storeByteOfObject( 0, newLargeInteger, i & 0xff );
    d_om->storeByteOfObject( 1, newLargeInteger, ( i >> 8 ) & 0xff );
    return newLargeInteger;
}

int Interpreter::positive16BitValueOf(Interpreter::OOP oop)
{
    if( !ObjectMemory2::isPointer(oop) )
        return ObjectMemory2::toInt(oop);
    if( d_om->fetchClassOf(oop) != ObjectMemory2::classLargePositiveInteger )
        return primitiveFail();
    if( d_om->fetchByteLenghtOf(oop) != 2 )
        return primitiveFail();
    int value = d_om->fetchByteOfObject(oop,1) << 8;
    value += d_om->fetchByteOfObject(oop,0);
    return value;
}

void Interpreter::arithmeticSelectorPrimitive()
{
    successUpdate( isIntegerObject( stackValue(1) ) );
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
    OOP receiverClass = d_om->fetchClassOf( stackValue( argumentCount ) );
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
    }
    primitiveFail();
}

bool Interpreter::isIntegerObject(Interpreter::OOP oop)
{
    return !ObjectMemory2::isPointer(oop);
}

void Interpreter::primitiveAdd()
{
    _addSubMulImp('+');
}

void Interpreter::primitiveSubtract()
{
    _addSubMulImp('-');
}

void Interpreter::primitiveLessThan()
{
    _compareImp('<');
}

void Interpreter::primitiveGreaterThan()
{
    _compareImp('>');
}

void Interpreter::primitiveLessOrEqual()
{
    _compareImp('l');
}

void Interpreter::primitiveGreaterOrEqual()
{
    _compareImp('g');
}

void Interpreter::primitiveEqual()
{
    _compareImp('=');
}

void Interpreter::primitiveNotEqual()
{
    _compareImp('!');
}

void Interpreter::primitiveMultiply()
{
    _addSubMulImp('*');
}

void Interpreter::primitiveDivide()
{
    const qint16 integerArgument = popInteger();
    const qint16 integerReceiver = popInteger();
    successUpdate( integerArgument != 0 );
    successUpdate( integerReceiver % integerArgument == 0 );
    qint16 integerResult = 0;
    if( success )
    {
        integerResult = integerReceiver / integerArgument;
        successUpdate( isIntegerValue(integerResult) );
    }
    if( success )
        push( ObjectMemory2::toPtr( integerResult ) );
    else
        unPop(2);
}

void Interpreter::primitiveMod()
{
    const qint16 integerArgument = popInteger();
    const qint16 integerReceiver = popInteger();
    successUpdate( integerArgument != 0 );
    qint16 integerResult = 0;
    if( success )
    {
        integerResult = integerReceiver % integerArgument; // TODO quotient always rounded down towards negative infinity?
        successUpdate( isIntegerValue(integerResult) );
    }
    if( success )
        pushInteger( integerResult );
    else
        unPop(2);
}

void Interpreter::primitiveMakePoint()
{
    enum { XIndex = 0, YIndex = 1, ClassPointSize = 2 };

    OOP integerArgument = popStack();
    OOP integerReceiver = popStack();
    successUpdate( isIntegerValue(integerArgument) );
    successUpdate( isIntegerValue(integerReceiver) );

    if( success )
    {
        OOP pointResult = d_om->instantiateClassWithPointers( ObjectMemory2::classPoint, ClassPointSize );
        d_om->storePointerOfObject( XIndex, pointResult, integerReceiver );
        d_om->storePointerOfObject( YIndex, pointResult, integerArgument );
        push( pointResult );
    }
    else
        unPop(2);
}

void Interpreter::primitiveBitShift()
{
    const qint16 integerArgument = popInteger();
    const qint16 integerReceiver = popInteger();
    successUpdate( integerArgument != 0 );
    qint16 integerResult = 0;
    if( success )
    {
        if( integerArgument >= 0 ) // TODO: check
            integerResult = integerReceiver << integerArgument;
        else
            integerResult = integerReceiver >> -integerArgument;
        successUpdate( isIntegerValue(integerResult) );
    }
    if( success )
        pushInteger( integerResult );
    else
        unPop(2);
}

void Interpreter::primitiveDiv()
{
    const qint16 integerArgument = popInteger();
    const qint16 integerReceiver = popInteger();
    successUpdate( integerArgument != 0 );
    qint16 integerResult = 0;
    if( success )
    {
        integerResult = integerReceiver / integerArgument; // TODO quotient always rounded down towards negative infinity?
        successUpdate( isIntegerValue(integerResult) );
    }
    if( success )
        pushInteger( integerResult );
    else
        unPop(2);
}

void Interpreter::primitiveBitAnd()
{
    _bitImp('&');
}

void Interpreter::primitiveBitOr()
{
    _bitImp('|');
}

qint16 Interpreter::fetchIntegerOfObject(quint16 fieldIndex, Interpreter::OOP objectPointer)
{
    OOP integerPointer = d_om->fetchPointerOfObject(fieldIndex,objectPointer);
    if( isIntegerObject(integerPointer) )
        return ObjectMemory2::toInt(integerPointer);
    else
        return primitiveFail();
}

void Interpreter::storeIntegerOfObjectWithValue(quint16 fieldIndex, Interpreter::OOP objectPointer, int integerValue)
{
    if( isIntegerValue( integerValue ) )
    {
        OOP integerPointer = ObjectMemory2::toPtr(integerValue);
        d_om->storePointerOfObject(fieldIndex,objectPointer,integerPointer);
    }else
        primitiveFail();
}

void Interpreter::quickInstanceLoad()
{
    OOP thisReceiver = popStack();
    quint16 fieldIndex = d_om->methodFieldIndex( d_om->getRegister(NewMethod) );
    push( d_om->fetchPointerOfObject( fieldIndex, thisReceiver ) );
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
    primitiveFail();
}

void Interpreter::dispatchArithmeticPrimitives()
{
    if( primitiveIndex < 20 )
        dispatchIntegerPrimitives();
    else if( primitiveIndex < 40 )
        dispatchLargeIntegerPrimitives();
    if( primitiveIndex < 60 )
        dispatchFloatPrimitives();
    primitiveFail();
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
    }
    primitiveFail();
}

void Interpreter::dispatchLargeIntegerPrimitives()
{
    primitiveFail();
}

void Interpreter::dispatchFloatPrimitives()
{
    // Examples of Float instance bytes
    // 0x40490fda, 0x3f80, empty, 0x3f, 0x40, 0x460ff0
}

void Interpreter::_addSubMulImp(char op)
{
    const qint16 integerArgument = popInteger();
    const qint16 integerReceiver = popInteger();
    qint16 integerResult = 0;
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
        successUpdate( isIntegerValue(integerResult) );
    }
    if( success )
        pushInteger( integerResult );
    else
        unPop(2);
}

void Interpreter::primitiveQuo()
{
    const double integerArgument = popInteger();
    const double integerReceiver = popInteger();
    successUpdate( integerArgument != 0 );
    qint16 integerResult = 0;
    if( success )
    {
        integerResult = qRound( integerReceiver / integerArgument );
        successUpdate( isIntegerValue(integerResult) );
    }
    if( success )
        pushInteger( integerResult );
    else
        unPop(2);
}

void Interpreter::primitiveBitXor()
{
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



