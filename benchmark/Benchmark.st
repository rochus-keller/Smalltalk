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
