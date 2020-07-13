We're using the built-in Benchmark class without changing any class or method.

To run a benchmark proceed as follows:

1) clear and enlarge (SHIFT+right click, context menu item "frame") the System Transcript window.

2) select and copy the following Smalltalk expression to the clipboard:

Transcript show: 'Done on ', Time millisecondClockValue printString, ' in [ms] ', 
	( Time millisecondsToRun: [ #(testLoadInstVar testLoadTempNRef testLoadTempRef
			testLoadQuickConstant testLoadLiteralNRef testLoadLiteralIndirect
			testPopStoreInstVar testPopStoreTemp
			test3plus4 test3lessThan4 test3times4 test3div4 test16bitArith testLargeIntArith
			testActivationReturn testShortBranch testWhileLoop
			testArrayAt testArrayAtPut testStringAt testStringAtPut testSize
			testPointCreation testStreamNext testStreamNextPut testEQ testClass
			testBlockCopy testValue testCreation testPointX 
			testLoadThisContext
			testBasicAt testBasicAtPut testPerform testStringReplace
			testAsFloat testFloatingPointAddition testBitBLT testTextScanning
			testClassOrganizer testPrintDefinition testPrintHierarchy
 			testAllCallsOn testAllImplementors testInspect 
			testCompiler testDecompiler
			testKeyboardLookAhead testKeyboardSingle 
			testTextDisplay testTextFormatting testTextEditing ) do:
		[:selector | Benchmark new perform: selector] ] ) printString.
		
3) click into the System Transcript window and press ALT+V; this transfers the expression
   into the System Transcript window.
   
4) select the whole expression in the System Transcript window (by mouse click/drag or mouse click
   below the caret).
   
5) run the selected expression (right click, context menu item "do it").

6) wait until you see "Done".

7) note the duration, repeat this procedure if required.

Note that it is possible to copy/paste selected text from the VM to the host OS; for this purpose
select the text you want to copy and execute the copy command (right click, context menu item "copy")
in the VM. Then press ALT+C. Now the text is on the clipboard of the host OS.
