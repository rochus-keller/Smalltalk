## About this project

Some time ago I came accross Mario Wolczko's site (http://www.wolczko.com/st80) when searching for an original 
Smalltalk-80 implementation corresponding to the famous Smalltalk "Bluebook" (see http://stephane.ducasse.free.fr/FreeBooks/BlueBook/Bluebook.pdf). 
I used Smalltalk in the nineties and also played around with Squeak and Pharo.
But I was surprised that there was no VM around capable of running the original Xerox images, which
Mario provides on this link: http://www.wolczko.com/st80/image.tar.gz. I wasn't able to load this image with
the aforementioned VMs; and even after numerous modifications, I still haven't managed to get the VM of Mario to work
(it now compiles but crashes before the image is fully loaded).

Since I recently completed an Oberon implementation based on LuaJIT (see https://github.com/rochus-keller/Oberon)
I got interested in the question whether it would be feasible to use LuaJIT as a backend for Smalltalk-80, and 
what performance it would achieve compared to Cog (see https://github.com/OpenSmalltalk/opensmalltalk-vm).
There are many similarities between Lua and Smalltalk, even though the syntax is very different. 
I see two implementation variants: run everything from the Smalltalk source code, or run it from the
Smalltalk image (i.e. Bluebook bytecode). To further analyze the Xerox implementation and make a decision
I needed a good tool, so here we are.

AND NOT TO FORGET: Smalltalk-80 turns 40 this year (2020), and Alan Kay turns 80 (on May 17), and Xerox PARC turns 50 (on July 1)!

Interim conclusion July 2020: I have implemented and optimized a few tools and two Bluebook bytecode interpreters - one in C++ and the other in Lua running on LuaJIT. The Lua version of the interpreter is very fast (i.e. almost as fast as the C++ version) and once again demonstrates the incredible performance of LuaJIT, but not faster than one would expect from an interpreter. I have looked at several ways to get rid of the interpreter and translate the Bluebook bytecode directly to Lua; but none so far seems feasible without changing the Smalltalk virtual image (which I don't want). The main problem is that a significant part of the VM (e.g. the scheduler, execution contexts and stacks) is implemented directly in Smalltalk and part of the virtual image, and that the virtual image makes many concrete assumptions about the interpreter and the memory model, so these cannot be replaced easily. At the moment I'm following two approaches and also want to extend the Lua version so that you can load benchmarks that also run on current Smalltalk VMs.

Update September 2020: Meanwhile I implemented a Smalltalk to Lua and LuaJIT bytecode translator for the SOM dialect and was able to do comparative measurements based on the https://github.com/smarr/are-we-fast-yet benchmark suite; from these experiments I could draw the conclusion that a LuaJIT-based speed-up is currently not possible, because Smalltalk blocks have to be implemented as closures, but the tracing JIT compiler does currently not support their instantiation; the code therefore runs mostly in the interpreter; see https://github.com/rochus-keller/Som for the details. 

Final conclusion December 2020: With the https://github.com/rochus-keller/Som project I was able to demonstrate that even if the tracing JIT compiler of LuaJIT would support closures, a Smalltalk/SOM implementation based on LuaJIT would still be at least factor 7 slower than a plain Lua on LuaJIT implementation or a Smalltalk implementation based on Cog/Spur (such as e.g. Pharo 7). To achieve the performance of Pharo 7 therefore aggressive optimizations on bytecode and VM level would be required, as it was done in https://github.com/OpenSmalltalk/opensmalltalk-vm over a period of 20 years. 

With this project I could show that a Smalltalk-80 implementation on LuaJIT is feasible and achieves a respectable performance with reasonable effort, but by no means the performance of an implementation like Cog/Spur, which has been optimized over decades. 


### A Smalltalk-80 parser and code model written in C++

Of course I could have implemented everything in Smalltalk as they did with the original Squeak and Cog VMs. But
over the years I got used to strictly/statically typed programming and C++ is my main programming language since
more than twenty years. And there is Qt (see https://www.qt.io/) which is a fabulous framework for (nearly) 
everything. And LuaJIT is written in C and Assembler. I therefore consider C++ a good fit.

Usually I start with an EBNF syntax and transform it to LL(1) using my own tools (see https://github.com/rochus-keller/EbnfStudio).
Smalltalk is different. There are syntax specifications available (even an ANSI standard), but there is a plethora
of variations. So I just sat down and wrote a lexer and then a parser and modified it until it could parse the
Smalltalk-80.sources file included with http://www.wolczko.com/st80/image.tar.gz. The parser feeds a code model
which does some additional validations and builds the cross-reference lists. On my old 32 bit HP EliteBook 2530p
the whole Smalltalk-80.sources file is parsed and cross-referenced in less than half a second. 

There is also an AST and a visitor which I will use for future code generator implementations.


### A Smalltalk-80 Class Browser written in C++

The Class Browser has a few special features that I need for my analysis. There is syntax highlighting of course
but it also marks all keywords of the same message and all uses of a declaration. If you click on the identifier
there is a tooltip with information whether it's a temporary, instance or class variable, etc. If you CTRL+click on a 
class identifier or on a message sent to an explicit class then it navigates to this class and method. There is also
a list with all methods of all classes where a variable is used or assigned to; other lists show all message patterns
or primaries used in the system and in which classes/methods they are implemented.
There is also a browsing history; you can go back or forward using the ALT+Left and ALT+Right keys.


Here are some screenshots:

![Overview](http://software.rochus-keller.ch/smalltalk80_class_browser.png)

![Mark Variables](http://software.rochus-keller.ch/st80_browser_mark_variable_show_origin.png)

![Mark Message Selector](http://software.rochus-keller.ch/st80_browser_mark_all_keywords_of_message.png)

![Xref](http://software.rochus-keller.ch/st80_browser_where_used_or_assigned.png)


### A Smalltalk-80 Image Viewer

With the Image Viewer one can inspect the contents of the original Smalltalk-80 Virtual Image in the
interchange format provided at http://www.wolczko.com/st80/image.tar.gz. The viewer presents the
object table in a tree; known objects (as defined in the Bluebook part 4) and classes are printed
by name; an object at a given oop can be expanded, so that object pointers can be followed; when clicking
on an object or its class the details are presented in html format with navigable links; by CTRL-
clicking on a list item or link a list or detail view opens with the given object as root. There is a
dedicated list with all classes and metaclasses found in the image, as well as a cross-reference list
from where a given oop is referenced. Detail views of methods also show bytecode with descriptions.
There is also a browsing history; you can go back or forward using the ALT+Left and ALT+Right keys.
Use CTRL+G to navigate to a given OOP, and CTRL+F to find text in the detail view (F3 to find again).


Here is a screenshot:

![Overview](http://software.rochus-keller.ch/smalltalk80_image_viewer_0.5.png)


### A Smalltalk-80 Interpreted Virtual Machine in C++

This is a bare bone Bluebook implementation to understand and run the original Smalltalk-80 Virtual Image in the interchange format provided at http://www.wolczko.com/st80/image.tar.gz. The focus is on functionality and compliance with the Bluebook, not on performance (it performs decently though) or productive work. Saving snapshots is not implemented. My goal is to gradualy migrate the virtual machine to a LuaJIT backend, if feasible. The interpreter reproduces the original Xerox trace2 and trace3 files included with http://www.wolczko.com/st80/image.tar.gz. The initial screen after startup corresponds to the screenshot shown on page 3 of the "Smalltalk 80 Virtual Image Version 2" manual. This is still work in progress though; there are some view update issues and don't be surprised by sporadic crashes.

Note that you can press CTRL+left mouse button to simulate a right mouse button click, and CTRL+SHIFT+left mouse button to simulate a middle mouse button click. If you have a two button mouse, then you can also use SHIFT+right mouse button to simulate a middle mouse button click.

All keys on the Alto keyboard (see e.g. https://www.extremetech.com/wp-content/uploads/2011/10/Alto_Mouse_c.jpg) besides LF are supported; just type the key combination for the expected symbol on your local keyboard. Use the left and up arrow keys to enter a left and up arrow character.

The VM supports some debugging features. If you press ALT+B the interpreter breaks and the Image Viewer is shown with the current state of the object memory and the interpreter registers. The currently active process is automatically selected and the current call chain is shown. When the Image Viewer is open you can press F5 (or close the viewer) to continue, or press F10 to execute the next bytecode and show the Image Viewer again. There are also some other shortcuts for logging (ALT+L) and screen update recording (ALT+R), but these only work if the corresponding functions are enabled when compiling the source code (see ST_DO_TRACING and ST_DO_SCREEN_RECORDING).
If you press ALT+V, the text on the clipboard is sent to the VM as keystrokes; only characters with a corresponding Alto key combination are considered. Conversely, you can transfer text located on the clipboard of the VM to the clipboard of the host OS by pressing ALT+C. For convenience, ALT+SHIFT+V sends the Smalltalk expression found in Benchmark.st to the VM.


Here is a screenshot of the running VM after some interactions:

![Screenshot](http://software.rochus-keller.ch/smalltalk80_vm_0.3.3.png)

I don't seem to be the only one interested in an original Bluebook Smalltalk-80 VM. Today (May 18 2020) I found this very interesting post on Reddit: https://www.reddit.com/r/smalltalk/comments/glqbrh/in_honor_of_alans_birthday_by_the_bluebook_c/ which refers to https://github.com/dbanay/Smalltalk. The initial commit was apparently on May 12, but most files have a creation date of February or March, one even of December; so obviously it was quite some work; the implementation is based on C++11 and SDL and seems to work very well; I also found Bluebook fixes in Dan's code which I didn't see anywhere else (thanks, Dan!). He implemented a bunch of convenience features like copy paste with the host, and file system access/persistent snapshots, with a customized image. So if you're up to using it for productive work and don't care that you can't directly load the original Xerox virtual image, then better take a look at his implementation.

### A Smalltalk-80 Interpreted Virtual Machine on LuaJIT

This is a Lua translation of the C++ based VM described above. The interpreter including the primitives (besides BitBlt) was re-implemented in Lua. The object memory is replaced with the LuaJIT memory manager; nil, Boolean and SmallInteger are directly represented by Lua primitive types; Smalltalk objects are represented by Lua tables; byte and word arrays use LuaJIT FFI native types (see StLjObjectMemory.cpp for more information).

The interpreter reproduces the original Xerox trace2 and trace3 files included with http://www.wolczko.com/st80/image.tar.gz; the initial screen after startup corresponds to the screenshot shown on page 3 of the "Smalltalk 80 Virtual Image Version 2" manual; even the trace log of the first 121k cycles (the time to fully display the initial screen) is identical with the one produced by the C++ implementation.


Since the whole interpreter is written in Lua which runs on LuaJIT the approach could be seen as a "meta-tracing JIT" (i.e. the tracing JIT directly effects the language interpreter, and only indirectly the user program running on it). But there is no speedup yet comparable to e.g. RPython where the JIT version of the VM runs four times faster than the C based interpreter. I will investigate this point further and consider ways to replace the interpreter with a translator from Bluebook directly into LuaJIT bytecode. For now, the approach can be used for representative performance comparisons between LuaJIT and C++ based on a sufficiently complex and realistic application (as opposed to micro benchmarks). 


I did some comparative measurements on my laptop (Intel Core Duo L9400 1.86GHz, 4GB RAM, Linux i386). It takes about 830 ms to run the first 121k cycles with the C++ version of the VM, and about 1000 ms with the LuaJIT version (with trace functions commented out, otherwise it takes about 250 ms more). A full run of the Smalltalk 'Benchmark testStandardTests' method takes 141 seconds with the C++ version (geometric mean of five runs) and 153 seconds with the LuaJIT version (geometric mean of ten runs, VM started from command line, without IDE). It is fair to conclude that the same program takes only a factor of 1.1 more time when run on LuaJIT compared to the native C++ version. This confirms the findings with my Oberon compiler (see https://github.com/rochus-keller/Oberon/blob/master/testcases/Hennessy_Results). 

Meanwhile I was daring and migrated BitBlt to Lua as well (in the previous measurements BitBlt was implemented in C++). The impact on the execution time is amazingly minor. The time to run the first 121k cycles increased by about 30ms, and running 'Benchmark testStandardTests' prolongates from 153 to 157 seconds (geometric mean of ten runs, VM started from command line, without IDE). The speed-down factor grows from 1.08 to 1.11, which still rounds to 1.1.

An analysis with Valgrind revealed that the most expensive functions are mutex locking/unlocking, malloc/free and QImage handling. So I did some optimizations concerning QImage. In commit 1f976e5b5ee3cfe40921c74b97edb8d84ff39dd3 I reorganized the Smalltalk bitmap to QImage conversion which resulted in testStandardTests running time of 142 seconds for the C++ version and 147 seconds for the Lua (including BitBlt) version, corresponding to a speed-down factor of 1.03! In commit 295f7fb5937d88792eff62f74ab235f4cb989ec8 I changed the QImage format from Mono to RGB32 because the latter is used by the Qt drawing pipeline and I thus hoped for a speed-up because of less format conversions; but the running time of the C++ version increased to 144 seconds and of the Lua version to 154 seconds; the resulting speed-down factor from C++ to Lua is 1.07, which is still very good; an explanation for the speed-down could be that bitmaps are copied more often than drawn and copying 8 pixels per byte is faster than 1 pixel per byte. 

After further analysis and tests I was able to achieve a considerable performance increase by giving computing time to Qt only each 30ms; this causes several drawing operations to be combined before the Qt drawing engine can update the display; 30ms is short enough that the interaction still appears smooth. In consequence the execution time of testStandardTests is reduced to 36 seconds for the C++ version and 50 seconds for the Lua version (geometric mean of five runs each). Interestingly the first run with the Lua version is the fastest; all other runs are about 10ms slower than the first one; this requires further investigation. The speed-down factor of the first run is 1.14, whereas the speed-down factor based on the geometric mean of all runs increases to 1.35. 

I was able to achieve yet another significant performance gain. As it turned out, a call of QElapsedTimer::elapsed() is surprisingly expensive. Therefore I added another counter, so that elapsed() is called only every few thousand cycles of the interpreter. The C++ version now only needs 5.1 seconds for one benchmark run, which is incredible 28 times less than the measurement before the optimizations. The results of the Lua version vary considerably; the fastest measured time is 9.6 seconds, and the slowest 24.2 seconds; the resulting speed-down factor for the best case is 1.87, which is worse than 1.1, but about as fast as Julia or DotNet Core (and faster than e.g. Java, GoLang and V8) on the current CLBG charts (see [chart1](http://software.rochus-keller.ch/St80LjVirtualMachine_0.6.1_clbg_comparison_compiled_languages.png) and [chart2](http://software.rochus-keller.ch/St80LjVirtualMachine_0.6.1_clbg_comparison_scripting_languages.png)). Unfortunately there seems to be an issue in LuaJIT, because of which after some time the fast traces are abandoned for no good reason; as a consequence it can happen that the first runs of testStandardTests take 10 seconds each and the following ones suddently 20 seconds, even though it's still the same LuaJIT session and code; here are some results which demonstrate the issue: [perormance report PDF](http://software.rochus-keller.ch/St80LjVirtualMachine_0.6_Performance_Report_2020-07-14.pdf). I've learned from [this post](https://www.freelists.org/post/luajit/Inexplicable-behaviour-of-JIT-sudden-speeddown-ofthe-samecode-that-was-running-fast-at-first-in-a-longrunning-Lua-programupdate) that this is a known issue but the only work-around currently available is to switch off Address Space Layout Randomisation (ASLR). I can confirm that this reduces the probability of the issue, at least on my test machine (see [here](https://www.freelists.org/post/luajit/Inexplicable-behaviour-of-JIT-sudden-speeddown-ofthe-samecode-that-was-running-fast-at-first-in-a-longrunning-Lua-program-partial-success) for results); but of course this is not the definitive solution.

**Finally I found the reason for the performance issue**. As it turns out, it was not actually a problem of LuaJIT, but it was the default parameters of LuaJIT that did not fit the present application and caused the observed problems. See [this post](https://www.freelists.org/post/luajit/Inexplicable-behaviour-of-JIT-sudden-speeddown-of-the-same-code-that-was-running-fast-at-first-in-a-longrunning-Lua-program,2) for more information. With optimized parameters the Lua version of the Smalltalk VM only needs **5.8 seconds** (with ASLR switched on) or 5.6 seconds (with ASLR switched off) per run, which is incredible 26 times less than the measurement before the optimizations. The corresponding speed-down factor compared to the C++ version is again **1.1** (i.e. **the LuaJIT version of the Smalltalk-80 VM only needs 5.8 seconds for the same task the C++ version needs 5.1 seconds**), as already found with the Oberon compiler. This is as fast as C or Rust on the current CLBG charts (see [chart1](http://software.rochus-keller.ch/St80LjVirtualMachine_0.6.1_clbg_comparison_compiled_languages.png) and [chart2](http://software.rochus-keller.ch/St80LjVirtualMachine_0.6.1_clbg_comparison_scripting_languages.png)). The lesson: LuaJIT is extremely fast, but it is necessary to optimize the JIT parameters appropriately for the given application; but even with non-optimal parameters we are at least as fast as Node.js, just amazing. 

Starting from version 0.6.2 the VM can also directly load and run the virtual image format used by [dbanay's implementation](https://github.com/dbanay/Smalltalk/blob/master/files/snapshot.im). Also the file and directory primitives are implemented and tested so that snapshot.im can access the sources and changes file. My intention is to load benchmarks which run on Smalltalk-80 as well as current Squeak and Pharo versions.

I was able to file in the Squeak tinyBenchmark into my Lua VM version 0.6.3 when running the dbanay snapshot.im (see file Squeak5-tinyBenchmarks-filein.st); the geomean of five runs is 5'351'408  bytecodes/sec and 268'008 sends/sec. The same benchmark runs on Squeak 5.3 with 1'279'354'588 bytecodes/sec and 63'102'795 sends/sec, and on Pharo 8.0 with 1'268'662'708 bytecodes/sec and 72'356'558 sends/sec (on my test machine specified above). Squeak is therefore 239 times faster in bytecodes/sec and 235 times in sends/sec than my Lua based Smalltalk-80 VM. But interestingly my Lua based VM is 13 times faster in bytecodes/sec and 7 times faster in sends/sec than dbanay's C++ based VM (version of May 18 2020), even though he has implemented method lookup caching (in contrast to my VMs). I wasn't able to run this benchmark on my C++ based VM yet.


The VM supports the ALT+V, ALT+SHIFT+V, ALT+C shortcuts (but not the other shortcuts supported by the C++ version). 

The VM integrates a Lua IDE with source-level debugger (see https://github.com/rochus-keller/LjTools#lua-parser-and-ide-features); the IDE can be enabled by the -ide or -pro command line option.

Here is a screenshot of the running VM with the Lua IDE/debugger with stack trace and local variables stepping at cycle 121000:

![Screenshot](http://software.rochus-keller.ch/smalltalk80_lj_vm_0.5.png)


### Binary versions

Here is a binary version of the class browser, the image viewer and the virtual machines for Windows: http://software.rochus-keller.ch/St80Tools_win32.zip.
Just unpack the ZIP somewhere on your drive and double-click St80ClassBrowser.exe, St80ImageViewer.exe, St80VirtualMachine.exe or St80LjVirtualMachine.exe; Qt libraries are included as well as the original Smalltalk-80.sources and VirtualImage file.

### Build Steps

Follow these steps if you want to build the tools yourself:

1. Make sure a Qt 5.x (libraries and headers) version compatible with your C++ compiler is installed on your system.
1. Download the source code from https://github.com/rochus-keller/Smalltalk/archive/master.zip and unpack it.
1. Goto the unpacked directory and execute e.g. `QTDIR/bin/qmake StClassBrowser.pro` (see the Qt documentation concerning QTDIR).
1. Run make; after a couple of seconds you will find the executable in the build directory.

Alternatively you can open the pro file using QtCreator and build it there. Note that there are different pro files in this project.

## Support
If you need support or would like to post issues or feature requests please use the Github issue list at https://github.com/rochus-keller/Smalltalk/issues or send an email to the author.



 
