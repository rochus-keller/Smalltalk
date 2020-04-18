#ifndef STINTERPRETER_H
#define STINTERPRETER_H

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
#include <QVector>
#include <Smalltalk/StObjectMemory2.h>

namespace St
{
    class Interpreter : public QObject
    {
    public:
        typedef quint16 OOP;
        enum MethodContext { SenderIndex = 0, InstructionPointerIndex = 1,
                             StackPointerIndex = 2, MethodIndex = 3, ReceiverIndex = 5,
                           TempFrameStart = 6 };
        enum BlockContext { CallerIndex = 0, BlockArgumentCountIndex = 3, InitialIPIndex = 4,
                            HomeIndex = 5 };
        enum Registers { ActiveContext, HomeContext, Method, Receiver, MessageSelector, NewMethod };

        enum ClassIndizes {
            SuperClassIndex = 0,
            MessageDictIndex = 1,
            InstanceSpecIndex = 2,
        };

        enum ProcessScheduler {
            ProcessListIndex = 0,
            ActiveProcessIndex = 1
        };

        Interpreter(QObject* p = 0);
        void setOm( ObjectMemory2* om );
        void interpret();
        void cycle();
    protected:
        qint16 instructionPointerOfContext( OOP contextPointer );
        void storeInstructionPointerValueInContext( qint16 value, OOP contextPointer );
        qint16 stackPointerOfContext( OOP contextPointer );
        void storeStackPointerValueInContext( qint16 value, OOP contextPointer );
        qint16 argumentCountOfBlock( OOP blockPointer );
        bool isBlockContext( OOP contextPointer );
        void fetchContextRegisters();
        void storeContextRegisters();
        void push( OOP value );
        OOP popStack();
        OOP stackTop();
        OOP stackValue(qint16 offset);
        void pop(quint16 number );
        void unPop(quint16 number );
        void newActiveContext( OOP aContext );
        OOP sender();
        OOP caller();
        OOP temporary( qint16 offset );
        OOP literal(qint16 offset);
        OOP lookupMethodInDictionary(OOP dictionary, OOP selector);
        OOP lookupMethodInClass(OOP cls, OOP selector);
        OOP superclassOf(OOP cls);
        OOP instanceSpecificationOf( OOP cls );
        bool isPointers( OOP cls );
        bool isWords( OOP cls );
        bool isIndexable( OOP cls );
        qint16 fixedFieldsOf( OOP cls );
        quint8 fetchByte();
        void checkProcessSwitch();
        void dispatchOnThisBytecode();
        bool stackBytecode();
        bool returnBytecode();
        bool sendBytecode();
        bool jumpBytecode();
        bool pushReceiverVariableBytecode();
        bool pushTemporaryVariableBytecode();
        bool pushLiteralConstantBytecode();
        bool pushLiteralVariableBytecode();
        bool storeAndPopReceiverVariableBytecode();
        bool storeAndPopTemoraryVariableBytecode();
        bool pushReceiverBytecode();
        bool pushConstantBytecode();
        bool extendedPushBytecode();
        bool extendedStoreBytecode();
        bool extendedStoreAndPopBytecode();
        bool popStackBytecode();
        bool duplicateTopBytecode();
        bool pushActiveContextBytecode();
        bool shortUnconditionalJump();
        bool shortContidionalJump();
        bool longUnconditionalJump();
        bool longConditionalJump();
        bool extendedSendBytecode();
        bool singleExtendedSendBytecode();
        bool doubleExtendedSendBytecode();
        bool singleExtendedSuperBytecode();
        bool doubleExtendedSuperBytecode();
        bool sendSpecialSelectorBytecode();
        bool sendLiteralSelectorBytecode();
        void jump( quint32 offset);
        void jumpif( quint16 condition, quint32 offset );
        void sendSelector( OOP selector, quint16 argumentCount );
        void sendSelectorToClass( OOP classPointer );
        void executeNewMethod();
        bool primitiveResponse();
        void activateNewMethod();
        void transfer( quint32 count, quint16 fromIndex, OOP fromOop, quint16 firstTo, OOP toOop );
        bool specialSelectorPrimitiveResponse();
        void nilContextFields();
        void returnToActiveContext( OOP aContext );
        void returnValue( OOP resultPointer, OOP contextPointer );
        void initPrimitive();
        void successUpdate( bool ); // original name success(value)
        OOP primitiveFail();
        qint16 popInteger();
        void pushInteger(qint16);
        bool isIntegerValue(int);
        OOP positive16BitIntegerFor(int);
        int positive16BitValueOf(OOP);
        void arithmeticSelectorPrimitive();
        void commonSelectorPrimitive();
        bool isIntegerObject(OOP);
        void primitiveAdd();
        void primitiveSubtract();
        void primitiveLessThan();
        void primitiveGreaterThan();
        void primitiveLessOrEqual();
        void primitiveGreaterOrEqual();
        void primitiveEqual();
        void primitiveNotEqual();
        void primitiveMultiply();
        void primitiveDivide();
        void primitiveMod();
        void primitiveMakePoint();
        void primitiveBitShift();
        void primitiveDiv();
        void primitiveBitAnd();
        void primitiveBitOr();
        qint16 fetchIntegerOfObject(quint16 fieldIndex, OOP objectPointer );
        void storeIntegerOfObjectWithValue(quint16 fieldIndex, OOP objectPointer, int integerValue );
        void primitiveEquivalent();
        void primitiveClass();
        void primitiveBlockCopy();
        void primitiveValue();
        // NOP void quickReturnSelf();
        void quickInstanceLoad();
        void dispatchPrimitives();
        void dispatchArithmeticPrimitives();
        void dispatchSubscriptAndStreamPrimitives();
        void dispatchStorageManagementPrimitives();
        void dispatchControlPrimitives();
        void dispatchInputOutputPrimitives();
        void dispatchSystemPrimitives();
        void dispatchPrivatePrimitives();
        void dispatchIntegerPrimitives();
        void dispatchLargeIntegerPrimitives();
        void dispatchFloatPrimitives();
        void _addSubMulImp(char op);
        void primitiveQuo();
        void primitiveBitXor();
        void _compareImp(char op);
        void _bitImp(char op);
    private:
        ObjectMemory2* d_om;
        qint16 stackPointer, instructionPointer, argumentCount, primitiveIndex;
        quint8 currentBytecode;
        bool d_run, success;
    };
}

#endif // STINTERPRETER_H
