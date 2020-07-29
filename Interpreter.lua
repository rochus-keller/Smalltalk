--[[
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
]]--

--[[
	This is the code from StInterpreter.cpp migrated to Lua.
	
	What I missed from Lua for this project:
	
	- #ifdef to hide statements only used for debugging and to avoid wasting calculation time 
	- constants not requiring local slots or hashed element access
	- explicit inline declaration, so I can better structure the code without additionl context 
	  switch and slot consumption
	- avoid implicit global declarations; each typo is only detected at runtime otherwise
	- explicit global declarations
	- compiler should complain about use of locals only declared later in the file
	( - switch/case control statement to avoid writing the full relation all over again )
]]--

------------------ Imports ------------------------------------------
local ffi = require 'ffi'
local C = ffi.C
local memory = require 'ObjectMemory'
local bit = require("bit")
local module = {}
local string = require "string"

ffi.cdef[[
    int St_DIV( int a, int b );
    int St_MOD( int a, int b );
    int St_isRunning();
    void St_processEvents();
    int St_extractBits(int from, int to, int word);
    int St_isIntegerValue( double val );
    int St_round( double val );
	typedef struct{ 
		int count; 
		uint8_t data[?];
	} ByteArray;
	typedef struct{
	    int count; // word count
	    uint16_t data[];
	} WordArray;
    uint32_t St_toUInt( ByteArray* ba );
    void St_setCursorPos( int x, int y );
    int St_nextEvent();
    void St_stop();
    void St_start();
    void St_log( const char* msg );
    const char* St_toString( ByteArray* ba );
    int St_extractBitsSi(int from, int to, int word);
    int St_pendingEvents();
    void St_beDisplay( WordArray* wa, int width, int height );
    void St_beCursor( WordArray* wa, int width, int height );
    void St_bitBlt( WordArray* destBits, int destW, int destH,
                          WordArray* sourceBits, int srcW, int srcH,
                          WordArray* htBits, int htW, int htH,
                          int combinationRule,
                          int destX, int destY, int width, int height,
                          int sourceX, int sourceY,
                          int clipX, int clipY, int clipWidth, int clipHeight );
    void St_timeWords( ByteArray* );
    void St_tickWords( ByteArray* );
    void St_wakeupOn( ByteArray* );
    int St_itsTime();
    void St_update( WordArray* destBits,
                          int destX, int destY, int width, int height,
                          int clipX, int clipY, int clipWidth, int clipHeight );
    void St_copyToClipboard( ByteArray* );
    int St_openFile( ByteArray* ba );
    int St_closeFile( int fd );
    int St_fileSize( int fd );
    int St_seekFile( int fd, int pos );
    int St_readFile( int fd, ByteArray* ba );
    int St_writeFile( int fd, ByteArray* ba, int toWrite );
    int St_truncateFile( int fd, int size );
    int St_createFile( ByteArray* ba );
    int St_deleteFile( ByteArray* ba );
    int St_renameFile( ByteArray* from, ByteArray* to );
]]

------------------ Module Data ------------------------------------------
local currentBytecode = 0
local instructionPointer = 0
local stackPointer = 0
local argumentCount = 0
local primitiveIndex = 0
local method, methodBytecode
local activeContext
local homeContext
local receiver
local messageSelector
local newMethod
local newProcess
local inputSemaphore
local semaphoreList = {}
local semaphoreIndex = 0
local newProcessWaiting = false
local primitive = {}
local success = true
local cycleNr = 0
local toSignal

------------------ Cached Objects ------------------------------------------
local bitand
local classSmallInteger
local classLargePositiveInteger
local classFloat
local classCompiledMethod
local classCharacter
local mathfloor
local classTrue
local classFalse
-- NOTE: requires call to memory.loadImage first before (most) cache can be filled

------------------ Functions ------------------------------------------

local function fetchClassOf(objectPointer)
	if objectPointer == true then
		return classTrue
	elseif objectPointer == false then
		return classFalse
	else
		return getmetatable(objectPointer)
	end
end

local function prettyValue( value )
	if value == nil then
		return "nil"
	elseif value == true then
		return "true"
	elseif value == false then
		return "false"
	end
	
	local cls = fetchClassOf(value)
	local knowns = memory.knownObjects
	
	if cls == classSmallInteger then
		return tostring(mathfloor(value))
	elseif cls == classLargePositiveInteger then
		return tostring(C.St_toUInt(value.data)) .. "L"
	elseif cls == classFloat then
		return tostring(value[0]) .. "F"
	elseif cls == classCharacter then
		local ch = value[0]
		if ch > 0x20 and ch < 0x7f then
			return "'" .. string.char(ch) .. "'"
		else
			return "0x" .. string.format("%x",ch)
		end
	elseif cls == knowns[0x38] then -- Symbol
		return 	"#" .. ffi.string(C.St_toString( value.data ))
	elseif cls == knowns[0x1a] then -- Point
		return prettyValue(value[0]) .. "@" .. prettyValue(value[1])
	elseif cls == knowns[0x0e] then -- String
		local str = ffi.string(C.St_toString( value.data ))
		local len = string.len(str)
		local suff = ""
		if len > 32 then
			suff = ".."
		end
		str = string.gsub(string.sub(str,1,32), "%s+", " ")
		return 	"\"" .. str .. "\"" .. suff
	elseif cls.oop == 0x84 then -- Association
		return prettyValue(value[0]) .. " = " .. prettyValue(value[1])
	else
		local sym = cls[6]
		if fetchClassOf(sym) ~= knowns[0x38] then
			sym = sym[6]
		end
		assert( fetchClassOf(sym) == knowns[0x38] )
		local str = "<a " .. ffi.string(C.St_toString( sym.data )) .. ">"
		return str
	end		
end

local function ST_TRACE_BYTECODE(...)
	-- TODO: comment out all TRACE calls when no longer needed!
	--TRACE( string.format("Bytecode <%d>\t[%d]", currentBytecode, cycleNr ), ... ) 
end

local function ST_TRACE_METHOD_CALL(...)
	-- TODO: comment out all TRACE calls when no longer needed!
	-- TRACE( "Cycle", cycleNr, "Call", ... )
end

local function ST_TRACE_PRIMITIVE(...)
	-- TODO: comment out all TRACE calls when no longer needed!
	-- TRACE( "Primitive", primitiveIndex, ... )
end

local function fetchByte()
	local offset = instructionPointer-(method.count+1)*2
	local byteCode = methodBytecode.data[offset]
	instructionPointer = instructionPointer + 1
	return byteCode
end

local function push( value )
	stackPointer = stackPointer + 1
	activeContext[stackPointer] = value
end

local function temporary(offset) -- used twice
    return homeContext[ offset + 6 ] -- TempFrameStart
end

local function literal(offset) -- used ten times
	return method[offset]
end

local function popStack()
    local stackTop = activeContext[stackPointer]
    stackPointer = stackPointer - 1
    return stackTop
end

local function stackTop() -- used ten times
	return activeContext[stackPointer]
end

local function extendedStoreBytecode() -- used twice
    local descriptor = fetchByte()
    local variableType = C.St_extractBits( 8, 9, descriptor )
    local variableIndex = C.St_extractBits( 10, 15, descriptor )
    if variableType == 0 then
        receiver[variableIndex] = stackTop()
    elseif variableType == 1 then
        homeContext[variableIndex+6] = stackTop() -- TempFrameStart
    elseif variableType == 2 then
        error( "ERROR: illegal store", cycleNr )
        -- BB: self error:
    elseif variableType == 3 then
        literal(variableIndex)[1] = stackTop() -- ValueIndex
    end
end

local function stackBytecode()
	local b = currentBytecode
	if b >= 0 and b <= 15 then
        -- pushReceiverVariableBytecode()
		-- ST_TRACE_BYTECODE("receiver:",prettyValue(receiver))
		push( receiver[ C.St_extractBits( 12, 15, currentBytecode ) ] )
	elseif b >= 16 and b <= 31 then
        -- pushTemporaryVariableBytecode()
		local var = C.St_extractBits( 12, 15, currentBytecode )
		local val = temporary( var )
		-- ST_TRACE_BYTECODE("variable:", var, "value:", prettyValue(val) )
		push( val )
	elseif b >= 32 and b <= 63 then
        -- pushLiteralConstantBytecode()
		local fieldIndex = C.St_extractBits( 11, 15, currentBytecode )
		local literalConstant = literal( fieldIndex )
		-- ST_TRACE_BYTECODE("literal:",fieldIndex,"value:",prettyValue(literalConstant),"of method:", method.oop )
		push( literalConstant )
	elseif b >= 64 and b <= 95 then
        -- pushLiteralVariableBytecode()
		local fieldIndex = C.St_extractBits( 11, 15, currentBytecode )
		local association = literal( fieldIndex )
		local value = association[1] -- ValueIndex
		-- ST_TRACE_BYTECODE("literal:", fieldIndex, "value:", prettyValue(value), "of method:", method.oop )
		push( value )
	elseif b >= 96 and b <= 103 then
        -- storeAndPopReceiverVariableBytecode()
		local variableIndex = C.St_extractBits( 13, 15, currentBytecode )
		local val = popStack()
		-- ST_TRACE_BYTECODE("var:", variableIndex, "val:", prettyValue(val) )
		receiver[variableIndex] = val
	elseif b >= 104 and b <= 111 then
        -- storeAndPopTemporaryVariableBytecode()
		local variableIndex = C.St_extractBits( 13, 15, currentBytecode )
		local val = popStack()
		-- ST_TRACE_BYTECODE("var:", variableIndex, "val:", prettyValue(val) )
		homeContext[variableIndex+6] = val -- +6 TempFrameStart
    elseif b == 112 then
        -- pushReceiverBytecode()
		-- ST_TRACE_BYTECODE("receiver:", prettyValue(receiver)) 
		push( receiver )
    elseif b >= 113 and b <= 119 then
        -- pushConstantBytecode()
        local val
		if currentBytecode == 113 then
		    val = true
		elseif currentBytecode == 114 then
		    val = false
		elseif currentBytecode == 115 then
		    val = nil
		elseif currentBytecode == 116 then
		    val = -1
		elseif currentBytecode == 117 then
		    val = 0
		elseif currentBytecode == 118 then
		    val = 1
		elseif currentBytecode == 119 then
		    val = 2
		end
		-- ST_TRACE_BYTECODE("val:", prettyValue(val) )
		push(val)
    elseif b == 128 then
        -- extendedPushBytecode()
		local descriptor = fetchByte()
		local variableType = C.St_extractBits( 8, 9, descriptor )
		local variableIndex = C.St_extractBits( 10, 15, descriptor )
		local val
		if variableType == 0 then
		    val = receiver[variableIndex]
		elseif variableType == 1 then
		    val = temporary( variableIndex )
		elseif variableType == 2 then
		    val = literal( variableIndex ) 
		elseif variableType == 3 then
		    val = literal( variableIndex )[1] -- ValueIndex
		end
		-- ST_TRACE_BYTECODE("val:", prettyValue(val) )
		push(val)
    elseif b == 129 then
	    -- ST_TRACE_BYTECODE()
        extendedStoreBytecode()
    elseif b == 130 then
        -- extendedStoreAndPopBytecode()
		-- ST_TRACE_BYTECODE()
		extendedStoreBytecode()	
		-- popStackBytecode()
		popStack()
    elseif b == 135 then
        -- popStackBytecode()
        -- ST_TRACE_BYTECODE()
        popStack()
    elseif b == 136 then
        -- duplicateTopBytecode()
        local val = stackTop()
        -- ST_TRACE_BYTECODE("val:", prettyValue(val) )
        push( val )
    elseif b == 137 then
        -- pushActiveContextBytecode()
        -- ST_TRACE_BYTECODE()
        push( activeContext )
    end
end

local function sender()
	return homeContext[0] -- SenderIndex
end

local function caller()
  return activeContext[0] -- CallerIndex  
end

local function stackValue(offset) -- called seven times
    return activeContext[stackPointer - offset]
end

local function lookupMethodInDictionary(dictionary)
	  local SelectorStart = 2
    local MethodArrayIndex = 1
    
    local length = dictionary.count -- this is a pointers array
    local mask = length - SelectorStart - 1;
    local hash = messageSelector.oop
    if not hash then
    	hash = toaddress(messageSelector)
    else
    	hash = hash / 2
    end
    local index = bitand( mask, hash ) + SelectorStart
    local wrapAround = false
    while true do
        local nextSelector = dictionary[index]
        if nextSelector == nil then
            return false
        end
        if nextSelector == messageSelector then
            local methodArray = dictionary[MethodArrayIndex]
            newMethod = methodArray[index - SelectorStart]
            -- function primitiveIndexOf used once, inlined
            local flagValue = C.St_extractBitsSi(0,2,newMethod.header) -- flagValueOf
            primitiveIndex = 0
            if flagValue == 7 then
            	primitiveIndex = C.St_extractBitsSi(7,14, newMethod[ newMethod.count - 2 ] )
           	end
            return true
        end
        index = index + 1
        if index == length then
            if wrapAround then
                return false
            end
            wrapAround = true
            index = SelectorStart
        end
    end
end

local function transfer(count, firstFrom, fromOop, firstTo, toOop)
    local fromIndex = firstFrom
    local lastFrom = firstFrom + count
    local toIndex = firstTo
    while fromIndex < lastFrom do
        local oop = fromOop[fromIndex]
        toOop[toIndex] = oop
        fromOop[fromIndex] = nil
        fromIndex = fromIndex + 1
        toIndex = toIndex + 1
    end
end

local function pop(number)
    stackPointer = stackPointer - number
end

local function createActualMessage()
    local argumentArray = { count = argumentCount }
    setmetatable( argumentArray, memory.knownObjects[0x10] ) -- classArray
    
    local message = { count = 2 } -- MessageSize 
    setmetatable( message, memory.knownObjects[0x20] ) -- classMessage
    message[0] = messageSelector -- MessageSelectorIndex
    message[1] = argumentArray -- MessageArgumentsIndex
    
    transfer( argumentCount, stackPointer - (argumentCount - 1 ), activeContext, 0, argumentArray )
    pop( argumentCount )
    push( message )
    argumentCount = 1
end

local function superclassOf(cls) -- three instances
	return cls[0] -- SuperClassIndex
end

local function lookupMethodInClass(cls) -- called four times
    local currentClass = cls
    while currentClass ~= nil do
        local dictionary = currentClass[1] -- MessageDictionaryIndex
        if lookupMethodInDictionary( dictionary ) then
            return true
        end
        currentClass = superclassOf(currentClass)
    end
    
    if messageSelector == memory.knownObjects[0x2a] then -- symbolDoesNotUnderstand
        print( "ERROR: Recursive not understood error encountered", cycleNr )
        -- BB self error:
        return false
    end
    createActualMessage() 
    messageSelector = memory.knownObjects[0x2a] -- symbolDoesNotUnderstand
    return lookupMethodInClass(cls)
end

local function primitiveResponse()
    if primitiveIndex == 0 then
        local flagValue = C.St_extractBitsSi(0,2,newMethod.header) -- flagValueOf
        if flagValue == 5 then
            -- NOP quickReturnSelf();
            return true
        elseif flagValue == 6 then
            -- quickInstanceLoad() called once inlined
			local thisReceiver = popStack()
			local fieldIndex = C.St_extractBitsSi(3,7,newMethod.header) -- fieldIndexOf
			local val = thisReceiver[fieldIndex]
			push( val )
           	return true
        end
    else
        success = true -- initPrimitive()
        local currentPrimitive = primitive[primitiveIndex] -- dispatchPrimitives() 
        -- ST_TRACE_PRIMITIVE()
        if currentPrimitive then
        	currentPrimitive()
       	else
       		success = false -- primitiveFail()
        end
        return success
    end
    return false
end

local function storeInstructionPointerValueInContext(value,contextPointer) -- called twice
	contextPointer[1] = value -- InstructionPointerIndex
end

local function storeStackPointerValueInContext(value,contextPointer) -- called five times
	contextPointer[2] = value -- StackPointerIndex
end

local function storeContextRegisters()
    if activeContext then -- deviation from BB since activeContext is null on first call
        storeInstructionPointerValueInContext( instructionPointer + 1, activeContext )
        storeStackPointerValueInContext( stackPointer - 6 + 1, activeContext ) -- TempFrameStart
    end
end

local function isBlockContext(contextPointer) -- called twice
    local methodOrArguments = contextPointer[3] -- MethodIndex
    return fetchClassOf(methodOrArguments) == classSmallInteger
end

local function fetchContextRegisters()
    if isBlockContext(activeContext) then
        homeContext = activeContext[5] -- HomeIndex
    else
        homeContext = activeContext
    end
    receiver = homeContext[5] -- ReceiverIndex
    method = homeContext[3] -- MethodIndex
    methodBytecode = method.bytecode
    assert(methodBytecode)
    instructionPointer = activeContext[1] - 1 -- InstructionPointerIndex instructionPointerOfContext(activeContext)
    stackPointer = activeContext[2] + 6 - 1 -- StackPointerIndex stackPointerOfContext(activeContext) TempFrameStart
end

local function newActiveContext(aContext)
	storeContextRegisters()
	activeContext = aContext
    fetchContextRegisters()
end

local function executeNewMethod()
    -- ST_TRACE_METHOD_CALL()
    if not primitiveResponse() then
        -- function activateNewMethod() -- used once inlined
		local contextSize = 6 -- TempFrameStart;
		if C.St_extractBitsSi(8,8, newMethod.header ) == 1 then -- largeContextFlagOf( newMethod )
		    contextSize = contextSize + 32
		else
		    contextSize = contextSize + 12;
		end
		local newContext = { count  = contextSize }
		setmetatable( newContext, memory.knownObjects[0x16] ) -- classMethodContext
		newContext[0] = activeContext -- SenderIndex
		-- initialInstructionPointerOfMethod( newMethod ) inlined
		local iip = ( newMethod.count + 1 ) * 2 + 1
		storeInstructionPointerValueInContext( iip, newContext ) 
		local temporaryCount = C.St_extractBitsSi(3,7,newMethod.header) -- temporaryCountOf( newMethod )
		storeStackPointerValueInContext( temporaryCount, newContext )
		newContext[3] = newMethod -- MethodIndex
		transfer( argumentCount + 1, stackPointer - argumentCount, activeContext, 5, newContext ) -- ReceiverIndex
		pop( argumentCount + 1 )
		newActiveContext(newContext)
		--end
        
    end
end

local function sendSelectorToClass(classPointer) -- called three times
    -- deviation from BB, we currently don't have a methodCache, original: findNewMethodInClass
    lookupMethodInClass(classPointer)
    executeNewMethod()
end

local function sendSelector(selector,count) -- called seven times
    messageSelector = selector
    argumentCount = count
    local newReceiver = stackValue(argumentCount) -- newReceiver might legally be nil!
    sendSelectorToClass( fetchClassOf(newReceiver) ) -- fetchClassOf
end

local function returnValue(resultPointer, contextPointer)
	-- ST_TRACE_BYTECODE("result:", prettyValue(resultPointer), "context:", prettyValue(contextPointer))

    if contextPointer == nil then
        push( activeContext )
        push( resultPointer )
        sendSelector( memory.knownObjects[0x2c], 1 ) -- symbolCannotReturn
    end
    
    local sendersIP = contextPointer[1] -- InstructionPointerIndex
    if sendersIP == nil then
        push( activeContext )
        push( resultPointer )
        sendSelector( memory.knownObjects[0x2c], 1 ) -- symbolCannotReturn
    end

    -- returnToActiveContext(contextPointer)
    local aContext = contextPointer
    -- function returnToActiveContext(aContext) -- called once, inlined
    local tmp = aContext -- increaseReferencesTo: aContext
    -- nilContextFields() -- called once inlined
    activeContext[0] = nil -- SenderIndex
    activeContext[1] = nil -- InstructionPointerIndex
    activeContext = aContext
    fetchContextRegisters()
	-- end function
    
    push( resultPointer )
end

local function returnBytecode()
    if currentBytecode == 120 then
        returnValue( receiver, sender() )
    elseif currentBytecode == 121 then
        returnValue( true, sender() )
    elseif currentBytecode == 122 then
        returnValue( false, sender() )
    elseif currentBytecode == 123 then
        returnValue( nil, sender() )
    elseif currentBytecode == 124 then
        returnValue( popStack(), sender() )
    elseif currentBytecode == 125 then
        returnValue( popStack(), caller() )
    else
 		print( "WARNING: executing unused bytecode", currentBytecode )
    end
end

local function methodClassOf(methodPointer)
    local literalCount = methodPointer.count -- literalCountOf(methodPointer);
    local association = methodPointer[literalCount-1]
    return association[1] -- ValueIndex
end

local function fetchIntegerOfObject(fieldIndex, objectPointer)
    local integerPointer = objectPointer[fieldIndex]
    if fetchClassOf(integerPointer) == classSmallInteger then
        return integerPointer
    end
    success = false -- primitiveFail
    return 0
end

local function specialSelectorPrimitiveResponse()
    success = true -- initPrimitive()
	if currentBytecode >= 176 and currentBytecode <= 191 then
	    -- arithmeticSelectorPrimitive() called once, inlined 
	    success = success and fetchClassOf( stackValue(1) ) == classSmallInteger
	    if not success then
	    	return false
	    end
	    if currentBytecode == 176 then
	    	primitive[1]() -- primitiveAdd()
		elseif currentBytecode == 177 then
			primitive[2]() -- primitiveSubtract()
		elseif currentBytecode == 178 then
			primitive[3]() -- primitiveLessThan();
		elseif currentBytecode == 179 then
			primitive[4]() -- primitiveGreaterThan();
		elseif currentBytecode == 180 then
			primitive[5]() -- primitiveLessOrEqual();
		elseif currentBytecode == 181 then
			primitive[6]() -- primitiveGreaterOrEqual();
		elseif currentBytecode == 182 then
			primitive[7]() -- primitiveEqual();
		elseif currentBytecode == 183 then
			primitive[8]() -- primitiveNotEqual();
		elseif currentBytecode == 184 then
			primitive[9]() -- primitiveMultiply();
		elseif currentBytecode == 185 then
			primitive[10]() -- primitiveDivide();
		elseif currentBytecode == 186 then
			primitive[11]() -- primitiveMod();
		elseif currentBytecode == 187 then
			primitive[18]() -- primitiveMakePoint();
		elseif currentBytecode == 188 then
			primitive[17]() -- primitiveBitShift();
		elseif currentBytecode == 189 then
			primitive[12]() -- primitiveDiv();
		elseif currentBytecode == 190 then
			primitive[14]() -- primitiveBitAnd();
		elseif currentBytecode == 191 then
			primitive[15]() -- primitiveBitOr();
	    end
	elseif currentBytecode >= 192 and currentBytecode <= 207 then
	    -- commonSelectorPrimitive() called once, inlined
	    local specialSelectors = memory.knownObjects[0x30]
	    argumentCount = fetchIntegerOfObject( (currentBytecode - 176) * 2 + 1, specialSelectors ) 
		local receiverClass = fetchClassOf( stackValue( argumentCount ) ) -- fetchClassOf
		if currentBytecode == 198 then
		    primitive[110]() -- primitiveEquivalent();
		elseif currentBytecode == 199 then
		    primitive[111]() -- primitiveClass();
		elseif currentBytecode == 200 then
		    success = success and ( receiverClass == memory.knownObjects[0x16] or -- classMethodContext
		                   receiverClass == memory.knownObjects[0x18] ) -- classBlockContext
		    if success then
		        primitive[80]() -- primitiveBlockCopy();
		    end
		elseif currentBytecode == 201 or currentBytecode == 202 then
		    success = success and receiverClass == memory.knownObjects[0x18] -- classBlockContext
		    if success then
		        primitive[81]() -- primitiveValue();
		    end
		else
			success = false
		end
	end
	return success
end

local function sendBytecode()
	if currentBytecode == 131 then
        -- singleExtendedSendBytecode()
		local descriptor = fetchByte()
		local selectorIndex = C.St_extractBits( 11, 15, descriptor )
		local _argumentCount = C.St_extractBits( 8, 10, descriptor )
		local selector = literal(selectorIndex)
		-- ST_TRACE_BYTECODE("selector:", prettyValue(selector), "count:", _argumentCount )
		sendSelector( selector, _argumentCount )
	elseif currentBytecode == 132 then
        -- doubleExtendedSendBytecode()
		local count = fetchByte()
		local selector = literal( fetchByte() )
		-- ST_TRACE_BYTECODE("selector:", prettyValue(selector), "count:", count )
		sendSelector( selector, count )
	elseif currentBytecode == 133 then
        -- singleExtendedSuperBytecode()
		local descriptor = fetchByte()
		argumentCount = C.St_extractBits( 8, 10, descriptor )
		local selectorIndex = C.St_extractBits( 11, 15, descriptor )
		messageSelector = literal( selectorIndex )
		local methodClass = methodClassOf( method )
		local super = superclassOf(methodClass)
		-- ST_TRACE_BYTECODE("selector:", prettyValue(messageSelector), "super:", prettyValue(super) )
		sendSelectorToClass( super )
	elseif currentBytecode == 134 then
        -- doubleExtendedSuperBytecode()
		argumentCount = fetchByte()
		messageSelector = literal( fetchByte() )
		local methodClass = methodClassOf( method )
		local super = superclassOf(methodClass)
		-- ST_TRACE_BYTECODE("selector:", prettyValue(messageSelector), "super:", prettyValue(super) )
		sendSelectorToClass( super )
	elseif currentBytecode >= 176 and currentBytecode <= 207 then
    	-- sendSpecialSelectorBytecode()
		if not specialSelectorPrimitiveResponse()  then
			local selectorIndex = ( currentBytecode - 176 ) * 2
			local specialSelectors = memory.knownObjects[0x30]
			local selector = specialSelectors[selectorIndex] -- specialSelectors
			local count = fetchIntegerOfObject(selectorIndex + 1, specialSelectors )
			-- ST_TRACE_BYTECODE("selector:", prettyValue(selector), "count:", count )
			sendSelector( selector, count )
		else
			-- ST_TRACE_BYTECODE("primitive")
		end
	elseif currentBytecode >= 208 and currentBytecode <= 255 then
        -- sendLiteralSelectorBytecode()
		local litNr = C.St_extractBits( 12, 15, currentBytecode )
		local selector = literal( litNr )
		local argumentCount = C.St_extractBits( 10, 11, currentBytecode ) - 1
		-- ST_TRACE_BYTECODE("selector:", prettyValue(selector), "count:", argumentCount )
		sendSelector( selector, argumentCount )
	end
end

local function jump(offset)
	instructionPointer = instructionPointer + offset
end

local function unPop(number)
	stackPointer = stackPointer + number
end

local function sendMustBeBoolean()
    sendSelector( memory.knownObjects[0x34], 0 ) -- symbolMustBeBoolean
end

local function jumpif(condition, offset)
    local boolean = popStack()
    if boolean == condition then
        jump(offset)
    elseif not ( boolean == true or boolean == false ) then
        unPop(1)
        sendMustBeBoolean()
    end
end

local function jumpBytecode()
    local b = currentBytecode
    if b >= 144 and b <= 151 then
        -- shortUnconditionalJump()
		local offset = C.St_extractBits( 13, 15, currentBytecode )
		-- ST_TRACE_BYTECODE("offset:", offset + 1 )
		jump( offset + 1 )
	elseif b >= 152 and b <= 159 then
        -- shortContidionalJump()
		local offset = C.St_extractBits( 13, 15, currentBytecode )
		-- ST_TRACE_BYTECODE("offset:", offset + 1 )
		jumpif( false, offset + 1 )
	elseif b >= 160 and b <= 167 then
        -- longUnconditionalJump()
		local offset = C.St_extractBits( 13, 15, currentBytecode )
		offset = ( offset - 4 ) * 256 + fetchByte()
		-- ST_TRACE_BYTECODE("offset:", offset )
		jump( offset )
    elseif b >= 168 and b <= 175 then
        -- longConditionalJump()
		local offset = C.St_extractBits( 14, 15, currentBytecode )
		offset = offset * 256 + fetchByte()
		-- ST_TRACE_BYTECODE("offset:", offset )
		if currentBytecode >= 168 and currentBytecode <= 171 then
			jumpif( true, offset )
		elseif currentBytecode >= 172 and currentBytecode <= 175 then
			jumpif( false, offset )
		end
	end
end

local function dispatchOnThisBytecode()
	local b = currentBytecode
    if ( b >= 0 and b <= 119 ) or ( b >= 128 and b <= 130 ) or ( b >= 135 and b <= 137 ) then
        stackBytecode() 
    elseif b >= 120 and b <= 127 then
        returnBytecode()
    elseif ( b >= 131 and b <= 134 ) or ( b >= 176 and b <= 255 ) then
        sendBytecode()
    elseif b >= 144 and b <= 175 then
        jumpBytecode()
    elseif b >= 138 and b <= 143 then
        print( "WARNING: running unused bytecode", b )
    end
end

local function isEmptyList(aLinkedList) -- called three times
    if aLinkedList == nil then
        return true
    end
    return aLinkedList[0] == nil -- FirstLinkIndex
end

local function removeFirstLinkOfList(aLinkedList) -- called twice
    local firstLink = aLinkedList[0] -- FirstLinkIndex
    local lastLink = aLinkedList[1] -- LastLinkIndex
    if firstLink == lastLink then
        aLinkedList[0] = nil -- FirstLinkIndex
        aLinkedList[1] = nil -- LastLinkIndex
    else
        local nextLink = firstLink[0] -- NextLinkIndex
        aLinkedList[0] = nextLink -- FirstLinkIndex
    end
    firstLink[0] = nil -- NextLinkIndex
    return firstLink
end

local function addLastLinkToList(aLink, aLinkedList) -- called twice
    if isEmptyList( aLinkedList ) then
        aLinkedList[0] = aLink -- FirstLinkIndex
    else
        local lastLink = aLinkedList[1] -- LastLinkIndex
        lastLink[0] = aLink -- NextLinkIndex
    end
    aLinkedList[1] = aLink -- LastLinkIndex
    aLink[3] = aLinkedList -- MyListIndex
end

local function schedulerPointer()
	return memory.knownObjects[0x08][1] -- ObjectMemory2::processor, ValueIndex
end

local function sleep(aProcess) -- called twice
    local priority = aProcess[2] -- PriorityIndex
    local processLists = schedulerPointer()[0] -- ProcessListIndex
    local processList = processLists[priority - 1]
    addLastLinkToList( aProcess, processList )
end

local function transferTo(aProcess) -- called twice
    newProcessWaiting = true
    newProcess = aProcess
end

local function activeProcess()
	if newProcessWaiting then
        return newProcess
    else
        return schedulerPointer()[1] -- ActiveProcessIndex
    end
end

local function resume(aProcess) -- called twice
    local activeProcess_ = activeProcess()
    local activePriority = activeProcess_[2] -- PriorityIndex
    local newPriority = aProcess[2] -- PriorityIndex
    if newPriority > activePriority then
        sleep( activeProcess_ )
        transferTo( aProcess )
    else
        sleep( aProcess )
    end
end

local function synchronousSignal(aSemaphore) -- called twice
    if isEmptyList(aSemaphore) then
        local excessSignals = aSemaphore[2] -- ExcessSignalIndex
        aSemaphore[2] = excessSignals + 1 -- ExcessSignalIndex
    else
        resume( removeFirstLinkOfList(aSemaphore) ) 
    end
end

local function checkProcessSwitch()
    while semaphoreIndex > 0 do
        synchronousSignal( semaphoreList[semaphoreIndex] ) 
        semaphoreIndex = semaphoreIndex - 1
    end
    
    if newProcessWaiting then
        newProcessWaiting = false
        local activeProcess_ = activeProcess()
        if activeProcess_ then
            activeProcess_[1] = activeContext -- SuspendedContextIndex
        end
        local scheduler = schedulerPointer()
        scheduler[1] = newProcess -- ActiveProcessIndex
        newActiveContext( newProcess[1] ) -- SuspendedContextIndex
        newProcess = nil
    end
end

local function cycle()
	local pending = C.St_pendingEvents()
	if pending > 0 then
		for i=1,pending do
			-- asynchronousSignal(inputSemaphore) inlined
			if inputSemaphore then
				semaphoreIndex = semaphoreIndex + 1
				semaphoreList[semaphoreIndex] = inputSemaphore
			end
		end
	elseif pending == -1 then
		-- asynchronousSignal(toSignal) inlined
		semaphoreIndex = semaphoreIndex + 1
		semaphoreList[semaphoreIndex] = toSignal
	elseif pending == -2 then
		local str = memory.knownObjects.CurrentSelection[1][0]
		C.St_copyToClipboard(str.data)
	end
    checkProcessSwitch() 
	currentBytecode = fetchByte()
	cycleNr = cycleNr + 1
	dispatchOnThisBytecode()
end

function module.interpret()
	C.St_start()
	cycleNr = 0
	newProcessWaiting = false;
    local firstContext = activeProcess()[1] -- SuspendedContextIndex
	newActiveContext( firstContext )
	print "start main loop"
	while C.St_isRunning() ~= 0 do -- and cycleNr < 121000 do 
		cycle()
		C.St_processEvents()
	end
	C.St_stop()
	print "quit main loop"
end

---------------------- Primitives implementation -----------------------------------

local function popInteger()
    local integerPointer = popStack()
    success = success and fetchClassOf(integerPointer) == classSmallInteger
    return integerPointer
end

local function isIntegerValue(value)
	local isInt = C.St_isIntegerValue(value) ~= 0
	return isInt
end

function primitive.Add() -- primitiveAdd
	-- ST_TRACE_PRIMITIVE("primitiveAdd")
	local integerArgument = popInteger()
    local integerReceiver = popInteger()
    local integerResult
    if success then
         integerResult = integerReceiver + integerArgument
         success = success and isIntegerValue(integerResult)
    end
    if success then
        push( integerResult )
    else
        unPop(2)
    end
end
primitive[1] = primitive.Add

function primitive.Subtract() -- primitiveSubtract
	--ST_TRACE_PRIMITIVE("primitiveSubtract")
	local integerArgument = popInteger()
    local integerReceiver = popInteger()
    local integerResult
    if success then
         integerResult = integerReceiver - integerArgument
         success = success and isIntegerValue(integerResult)
    end
    if success then
        push( integerResult )
    else
        unPop(2)
    end
end
primitive[2] = primitive.Subtract

function primitive.LessThan() -- primitiveLessThan
	--ST_TRACE_PRIMITIVE("primitiveLessThan")
	local integerArgument = popInteger()
	local integerReceiver = popInteger()
	if success then
		push( integerReceiver < integerArgument )
	else
		unPop(2)
	end
end
primitive[3] = primitive.LessThan

function primitive.GreaterThan() -- primitiveGreaterThan
	-- ST_TRACE_PRIMITIVE("primitiveGreaterThan")
	local integerArgument = popInteger()
    local integerReceiver = popInteger()
    if success then
    	push( integerReceiver > integerArgument )
    else
    	unPop(2)
    end
end
primitive[4] = primitive.GreaterThan

function primitive.LessOrEqual() -- primitiveLessOrEqual
	-- ST_TRACE_PRIMITIVE("");
	local integerArgument = popInteger()
    local integerReceiver = popInteger()
    if success then
    	push( integerReceiver <= integerArgument )
    else
    	unPop(2)
    end
end
primitive[5] = primitive.LessOrEqual

function primitive.GreaterOrEqual() -- primitiveGreaterOrEqual
	-- ST_TRACE_PRIMITIVE("");
	local integerArgument = popInteger()
    local integerReceiver = popInteger()
    if success then
    	push( integerReceiver >= integerArgument )
    else
    	unPop(2)
    end
end
primitive[6] = primitive.GreaterOrEqual

function primitive.Equal() -- primitiveEqual
	-- ST_TRACE_PRIMITIVE("");
	local integerArgument = popInteger()
    local integerReceiver = popInteger()
    if success then
    	push( integerReceiver == integerArgument )
    else
    	unPop(2)
    end
end
primitive[7] = primitive.Equal

function primitive.NotEqual() -- primitiveNotEqual
	local integerArgument = popInteger()
    local integerReceiver = popInteger()
    if success then
    	push( integerReceiver ~= integerArgument )
    else
    	unPop(2)
    end
end
primitive[8] = primitive.NotEqual

function primitive.Multiply() -- primitiveMultiply
	-- ST_TRACE_PRIMITIVE("");
	local integerArgument = popInteger()
    local integerReceiver = popInteger()
    local integerResult
    if success then
         integerResult = integerReceiver * integerArgument
         success = success and isIntegerValue(integerResult)
    end
    if success then
        push( integerResult )
    else
        unPop(2)
    end
end
primitive[9] = primitive.Multiply

function primitive.Divide() -- primitiveDivide
	-- ST_TRACE_PRIMITIVE("");
	local integerArgument = popInteger()
    local integerReceiver = popInteger()
    success = success and ( integerArgument ~= 0 and integerReceiver % integerArgument == 0 )
    local integerResult
    if success then
         integerResult = integerReceiver / integerArgument
         success = success and isIntegerValue(integerResult)
    end
    if success then
        push( integerResult )
    else
        unPop(2)
    end
end
primitive[10] = primitive.Divide

function primitive.Mod() -- primitiveMod
	-- ST_TRACE_PRIMITIVE("");
	local integerArgument = popInteger()
    local integerReceiver = popInteger()
    local integerResult
    if success then
         integerResult = C.St_MOD( integerReceiver, integerArgument )
         success = success and isIntegerValue(integerResult)
    end
    if success then
        push( integerResult )
    else
        unPop(2)
    end
end
primitive[11] = primitive.Mod

function primitive.Div() -- primitiveDiv
	-- ST_TRACE_PRIMITIVE("");
		local integerArgument = popInteger()
		local integerReceiver = popInteger()
		success = success and integerArgument ~= 0
		local integerResult
		if success then
			integerResult = C.St_DIV( integerReceiver, integerArgument )
			success = success and isIntegerValue(integerResult)
		end
		if success then
			push( integerResult )
		else
			unPop(2)
	end
end
primitive[12] = primitive.Div

function primitive.Quo() -- primitiveQuo
	-- ST_TRACE_PRIMITIVE("");
	local integerArgument = popInteger()
    local integerReceiver = popInteger()
    success = success and integerArgument ~= 0
    local integerResult
    if success then
         integerResult = C.St_round( integerReceiver / integerArgument )
         success = success and isIntegerValue(integerResult)
    end
    if success then
        push( integerResult )
    else
        unPop(2)
    end
end
primitive[13] = primitive.Quo

function primitive.BitAnd() -- primitiveBitAnd
	-- ST_TRACE_PRIMITIVE("");
	local integerArgument = popInteger()
    local integerReceiver = popInteger()
    local integerResult
    if success then
         integerResult = bitand( integerReceiver, integerArgument )
    end
    if success then
        push( integerResult )
    else
        unPop(2)
    end
end
primitive[14] = primitive.BitAnd

function primitive.BitOr() -- primitiveBitOr
	-- ST_TRACE_PRIMITIVE("");
	local integerArgument = popInteger()
    local integerReceiver = popInteger()
    local integerResult
    if success then
         integerResult = bit.bor( integerReceiver, integerArgument )
    end
    if success then
        push( integerResult )
    else
        unPop(2)
    end
end
primitive[15] = primitive.BitOr

function primitive.BitXor() -- primitiveBitXor
	-- ST_TRACE_PRIMITIVE("");
	local integerArgument = popInteger()
    local integerReceiver = popInteger()
    local integerResult
    if success then
        integerResult = bit.xor( integerReceiver, integerArgument )
    end
    if success then
        push( integerResult )
    else
        unPop(2)
    end
end
primitive[16] = primitive.BitXor

function primitive.BitShift() -- primitiveBitShift
	-- ST_TRACE_PRIMITIVE("");
	local integerArgument = popInteger()
    local integerReceiver = popInteger()
    local integerResult
    if success then
    	if integerArgument >= 0 then
    		integerResult = bit.lshift( integerReceiver, integerArgument )
        else
    		integerResult = bit.arshift( integerReceiver, -integerArgument )
        end
        success = success and isIntegerValue(integerResult)
    end
    if success then
        push( integerResult )
    else
        unPop(2)
    end
end
primitive[17] = primitive.BitShift

function primitive.MakePoint() -- primitiveMakePoint
	-- ST_TRACE_PRIMITIVE("");
	local integerArgument = popInteger()
    local integerReceiver = popInteger()
    if success then
    	local pointResult = { count = 2 } -- ClassPointSize
    	setmetatable(pointResult, memory.knownObjects[0x1a]) -- classPoint
    	pointResult[0] = integerReceiver -- XIndex
    	pointResult[1] = integerArgument -- YIndex
        push( pointResult )
    else
        unPop(2)
    end
end
primitive[18] = primitive.MakePoint

-- 19 to 39 dispatchLargeIntegerPrimitives optional

local function popFloat()
    local floatPointer = popStack()
    local cls = fetchClassOf(floatPointer)
    success = success and cls == classFloat
    if success then
	    return floatPointer[0]
	else
		return 0
	end
end

local function pushFloat( value )
	local floatObject = { [0] = value, count = 1 }
	setmetatable( floatObject, classFloat ) 
	push( floatObject )
end

function primitive.AsFloat() -- primitiveAsFloat
    -- ST_TRACE_PRIMITIVE("");
    local integerReceiver = popInteger()
    if success then
        pushFloat(integerReceiver)
    else
        unPop(1)
    end
end
primitive[40] = primitive.AsFloat

function primitive.FloatAdd() -- primitiveFloatAdd
	-- ST_TRACE_PRIMITIVE("");
	local floatArgument = popFloat()
    local floatReceiver = popFloat()
    if success then
        local floatResult = floatReceiver + floatArgument
        pushFloat( floatResult )
    else
        unPop(2)
    end
end
primitive[41] = primitive.FloatAdd

function primitive.FloatSubtract() -- primitiveFloatSubtract
	-- ST_TRACE_PRIMITIVE("");
	local floatArgument = popFloat()
    local floatReceiver = popFloat()
    if success then
        local floatResult = floatReceiver - floatArgument
        pushFloat( floatResult )
    else
        unPop(2)
    end
end
primitive[42] = primitive.FloatSubtract

function primitive.FloatLessThan() -- primitiveFloatLessThan
	-- ST_TRACE_PRIMITIVE("");
	local floatArgument = popFloat()
    local floatReceiver = popFloat()
    if success then
        local result = floatReceiver < floatArgument
        push( result )
    else
        unPop(2)
    end
end
primitive[43] = primitive.FloatLessThan

function primitive.FloatGreaterThan() -- primitiveFloatGreaterThan
	-- ST_TRACE_PRIMITIVE("");
	local floatArgument = popFloat()
    local floatReceiver = popFloat()
    if success then
        local result = floatReceiver > floatArgument
        push( result )
    else
        unPop(2)
    end
end
primitive[44] = primitive.FloatGreaterThan

function primitive.FloatLessOrEqual() -- primitiveFloatLessOrEqual
	-- ST_TRACE_PRIMITIVE("");
	local floatArgument = popFloat()
    local floatReceiver = popFloat()
    if success then
        local result = floatReceiver <= floatArgument
        push( result )
    else
        unPop(2)
    end
end
primitive[45] = primitive.FloatLessOrEqual

function primitive.FloatGreaterOrEqual() -- 
	-- ST_TRACE_PRIMITIVE("");
	local floatArgument = popFloat()
    local floatReceiver = popFloat()
    if success then
        local result = floatReceiver >= floatArgument
        push( result )
    else
        unPop(2)
    end
end
primitive[46] = primitive.FloatGreaterOrEqual

function primitive.FloatEqual() -- primitiveFloatEqual
	-- ST_TRACE_PRIMITIVE("");
	local floatArgument = popFloat()
    local floatReceiver = popFloat()
    if success then
        local result = floatReceiver == floatArgument
        push( result )
    else
        unPop(2)
    end
end
primitive[47] = primitive.FloatEqual

function primitive.FloatNotEqual() -- primitiveFloatNotEqual
	-- ST_TRACE_PRIMITIVE("");
	local floatArgument = popFloat()
    local floatReceiver = popFloat()
    if success then
        local result = floatReceiver ~= floatArgument
        push( result )
    else
        unPop(2)
    end
end
primitive[48] = primitive.FloatNotEqual

function primitive.FloatMultiply() -- primitiveFloatMultiply
	-- ST_TRACE_PRIMITIVE("");
	local floatArgument = popFloat()
    local floatReceiver = popFloat()
    if success then
        local floatResult = floatReceiver * floatArgument
        pushFloat( floatResult )
    else
        unPop(2)
    end
end
primitive[49] = primitive.FloatMultiply

function primitive.FloatDivide() -- primitiveFloatDivide
	-- ST_TRACE_PRIMITIVE("");
	local floatArgument = popFloat()
    local floatReceiver = popFloat()
    success = success and floatArgument ~= 0.0
    if success then
        local floatResult = floatReceiver / floatArgument
        pushFloat( floatResult )
    else
        unPop(2)
    end
end
primitive[50] = primitive.FloatDivide

function primitive.Truncated() -- primitiveTruncated
    -- ST_TRACE_PRIMITIVE("");
    local floatReceiver = popFloat()
    local res = mathfloor(floatReceiver)
    success = success and isIntegerValue(res)
    if success then
        push(res)
    else
        unPop(1)
    end
end
primitive[51] = primitive.Truncated

function primitive.FractionalPart() -- primitiveFractionalPart
    -- ST_TRACE_PRIMITIVE("");
    local floatReceiver = popFloat()
    if success then
        pushFloat( floatReceiver - mathfloor(floatReceiver) )
    else
        unPop(1)
    end
end
primitive[52] = primitive.FractionalPart

function primitive.Exponent() -- primitiveExponent
	success = false -- optional
end
primitive[53] = primitive.Exponent

function primitive.TimesTwoPower() -- primitiveTimesTwoPower
	success = false -- optional
end
primitive[54] = primitive.TimesTwoPower

local function positive16BitValueOf(integerPointer)
	local meta = fetchClassOf(integerPointer)
    if meta == classSmallInteger then
        return integerPointer -- this is already a Lua number
    end

    if meta ~= classLargePositiveInteger or integerPointer.data.count > 4 then
    	success = false
        return 0
    end

    return C.St_toUInt(integerPointer.data);
end

local function lengthOf(array)
	if array == nil then
		return 0
	elseif array.bytecode then
		return (array.count+1)*2 + array.bytecode.count
	else
		return array.count or array.data.count
	end
end

local function subscriptWith(array, index)
	-- inlined positive16BitIntegerFor
	if array.data then
		return array.data.data[index-1]
	else
		return array[index-1]
    end
end

local function subscriptWithStoring(array, index, value)
	-- inlined positive16BitValueOf
	if array.data then
		array.data.data[index-1] = value
	else
		array[index-1] = value
	end
end

function primitive.At() -- primitiveAt
    -- ST_TRACE_PRIMITIVE("");
    local tmp = popStack()
    local index = positive16BitValueOf( tmp )
    local array = popStack()
    local arrayClass = fetchClassOf(array)
    -- checkIndexableBoundsOf inlined
    local instSpec = arrayClass[2] -- InstanceSpecIndex
    local fixed = C.St_extractBitsSi(4,14,instSpec) -- fixedFieldsOf inlined
    success = success and index >= 1 and ( index + fixed ) <= lengthOf(array)
    local result
    if success then
        index  = index + fixed
        if arrayClass == classCompiledMethod then
	        local initPc = (array.count+1)*2
        	result = array.bytecode.data[index-1-initPc] -- index - initialPc
        else
        	result = subscriptWith(array,index)
        end
    end
    if success then
        push(result)
    else
        unPop(2)
    end
end
primitive[60] = primitive.At

function primitive.AtPut() -- primitiveAtPut
    -- ST_TRACE_PRIMITIVE("");
    local value = popStack()
    local index = positive16BitValueOf( popStack() )
    local array = popStack()
    local arrayClass = fetchClassOf(array)
    -- checkIndexableBoundsOf inlined
    local instSpec = arrayClass[2] -- InstanceSpecIndex
    local fixed = C.St_extractBitsSi(4,14,instSpec) -- fixedFieldsOf inlined
    success = success and index >= 1 and ( index + fixed ) <= lengthOf(array)
    if success then
        index  = index + fixed
        if arrayClass == classCompiledMethod then
	        local initPc = (array.count+1)*2
        	array.bytecode.data[index-1-initPc] = value -- index 
        else
        	subscriptWithStoring(array,index,value)
        end
    end
    if success then
        push(value)
    else
        unPop(3)
    end
end
primitive[61] = primitive.AtPut

function primitive.Size() -- primitiveSize
    local array = popStack()
    local arrayClass = fetchClassOf(array)
    local instSpec = arrayClass[2] -- InstanceSpecIndex
    local fixed = C.St_extractBitsSi(4,14,instSpec) -- fixedFieldsOf inlined
    local length = lengthOf(array) - fixed -- positive16BitIntegerFor not required
    -- ST_TRACE_PRIMITIVE("size" << memory->integerValueOf(length));
    if success then
        push(length)
    else
        unPop(1)
    end
end
primitive[62] = primitive.Size

function primitive.StringAt() -- primitiveStringAt
    -- ST_TRACE_PRIMITIVE("");
    local index = positive16BitValueOf( popStack() )
    local array = popStack()
    local arrayClass = fetchClassOf(array)
    -- checkIndexableBoundsOf inlined
    success = success and index >= 1 and index <= lengthOf(array)
    local character
    if success then
        local ascii = subscriptWith(array,index)
        character = memory.knownObjects[0x32][ascii] -- ObjectMemory2::characterTable
    end
    if success then
        push(character)
    else
        unPop(2)
    end
end
primitive[63] = primitive.StringAt

function primitive.StringAtPut() -- primitiveStringAtPut
    -- ST_TRACE_PRIMITIVE("");
    local character = popStack()
    local index = positive16BitValueOf( popStack() )
    local array = popStack()
    local arrayClass = fetchClassOf(array)
    -- checkIndexableBoundsOf inlined
    success = success and index >= 1 and index <= lengthOf(array)
    	and fetchClassOf(character) == classCharacter
    if success then
        local ascii = character[0] -- character is a pointer array
        subscriptWithStoring(array,index,ascii)
    end
    if success then
        push(character)
    else
        unPop(3) 
    end
end
primitive[64] = primitive.StringAtPut

function primitive.Next() -- primitiveNext
    -- ST_TRACE_PRIMITIVE("");
    local stream = popStack()
    local array = stream[0] -- StreamArrayIndex
    local arrayClass = fetchClassOf(array)
    local index = fetchIntegerOfObject(1,stream) -- StreamIndexIndex
    local limit = fetchIntegerOfObject(2,stream) -- StreamReadLimitIndex
    success = success and index < limit
    local classArray = memory.knownObjects[0x10] -- classArray
    success = success and ( arrayClass == classArray or 
	    arrayClass == memory.knownObjects[0x0e] ) -- classString
	-- NOTE: this primitive only supports classArray and classString objects
    index = index + 1
    -- checkIndexableBoundsOf inlined
    local instSpec = arrayClass[2] -- InstanceSpecIndex
    local fixed = C.St_extractBitsSi(4,14,instSpec) -- fixedFieldsOf inlined
    success = success and index >= 1 and ( index + fixed ) <= lengthOf(array)
    local result = 0;
    if success then
        result = subscriptWith(array,index)
    end
    if success then
        stream[1] = index -- StreamIndexIndex
    end
    if success then
        if arrayClass == classArray then 
            push(result)
        else
            local ascii = result
            push( memory.knownObjects[0x32][ascii] ) -- ObjectMemory2::characterTable )
        end
    else
        unPop(1)
    end
end
primitive[65] = primitive.Next

function primitive.NextPut() -- primitiveNextPut
    -- ST_TRACE_PRIMITIVE("");
    local value = popStack()
    local stream = popStack()
    local array = stream[0] -- StreamArrayIndex
    local arrayClass = fetchClassOf(array)
    local index = fetchIntegerOfObject(1,stream) -- StreamIndexIndex
    local limit = fetchIntegerOfObject(2,stream) -- StreamReadLimitIndex
    success = success and index < limit
    local classArray = memory.knownObjects[0x10] -- classArray
    success = success and ( arrayClass == classArray or 
	    arrayClass == memory.knownObjects[0x0e] ) -- classString
	-- NOTE: this primitive only supports classArray and classString objects
    index = index + 1
    -- checkIndexableBoundsOf inlined
    local instSpec = arrayClass[2] -- InstanceSpecIndex
    local fixed = C.St_extractBitsSi(4,14,instSpec) -- fixedFieldsOf inlined
    success = success and index >= 1 and ( index + fixed ) <= lengthOf(array)
    if success then
        if arrayClass == classArray then
            subscriptWithStoring(array,index,value)
        else
            local ascii = value[0] -- character value index 0
            subscriptWithStoring(array,index,ascii)
        end
    end
    if success then
        stream[1] = index -- storeIntegerOfObjectWithValue StreamIndexIndex
    end
    if success then
        push(value)
    else
        unPop(2)
    end
end
primitive[66] = primitive.NextPut

function primitive.AtEnd() -- primitiveAtEnd
    -- ST_TRACE_PRIMITIVE("");
    local stream = popStack()
    local array = stream[0] -- StreamArrayIndex
    local arrayClass = fetchClassOf(array)
    local lenght = lengthOf(array)
    local index = fetchIntegerOfObject(1,stream) -- StreamIndexIndex
    local limit = fetchIntegerOfObject(2,stream) -- StreamReadLimitIndex
    success = success and index < limit
    local classArray = memory.knownObjects[0x10] -- classArray
    success = success and ( arrayClass == classArray or 
	    arrayClass == memory.knownObjects[0x0e] ) -- classString
    if success then
        if index >= limit or index >= lenght then
            push( true )
        else
            push( false )
        end
    else
        unPop(1)
    end
end
primitive[67] = primitive.AtEnd

function primitive.ObjectAt() -- primitiveObjectAt
    -- ST_TRACE_PRIMITIVE("");
    -- read CompiledMethod header and literals
    local index = popInteger()
    local thisReceiver = popStack()
    success = success and index > 0
    success = success and index <= ( thisReceiver.count + 1 )
    index = index - 1
    if success then
    	if index == 0 then
    		push( thisReceiver.header )
    	else
        	push( thisReceiver[index-1] )
        end
    else
        unPop(2)
    end
end
primitive[68] = primitive.ObjectAt

function primitive.ObjectAtPut() -- primitiveObjectAtPut
    -- ST_TRACE_PRIMITIVE("");
    -- write CompiledMethod header and literals
    local newValue = popStack()
    local index = popInteger()
    local thisReceiver = popStack()
    success = success and index > 0
    success = success and index <= ( thisReceiver.count + 1 )
    index = index - 1
    if success then
    	if index == 0 then
    		thisReceiver.header = newValue
    	else
        	thisReceiver[index-1] = newValue
        end
        push( newValue )
    else
        unPop(3)
    end   
end
primitive[69] = primitive.ObjectAtPut

function primitive.New() -- primitiveNew
    -- ST_TRACE_PRIMITIVE("");
    local cls = popStack()
    local instSpec = cls[2] -- InstanceSpecIndex
    local size = C.St_extractBitsSi(4,14,instSpec) -- fixedFieldsOf inlined
    local isIndexable = C.St_extractBitsSi(2,2,instSpec) == 1 -- isIndexable inlined
    success = success and not isIndexable 
    if success then
	    local isPointers = C.St_extractBitsSi(0,0,instSpec) == 1 -- isPointers inlined
        if isPointers then 
        	local inst = { count = size } -- instantiateClassWithPointers inlined
        	setmetatable(inst,cls)
            push( inst )
        else
        	local inst = { data = memory.createArray(nil,size*2,true) }
         	setmetatable(inst,cls)
            push( inst ) -- instantiateClassWithWords inlined
        end
    else
        unPop(1)
    end
end
primitive[70] = primitive.New

function primitive.NewWithArg() -- primitiveNewWithArg
    -- ST_TRACE_PRIMITIVE("");
    local int = popStack()
    local size = positive16BitValueOf( int )
    local cls = popStack()
    local instSpec = cls[2] -- InstanceSpecIndex
    local isIndexable = C.St_extractBitsSi(2,2,instSpec) == 1 -- isIndexable inlined
    success = success and isIndexable 
    if success then
        size =  size + C.St_extractBitsSi(4,14,instSpec) -- fixedFieldsOf inlined
	    local isPointers = C.St_extractBitsSi(0,0,instSpec) == 1 -- isPointers inlined
	    local isWords = C.St_extractBitsSi(1,1,instSpec) == 1 -- isWords inlined
        if isPointers then 
         	local inst = { count = size } -- instantiateClassWithPointers inlined
        	setmetatable(inst,cls)
            push( inst )
        elseif isWords then 
        	local inst = { data = memory.createArray(nil,size*2,true) }
         	setmetatable(inst,cls)
            push( inst ) -- instantiateClassWithWords inlined
        else
         	local inst = { data = memory.createArray(nil,size,false) }
         	setmetatable(inst,cls)
           push( inst ) -- instantiateClassWithBytes inlined
        end
    else
        unPop(2)
	end
end
primitive[71] = primitive.NewWithArg

function primitive.Become() -- primitiveBecome
    -- ST_TRACE_PRIMITIVE("");
    local otherPointer = popStack()
    local thisReceiver = popStack()
    success = success and type(otherPointer) == "table" and type(thisReceiver) == "table"
    if success then
    	local temp = {}
    	for k in pairs(thisReceiver) do
    		temp[k] = thisReceiver[k]
    		thisReceiver[k] = nil
		end
    	for k in pairs(otherPointer) do
    		thisReceiver[k] = otherPointer[k]
    		otherPointer[k] = nil
		end
     	for k in pairs(temp) do
    		otherPointer[k] = temp[k]
		end
		temp = fetchClassOf(thisReceiver)
		setmetatable(thisReceiver,fetchClassOf(otherPointer))
		setmetatable(otherPointer,temp)
        push(thisReceiver)
    else
        unPop(2)
    end
end
primitive[72] = primitive.Become

function primitive.InstVarAt() -- primitiveInstVarAt
    -- ST_TRACE_PRIMITIVE("");
    local index = popInteger()
    local thisReceiver = popStack()
    -- checkInstanceVariableBoundsOf(index,thisReceiver) inlined
    success = success and index >= 1 and index <= lengthOf(thisReceiver)
    local value = 0
    if success then
        value = subscriptWith(thisReceiver,index)
    end
    if success then
        push(value)
    else
        unPop(2)
    end
end
primitive[73] = primitive.InstVarAt

function primitive.InstVarAtPut() -- primitiveInstVarAtPut
    --ST_TRACE_PRIMITIVE("");
    local newValue = popStack()
    local index = popInteger()
    local thisReceiver = popStack()
    -- checkInstanceVariableBoundsOf(index,thisReceiver) inlined
    success = success and index >= 1 and index <= lengthOf(thisReceiver)
    if success then
        subscriptWithStoring(thisReceiver,index,newValue)
    end
    if success then
        push(newValue)
    else
        unPop(3)
    end
end
primitive[74] = primitive.InstVarAtPut

function primitive.AsOop() -- primitiveAsOop
    -- ST_TRACE_PRIMITIVE("");
    local thisReceiver = popStack()
    success = success and type(thisReceiver) == "table"
    if success then
    	if thisReceiver.oop then
    		push( thisReceiver.oop / 2 )
    	else
    		push( toaddress(thisReceiver) )
        end
    else
        unPop(1)
    end
end
primitive[75] = primitive.AsOop

function primitive.AsObject() -- primitiveAsObject
	success = false
	print "primitiveAsObject not supported"
end
primitive[76] = primitive.AsObject

function primitive.SomeInstance() -- primitiveSomeInstance
	-- ST_TRACE_PRIMITIVE("");
    local cls = popStack();
    local ot = memory.allObjects
    local nextInst
    while true do
    	nextInst = next( ot, nextInst )
    	if nextInst == nil or fetchClassOf(nextInst) == cls then break end
    end
    if nextInst then
        push(nextInst)
    else
        success = false
    end
end
primitive[77] = primitive.SomeInstance

function primitive.NextInstance() -- primitiveNextInstance
	-- ST_TRACE_PRIMITIVE("");
    local nextInst = popStack()
    local cls = fetchClassOf(nextInst)
    local ot = memory.allObjects
    while true do
    	nextInst = next( ot, nextInst )
    	if nextInst == nil or fetchClassOf(nextInst) == cls then break end
    end
    if nextInst then
        push(nextInst)
    else
        success = false
    end
end
primitive[78] = primitive.NextInstance

function primitive.NewMethod() -- primitiveNewMethod
    --ST_TRACE_PRIMITIVE("");
    local header = popStack()
    local bytecodeCount = popInteger()
    local cls = popStack()
    local literalCount = C.St_extractBitsSi(9,14,header) -- literalCountOfHeader
    local newMethod = { count = literalCount } -- instantiateClassWithBytes
    newMethod.bytecode = memory.createArray(nil,bytecodeCount,false)
    setmetatable(newMethod,cls)
    newMethod.header = header
    push( newMethod );
end
primitive[79] = primitive.NewMethod

function primitive.BlockCopy() -- primitiveBlockCopy
	local blockArgumentCount = popStack()
    local context = popStack()
    local methodContext
    if isBlockContext(context) then
        methodContext = context[5] -- HomeIndex
    else
        methodContext = context
    end
    local contextSize = methodContext.count;
    local newContext = { count = contextSize }
    setmetatable( newContext, memory.knownObjects[0x18] ) -- classBlockContext
    local initialIP = instructionPointer+3
    newContext[4] = initialIP -- InitialIPIndex
    newContext[1] = initialIP -- InstructionPointerIndex
    storeStackPointerValueInContext(0,newContext)
    newContext[3] = blockArgumentCount -- BlockArgumentCountIndex
    newContext[5] = methodContext -- HomeIndex
    push(newContext)
end
primitive[80] = primitive.BlockCopy

function primitive.Value() -- primitiveValue
    -- ST_TRACE_PRIMITIVE("");
    local blockContext = stackValue(argumentCount)
    local blockArgumentCount = fetchIntegerOfObject( 3, blockContext ) -- BlockArgumentCountIndex argumentCountOfBlock inlined
    success = success and argumentCount == blockArgumentCount
    if success then
        transfer(argumentCount, stackPointer-argumentCount+1,activeContext, 6, blockContext ) -- TempFrameStart
        pop(argumentCount+1)
        local initialIP = blockContext[4] -- InitialIPIndex
        blockContext[1] = initialIP -- InstructionPointerIndex
        storeStackPointerValueInContext(argumentCount, blockContext)
        blockContext[0] = activeContext -- CallerIndex
        newActiveContext(blockContext)
    end
end
primitive[81] = primitive.Value

function primitive.ValueWithArgs() -- primitiveValueWithArgs
    -- ST_TRACE_PRIMITIVE("");
    local argumentArray = popStack()
    local blockContext = popStack()
    local blockArgumentCount = fetchIntegerOfObject( 3, blockContext ) -- BlockArgumentCountIndex argumentCountOfBlock inlined
    local arrayClass = fetchClassOf(argumentArray)
    success = success and arrayClass == memory.knownObjects[0x10] -- classArray
    local arrayArgumentCount = 0
    if success then
        arrayArgumentCount = lengthOf(argumentArray)
    	success = success and arrayArgumentCount == blockArgumentCount
    end
    if success then
        transfer( arrayArgumentCount, 0, argumentArray, 6, blockContext ) -- TempFrameStart
        local initialIP = blockContext[4] -- InitialIPIndex
        blockContext[1] = initialIP -- InstructionPointerIndex
        storeStackPointerValueInContext( arrayArgumentCount, blockContext )
        blockContext[0] = activeContext -- CallerIndex
        newActiveContext( blockContext )
    else
        unPop(2)
	end
end
primitive[82] = primitive.ValueWithArgs

local function argumentCountOf(methodPointer)
	local flagValue = C.St_extractBitsSi(0,2,methodPointer.header) -- flagValueOf
	if flagValue < 5 then
		return flagValue
	end
	if flagValue < 7 then
		return 0
	else
		local extension = methodPointer[ methodPointer.count - 2 ] -- headerExtensionOf inlined
		return C.St_extractBitsSi(2,6,extension) -- literalCountOf, inlined
	end
end

function primitive.Perform() -- primitivePerform
    local performSelector = messageSelector
    local newSelector = stackValue(argumentCount-1)
    messageSelector = newSelector
    -- ST_TRACE_PRIMITIVE("selector" << memory->prettyValue(newSelector).constData());
    local newReceiver = stackValue(argumentCount)
    lookupMethodInClass( fetchClassOf(newReceiver) )
    success = success and argumentCountOf( newMethod ) == argumentCount - 1
    if success then
        local selectorIndex = stackPointer - argumentCount + 1
        transfer( argumentCount -1, selectorIndex + 1, activeContext, selectorIndex, activeContext )
        pop(1)
        argumentCount = argumentCount - 1
        executeNewMethod()
    else
        messageSelector = performSelector
	end
end
primitive[83] = primitive.Perform

function primitive.PerformWithArgs() -- primitivePerformWithArgs
    -- ST_TRACE_PRIMITIVE("");
    local argumentArray = popStack()
    local arraySize = lengthOf(argumentArray)
    local arrayClass = fetchClassOf(argumentArray)
    success = success and (stackPointer+arraySize) < lengthOf( activeContext ) -- fetchWordLenghtOf
    success = success and arrayClass == memory.knownObjects[0x10] -- classArray
    if success then
        local performSelector = messageSelector
        messageSelector = popStack()
        local thisReceiver = stackTop()
        argumentCount = arraySize
        local index = 1
        while index <= argumentCount do
            push( argumentArray[index-1] )
            index = index + 1
        end
    	lookupMethodInClass( fetchClassOf(thisReceiver) )
    	success = success and argumentCountOf( newMethod ) == argumentCount
        if success then
            executeNewMethod()
        else
            unPop(argumentCount)
            push( messageSelector)
            push(argumentArray)
            argumentCount = 2
            messageSelector = performSelector
        end
    else
        unPop(1)
	end
end
primitive[84] = primitive.PerformWithArgs

function primitive.Signal() -- primitiveSignal
    local top = stackTop()
    -- ST_TRACE_PRIMITIVE("stackTop" << memory->prettyValue(top).constData());
    synchronousSignal( top )
end
primitive[85] = primitive.Signal

local function suspendActive()
	-- wakeHighestPriority inlined
    local processLists = schedulerPointer()[0] -- ProcessListIndex
    local priority = processLists.count
    local processList
    while true do
        processList = processLists[ priority - 1 ]
        if isEmptyList( processList ) then
            priority = priority - 1
        else
            break
        end
    end
    local proc = removeFirstLinkOfList( processList )
    
    transferTo( proc )
end

local function wakeHighestPriority()
    local processLists = schedulerPointer()[0] -- ProcessListIndex
    local priority = lengthOf(processLists)
    local processList
    while true do
        processList = processLists[ priority - 1 ]
        if isEmptyList( processList ) then
            priority = priority - 1
        else
            break
        end
    end
    return removeFirstLinkOfList( processList )
end

function primitive.Wait() -- primitiveWait
    -- ST_TRACE_PRIMITIVE("");
    local thisReceiver = stackTop()
    local excessSignals = fetchIntegerOfObject( 2, thisReceiver ) -- ExcessSignalIndex
    if excessSignals > 0 then
        thisReceiver[2] = excessSignals - 1 -- storeIntegerOfObjectWithValue ExcessSignalIndex
    else
        addLastLinkToList( activeProcess(), thisReceiver )
        transferTo( wakeHighestPriority() ) -- suspendActive inlined
    end
end
primitive[86] = primitive.Wait

function primitive.Resume() -- primitiveResume
    -- ST_TRACE_PRIMITIVE("");
    resume( stackTop() )
end
primitive[87] = primitive.Resume

function primitive.Suspend() -- primitiveSuspend
    -- ST_TRACE_PRIMITIVE("");
    success = success and stackTop() == activeProcess()
    if success then
        popStack()
        push( nil )
        suspendActive()
    end
end
primitive[88] = primitive.Suspend

function primitive.FlushCache() -- primitiveFlushCache
	-- NOP
end
primitive[89] = primitive.FlushCache

function primitive.MousePoint() -- primitiveMousePoint
	success = false -- not used
end
primitive[90] = primitive.MousePoint

function primitive.CursorLocPut() -- primitiveCursorLocPut
    --ST_TRACE_PRIMITIVE("");
    local point = popStack()
	C.St_setCursorPos( point[0], point[1] ) -- XIndex, YIndex
end
primitive[91] = primitive.CursorLocPut

function primitive.CursorLink() -- primitiveCursorLink
    --ST_TRACE_PRIMITIVE("");
	print "WARNING: primitiveCursorLink not supported"
	popStack()
end
primitive[92] = primitive.CursorLink

function primitive.InputSemaphore() -- primitiveInputSemaphore
    -- ST_TRACE_PRIMITIVE("");
    inputSemaphore = popStack()
end
primitive[93] = primitive.InputSemaphore

function primitive.SamleInterval() -- primitiveSamleInterval
    --ST_TRACE_PRIMITIVE("");
    print "WARNING: primitiveSamleInterval not yet implemented"
    success = false
end
primitive[94] = primitive.SamleInterval

function primitive.InputWord() -- primitiveInputWord
	-- ST_TRACE_PRIMITIVE("");
    pop(1)
    push( C.St_nextEvent() ) -- inlined positive16BitIntegerFor
end
primitive[95] = primitive.InputWord

local RightMasks = { [0]=0, [1]=0x1, [2]=0x3, [3]=0x7, [4]=0xf,
       [5]=0x1f, [6]=0x3f, [7]=0x7f, [8]=0xff,
       [9]=0x1ff, [10]=0x3ff, [11]=0x7ff, [12]=0xfff,
       [13]=0x1fff, [14]=0x3fff, [15]=0x7fff, [16]=0xffff }
       
function primitive.CopyBits() -- primitiveCopyBits
	local bitblt = stackTop()

	local destBits, destW, destH
    local sourceBits, srcW, srcH
    local htBits, htW, htH
    
    local destForm = bitblt[0]
    assert( destForm )
	destBits = destForm[0].data
	destW = destForm[1]
	destH = destForm[2]
	local sourceForm = bitblt[1]
    if sourceForm then
	    sourceBits = sourceForm[0].data
	    srcW = sourceForm[1]
	    srcH = sourceForm[2]	
	else
		srcW = 0
		srcH = 0
	end
    local halftoneForm = bitblt[2]
    if halftoneForm then
	    htBits = halftoneForm[0].data
	    htW = halftoneForm[1]
	    htH = halftoneForm[2]	
	else
		htW = 0
		htH = 0
	end

    --ST_TRACE_PRIMITIVE()
    
    ----[[
    local function bitBlt( destBits, destW, destH,
                          sourceBits, srcW, srcH,
                          htBits, htW, htH,
                          combinationRule,
                          destX, destY, width, height,
                          sourceX, sourceY,
                          clipX, clipY, clipWidth, clipHeight )
	    local sourceRaster
        local destRaster
        local skew, mask1, mask2, skewMask, nWords, vDir, hDir
        local sx, sy, dx, dy, w, h -- pixel
        local sourceIndex, destIndex, sourceDelta, destDelta
        local preload

	    local function clipRange()
            -- set sx/y, dx/y, w and h so that dest doesn't exceed clipping range and
            -- source only covers what needed by clipped dest
            if destX >= clipX then
                sx = sourceX
                dx = destX
                w = width
            else
                sx = sourceX + ( clipX - destX )
                w = width - ( clipX - destX )
                dx = clipX
            end
            if ( dx + w ) > ( clipX + clipWidth ) then
                w = w - ( ( dx + w ) - ( clipX + clipWidth ) )
            end
            if destY >= clipY then
                sy = sourceY
                dy = destY
                h = height
            else
                sy = sourceY + clipY - destY
                h = height - clipY + destY
                dy = clipY
            end
            if ( dy + h ) > ( clipY + clipHeight ) then
                h = h - ( ( dy + h ) - ( clipY + clipHeight ) )
            end
            if sx < 0 then
                dx = dx - sx;
                w = w + sx;
                sx = 0;
            end
            if sourceBits and ( sx + w ) > srcW then
                w = w - ( sx + w - srcW )
            end
            if sy < 0 then
                dy = dy - sy
                h = h + sy
                sy = 0
            end
            if sourceBits and ( sy + h ) > srcH then
                h = h - ( sy + h - srcH )
            end
        end
        
        local function computeMasks()
            destRaster = mathfloor( ( ( destW - 1 ) / 16 ) + 1 )
            if sourceBits then
                sourceRaster = mathfloor( ( ( srcW - 1 ) / 16 ) + 1 )
            else
	            sourceRaster = 0
            end
            skew = bitand( ( sx - dx ), 15 )
            local startBits = 16 - bitand( dx , 15 )
            mask1 = RightMasks[ startBits ]
            local endBits = 15 - bitand( ( dx + w - 1 ), 15 )
            mask2 = bit.bnot( RightMasks[ endBits ] )
            if skew == 0 then
	            skewMask = 0
	        else
	            skewMask = RightMasks[ 16 - skew  ]
            end
            if w < startBits then
                mask1 = bitand( mask1, mask2 )
                mask2 = 0
                nWords = 1
            else
                nWords = mathfloor( ( w - startBits + 15) / 16 + 1 )
            end
        end

		local function checkOverlap()
            hDir = 1
            vDir = 1
            if sourceBits == destBits and dy >= sy then
                if dy > sy then
                    vDir = -1
                    sy = sy + h - 1
                    dy = dy + h - 1
                elseif dx > sx then
                    hDir = -1
                    sx = sx + w - 1
                    dx = dx + w - 1
                    skewMask = bit.bnot(skewMask)
                    local t = mask1
                    mask1 = mask2
                    mask2 = t
                end -- if
            end -- if
        end -- checkOverlap
   
		local function calculateOffsets()
            preload = ( sourceBits and skew ~= 0 and skew <= bitand( sx, 15 ) )
            if hDir < 0 then
                preload = preload == false
            end
            sourceIndex = sy * sourceRaster + mathfloor( sx / 16 )
            destIndex = dy * destRaster + mathfloor( dx / 16 )
            local off = 0
            if preload then
	            off = 1
	        end
            sourceDelta = ( sourceRaster * vDir ) - ( (nWords + off ) * hDir )
            destDelta = ( destRaster * vDir ) - ( nWords * hDir )
        end  -- calculateOffsets
        
	    local function copyLoop()
            local prevWord, thisWord, skewWord, mergeMask, halftoneWord, mergeWord, word
            local bitor = bit.bor
            local bitnot = bit.bnot
            local lshift = bit.lshift
            local rshift = bit.rshift
            local bitxor = bit.bxor
            local function merge(combinationRule, source, destination)
				if combinationRule == 0 then
					return 0
				elseif combinationRule == 1 then
					return bitand(source, destination)
				elseif combinationRule == 2 then
					return bitand( source, bitnot(destination) )
				elseif combinationRule == 3 then
					return source
				elseif combinationRule == 4 then
					return bitand( bitnot(source), destination )
				elseif combinationRule == 5 then
					return destination
				elseif combinationRule == 6 then
					return bitxor( source, destination )
				elseif combinationRule == 7 then
					return bitor( source, destination )
				elseif combinationRule == 8 then
					return bitand( bitnot(source), bitnot(destination) )
				elseif combinationRule == 9 then
					return bitxor( bitnot(source), destination )
				elseif combinationRule == 10 then
					return bitnot(destination)
				elseif combinationRule == 11 then
					return bitor( source, bitnot(destination) )
				elseif combinationRule == 12 then
					return bitnot(source)
				elseif combinationRule == 13 then
					return bitor( bitnot(source), destination )
				elseif combinationRule == 14 then
					return bitor( bitnot(source), bitnot(destination) )
				elseif combinationRule == 15 then
					return 0xffff -- AllOnes
				else
					print "WARNING: unknown combination rule"
					return 0
				end
			end -- merge
            
            for i = 1, h do
                if htBits then
                    halftoneWord = htBits.data[ bitand( dy, 15 ) ] -- left out +1 since we're 0 based
                    dy = dy + vDir
                else
                    halftoneWord = 0xffff -- AllOnes
                end
                skewWord = halftoneWord
                if preload and sourceBits then
                    prevWord = sourceBits.data[ sourceIndex ] -- left out +1
                    sourceIndex = sourceIndex + hDir
                else
                    prevWord = 0
                end
                mergeMask = mask1;
                for word = 1, nWords do
                    if sourceBits then
                        prevWord = bitand( prevWord, skewMask )
                        if word <= sourceRaster and sourceIndex >= 0 and 
	                        sourceIndex < sourceBits.count then
                            thisWord = sourceBits.data[ sourceIndex ] -- left out +1
                        else
                            thisWord = 0
                        end
                        skewWord = bitor( prevWord, bitand( thisWord, bitnot(skewMask) ) )
                        prevWord = thisWord
                        skewWord = bitor( lshift( skewWord, skew ), rshift( skewWord, -( skew - 16 ) ) )
                    end
                    if destIndex >= destBits.count then
                        return
                    end
                    local destWord =  destBits.data[ destIndex ] -- left out +1
                    mergeWord = merge( combinationRule, bitand( skewWord, halftoneWord ), destWord )
                    destBits.data[ destIndex ] =  -- left out +1
	                    bitor( bitand( mergeMask, mergeWord ), bitand( bitnot(mergeMask), destWord ) )
                    sourceIndex = sourceIndex + hDir
                    destIndex = destIndex + hDir
                    if word == ( nWords - 1 ) then
                        mergeMask = mask2
                    else
                        mergeMask = 0xffff -- AllOnes
                    end
                end
                sourceIndex = sourceIndex + sourceDelta
                destIndex = destIndex + destDelta
            end -- for
        end  -- copyLoop
        
        clipRange()
	    if w <= 0 or h <= 0 then
	        return
	    end
	    computeMasks()
	    checkOverlap()
	    calculateOffsets()
	    copyLoop()
	    C.St_update( destBits,
					  destX, destY, width, height,
					  clipX, clipY, clipWidth, clipHeight )
	end -- bitBlt
	--]]

	--C.St_bitBlt( 
	bitBlt(
		destBits, destW, destH, 
		sourceBits, srcW, srcH, 
		htBits, htW, htH,
		bitblt[3], -- combinationRule
		bitblt[4],bitblt[5],bitblt[6],bitblt[7], -- destX, destY, width, height
		bitblt[8],bitblt[9], -- sourceX, sourceY
		bitblt[10],bitblt[11],bitblt[12],bitblt[13] -- clipX, clipY, clipWidth, clipHeight
		) 
end
primitive[96] = primitive.CopyBits

function primitive.Snapshot() -- primitiveSnapshot
	print "WARNING: primitiveSnapshot not yet implemented"
end
primitive[97] = primitive.Snapshot

function primitive.TimeWordsInto() -- primitiveTimeWordsInto
	local array = popStack() -- LargePositiveInteger
	C.St_timeWords(array.data)
end
primitive[98] = primitive.TimeWordsInto

function primitive.TickWordsInto() -- primitiveTickWordsInto
	local array = popStack() -- LargePositiveInteger
	C.St_tickWords(array.data)
end
primitive[99] = primitive.TickWordsInto

function primitive.SignalAtTick() -- primitiveSignalAtTick
    local time = popStack() -- LargePositiveInteger
    toSignal = popStack()
    C.St_wakeupOn(time.data)
end
primitive[100] = primitive.SignalAtTick

function primitive.BeCursor() -- primitiveBeCursor
    -- ST_TRACE_PRIMITIVE("");
    local cursor = popStack()
	local bitmap = cursor[0].data
    local width = cursor[1]
    local height = cursor[2]
    C.St_beCursor( bitmap, width, height )
end
primitive[101] = primitive.BeCursor

function primitive.BeDisplay() -- primitiveBeDisplay
	local displayScreen = popStack()
	local bitmap = displayScreen[0].data
    local width = displayScreen[1]
    local height = displayScreen[2]
    C.St_beDisplay( bitmap, width, height )
end
primitive[102] = primitive.BeDisplay

function primitive.ScanCharacters() -- primitiveScanCharacters
	success = false -- optional
end
primitive[103] = primitive.ScanCharacters

function primitive.DrawLoop() -- primitiveDrawLoop
	success = false -- optional
end
primitive[104] = primitive.DrawLoop

function primitive.StringReplace() -- primitiveStringReplace
	-- optional
	success = false
end
primitive[105] = primitive.StringReplace

function primitive.Equivalent() -- primitiveEquivalent
    --ST_TRACE_PRIMITIVE("");
    local otherObject = popStack()
    local thisObject = popStack()
    push( thisObject == otherObject )
end
primitive[110] = primitive.Equivalent

function primitive.Class() -- primitiveClass
    --ST_TRACE_PRIMITIVE("");
    local instance = popStack()
    push( fetchClassOf(instance) )
end
primitive[111] = primitive.Class

function primitive.CoreLeft() -- primitiveCoreLeft
	push( 0xffff ) -- phantasy number
end
primitive[112] = primitive.CoreLeft

function primitive.Quit() -- primitiveQuit
	C.St_stop()
end
primitive[113] = primitive.Quit

function primitive.ExitToDebugger() -- primitiveExitToDebugger
	print "WARNING: primitiveExitToDebugger not implemented"
end
primitive[114] = primitive.ExitToDebugger

function primitive.OopsLeft() -- primitiveOopsLeft
	push( 0xffff ) -- phantasy number
end
primitive[115] = primitive.OopsLeft

function primitive.SignalAtOopsLeftWordsLeft() -- primitiveSignalAtOopsLeftWordsLeft
	print "WARNING: primitiveSignalAtOopsLeftWordsLeft not implemented"
end
primitive[116] = primitive.SignalAtOopsLeftWordsLeft

-- 129 to 255 dispatchPrivatePrimitives

function primitive.PosixFileOperation()
	-- this is the Lua version of dbanay's void Interpreter::primitivePosixFileOperation() with modifications
    local page = popStack()
    local name = popStack()
    local code = popStack()
    local file = popStack()

    local DescriptorIndex  = 8 -- fd field of PosixFile
    local PageNumberIndex  = 3 -- pageNumber  field in PosixFilePage
    local PageInPageIndex  = 1 -- ByteArray contents in PosixFilePage
    local BytesInPageIndex = 4 -- bytesInPage field in PosixFilePage
    local PageSize = 512 -- MUST match page size of PosixFilePage
    
    success = success and code >= 0 and code <= 6 and file
    if success then
	    if code == 4 then -- open
		    local fd = C.St_openFile(name.data)
		    if fd >= 0 then
			    file[DescriptorIndex] = fd
			    push(true)
			else
				push(false)
			end
		elseif code == 5 then -- close
			local fd = file[DescriptorIndex]
			file[DescriptorIndex] = nil
			fd = C.St_closeFile(fd)
			push( fd >= 0 )
		elseif code == 3 then -- size
			local fd = file[DescriptorIndex]
			local size
			if fd then
				size = C.St_fileSize(fd)
			end
			if size >= 0 then
				push(size)
			else
				push(nil)
			end
		elseif code == 0 then -- read page
			local fd = file[DescriptorIndex]
			local pageNumber = page[PageNumberIndex]	
			success = success and pageNumber >= 1 and fd
			if success then
				local byteArray = page[PageInPageIndex]
				local position = (pageNumber - 1)*PageSize
				if C.St_seekFile(fd,position) == position then
					local read = C.St_readFile(fd,byteArray.data)
					page[BytesInPageIndex] = read
					push(true)
				else
					push(false)
				end
			end		
		elseif code == 1 then -- write page
			local fd = file[DescriptorIndex]
			local pageNumber = page[PageNumberIndex]	
			success = success and pageNumber >= 1 and fd
			if success then
				local byteArray = page[PageInPageIndex]
				local position = (pageNumber - 1)*PageSize
				if C.St_seekFile(fd,position) == position then
					local toWrite = page[BytesInPageIndex]
					local written = C.St_writeFile(fd,byteArray.data,toWrite)
					push(written==toWrite)
				else
					push(false)
				end
			end	
		elseif code == 2 then -- truncate at page
			local fd = file[DescriptorIndex]
			success = success and fd
			if success then
				if page then
					local pageNumber = page[PageNumberIndex]	
					local bytesInPage = page[BytesInPageIndex];
					local newSize = (pageNumber-1)*PageSize + bytesInPage;
					push( C.St_truncateFile(fd,newSize) >= 0 )
				else
					push( C.St_truncateFile(fd,0) >= 0 )
				end
			end							
		end
    end
    if not success then
	    unPop(4)
	end
end
primitive[130] = primitive.PosixFileOperation

function primitive.PosixDirectoryOperation()
	-- this is the Lua version of dbanay's void Interpreter::primitivePosixDirectoryOperation() with modifications
    local DescriptorIndex  = 8 -- fd field of PosixFile
	local FileNameIndex = 1
 
    local arg2 = popStack()
    local arg1 = popStack()
    local code = popInteger()
    pop(1) -- remove receiver
    
    success = success and code >= 0 and code <= 3 and ( arg1 == nil or arg1.data )
    
    if success then
	    if code == 0 then -- create file
		    local fd = C.St_createFile(arg1.data)
		    if fd >= 0 then
			    push(fd)
			else
				push(nil)
			end
		elseif code == 1 then -- delete file
			push( C.St_deleteFile(arg1.data) >= 0 )
		elseif code == 2 then -- rename file
			local file = arg2
			local newName = arg1
			local oldName = file[FileNameIndex]
			push( C.St_renameFile(oldName.data,newName.data) >= 0 )			
		elseif code == 3 then -- list files
			local files = { getfilesofdir() }
			local strCls = memory.knownObjects[0x0e] -- String
			local array = { count = #files }
			setmetatable( array, memory.knownObjects[0x10] ) -- classArray
			for i=1,#files do
				local str = files[i]
				local obj = { data = memory.createArray(nil,#str,false) }
				ffi.copy(obj.data.data, str)
				print( ffi.string(obj.data.data) )
	         	setmetatable(obj, strCls )
	         	array[i-1] = obj
			end	
			push( array )
	    end
    else
	    unPop(4)
    end
end
primitive[131] = primitive.PosixDirectoryOperation


function runInterpreter()
	local interpreter = require "Interpreter"
	local memory = require "ObjectMemory"
	if not VirtualImage then
		print "Global variable VirtualImage is not defined!"
		return
	end
	if memory.loadImage( VirtualImage ) then
		bitand = bit.band
		classSmallInteger = memory.knownObjects[0x0c]
		classLargePositiveInteger = memory.knownObjects[0x1c]
		classFloat = memory.knownObjects[0x14]
		classCompiledMethod = memory.knownObjects[0x22]
		classCharacter = memory.knownObjects[0x28]
		mathfloor = require("math").floor
		classTrue = memory.knownObjects.True
		classFalse = memory.knownObjects.False
		module.interpret()
	end
end

return module
