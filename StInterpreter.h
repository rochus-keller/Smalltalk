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
#include <QTimer>
#include <QVector>
#include <Smalltalk/StObjectMemory2.h>

namespace St
{
    // This is a textbook implementation according to Blue Book (BB) part 4.
    // Known to be inefficient; focus is on functionality and compliance.

    // originally implemented according to this version of the BB:
    //      http://stephane.ducasse.free.fr/FreeBooks/BlueBook/Bluebook.pdf
    // later code reviewed based on this version: http://www.mirandabanda.org/bluebook/
    //      and on the "Smalltalk-80 Virtual Image Version 2" (VIM) manual

    class Interpreter : public QObject
    {
        Q_OBJECT
    public:
        typedef quint16 OOP;
        enum MethodContext { SenderIndex = 0, // BB: The suspended context is called the new context's sender
                             InstructionPointerIndex = 1,
                             StackPointerIndex = 2, MethodIndex = 3, ReceiverIndex = 5,
                           TempFrameStart = 6 };
        enum BlockContext { CallerIndex = 0, BlockArgumentCountIndex = 3, InitialIPIndex = 4, HomeIndex = 5 };

        enum Registers { ActiveContext, HomeContext, Method, Receiver, MessageSelector, NewMethod,
                         NewProcess, InputSemaphore };

        enum MessageIndices {
            MessageSelectorIndex = 0,
            MessageArgumentsIndex = 1,
            MessageSize = 2
        };

        enum ClassIndizes {
            SuperClassIndex = 0,
            MessageDictionaryIndex = 1,
            InstanceSpecIndex = 2,
        };

        enum ProcessScheduler {
            ProcessListIndex = 0,
            ActiveProcessIndex = 1
        };

        enum StreamIndices {
            StreamArrayIndex = 0,
            StreamIndexIndex = 1,
            StreamReadLimitIndex = 2,
            StreamWriteLimitIndex = 3
        };

        enum LinkedList {
            FirstLinkIndex = 0,
            LastLinkIndex = 1
        };

        enum Semaphore {
            ExcessSignalIndex = 2
        };

        enum Link {
            NextLinkIndex = 0
        };

        enum Process {
            SuspendedContextIndex = 1,
            PriorityIndex = 2,
            MyListIndex = 3
        };

        enum Point {
            XIndex = 0, YIndex = 1, ClassPointSize = 2
        };

        Interpreter(QObject* p = 0);
        void setOm( ObjectMemory2* om );
        void interpret();
    protected slots:
        void onEvent();
        void onTimeout();
        void onBreak();

    protected:
        void cycle();
        void BREAK(bool immediate = true);
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
        bool lookupMethodInDictionary(OOP dictionary);
        bool lookupMethodInClass(OOP cls);
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
        bool storeAndPopTemporaryVariableBytecode();
        bool pushReceiverBytecode();
        bool pushConstantBytecode();
        bool extendedPushBytecode();
        bool extendedStoreBytecode(bool subcall);
        bool extendedStoreAndPopBytecode();
        bool popStackBytecode(bool subcall);
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
        void jump( qint32 offset);
        void jumpif( quint16 condition, qint32 offset );
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
        OOP positive16BitIntegerFor(quint16);
        quint16 positive16BitValueOf(OOP);
        void arithmeticSelectorPrimitive();
        void commonSelectorPrimitive();
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
        void primitiveAsFloat();
        void primitiveFloatAdd();
        void primitiveFloatSubtract();
        void primitiveFloatLessThan();
        void primitiveFloatGreaterThan();
        void primitiveFloatLessOrEqual();
        void primitiveFloatGreaterOrEqual();
        void primitiveFloatEqual();
        void primitiveFloatNotEqual();
        void primitiveFloatMultiply();
        void primitiveFloatDivide();
        void primitiveTruncated();
        void primitiveFractionalPart();
        void primitiveExponent();
        void primitiveTimesTwoPower();
        void _floatOpImp(char op);
        void _floatCompImp(char op);
        float popFloat();
        void pushFloat(float);
        void primitiveAt();
        void primitiveAtPut();
        void primitiveSize();
        void primitiveStringAt();
        void primitiveStringAtPut();
        void primitiveNext();
        void primitiveNextPut();
        void primitiveAtEnd();
        void checkIndexableBoundsOf(int index, OOP array );
        int lengthOf(OOP array);
        OOP subscriptWith(OOP array, int index);
        void subscriptWithStoring(OOP array, int index, OOP value );
        void primitiveObjectAt();
        void primitiveObjectAtPut();
        void primitiveNew();
        void primitiveNewWithArg();
        void primitiveBecome();
        void primitiveInstVarAt();
        void primitiveInstVarAtPut();
        void primitiveAsOop();
        void primitiveAsObject();
        void primitiveSomeInstance();
        void primitiveNextInstance();
        void primitiveNewMethod();
        void checkInstanceVariableBoundsOf( int index, OOP object );
        void primitiveValueWithArgs();
        void primitivePerform();
        void primitivePerformWithArgs();
        void primitiveSignal();
        void primitiveWait();
        void primitiveResume();
        void primitiveSuspend();
        void primitiveFlushCache();
        void asynchronousSignal(OOP aSemaphore);
        bool isEmptyList(OOP aLinkedList);
        void synchronousSignal(OOP aSemaphore);
        OOP removeFirstLinkOfList(OOP);
        void transferTo(OOP aProcess);
        OOP activeProcess();
        OOP schedulerPointer();
        OOP firstContext();
        void addLastLinkToList( OOP aLink, OOP aLinkedList );
        OOP wakeHighestPriority();
        void sleep(OOP aProcess);
        void resume(OOP aProcess);
        void suspendActive();
        void primitiveQuit();
        void createActualMessage();
        void sendMustBeBoolean();
        QByteArray prettyArgs_();
        void primitiveBeDisplay();
        void primitiveCopyBits();
        void primitiveStringReplace();
        void dumpStack_(const char* title = "");
        void primitiveBeCursor();
        void primitiveCursorLink();
        void primitiveInputSemaphore();
        void primitiveInputWord();
        void primitiveSamleInterval();
        void primitiveMousePoint();
        void primitiveSignalAtOopsLeftWordsLeft();
        void primitiveCursorLocPut();
        void primitiveTimeWordsInto();
        void primitiveTickWordsInto();
        void primitiveSignalAtTick();
        void primitiveAltoFile();
        static inline quint16 extractBits( quint8 from, quint8 to, quint16 of )
        {
            Q_ASSERT( from <= to && to <= 15 );
            return ( of >> ( 15 - to ) ) & ( ( 1 << ( to - from + 1 ) ) - 1 );
        }
        static inline quint16 lowByteOf( quint16 anInteger )
        {
            return extractBits( 8, 15, anInteger );
        }
        static inline quint16 highByteOf( quint16 anInteger )
        {
            return extractBits( 0, 7, anInteger );
        }
        static inline quint16 literalCountOfHeader(OOP headerPointer )
        {
            return extractBits( 9, 14, headerPointer );
        }
    private:
        ObjectMemory2* memory;
        qint16 stackPointer, instructionPointer, argumentCount, primitiveIndex;
        QList<OOP> semaphoreList;
        quint32 cycleNr, level;
        QByteArray prevMsg;
        QTimer d_timer;
        OOP toSignal;
        quint8 currentBytecode;
        bool success, newProcessWaiting;
    };
}

#endif // STINTERPRETER_H
