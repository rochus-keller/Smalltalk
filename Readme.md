## About this project

Some time ago I came accross Mario Wolczko's site (http://www.wolczko.com/st80) when searching for an original 
Smalltalk-80 implementation corresponding to the famous Smalltalk "Blue Book" (see http://stephane.ducasse.free.fr/FreeBooks/BlueBook/Bluebook.pdf). 
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
Smalltalk image (i.e. Blue Book bytecode). To further analyze the Xerox implementation and make a decision
I needed a good tool, so here we are.

AND NOT TO FORGET: Smalltalk-80 turns 40 this year (2020), and Alan Kay turns 80 (on May 17)!


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

![Overview](http://software.rochus-keller.info/smalltalk80_class_browser.png)

![Mark Variables](http://software.rochus-keller.info/st80_browser_mark_variable_show_origin.png)

![Mark Message Selector](http://software.rochus-keller.info/st80_browser_mark_all_keywords_of_message.png)

![Xref](http://software.rochus-keller.info/st80_browser_where_used_or_assigned.png)


### A Smalltalk-80 Image Viewer

With the Image Viewer one can inspect the contents of the original Smalltalk-80 Virtual Image in the
interchange format provided at http://www.wolczko.com/st80/image.tar.gz. The viewer presents the
object table in a tree; known objects (as defined in the Blue Book part 4) and classes are printed
by name; an object at a given oop can be expanded, so that object pointers can be followed; when clicking
on an object or its class the details are presented in html format with navigable links; by CTRL-
clicking on a list item or link a list or detail view opens with the given object as root. There is a
dedicated list with all classes and metaclasses found in the image, as well as a cross-reference list
from where a given oop is referenced. Detail views of methods also show bytecode with descriptions.
There is also a browsing history; you can go back or forward using the ALT+Left and ALT+Right keys.
Use CTRL+G to navigate to a given OOP, and CTRL+F to find text in the detail view (F3 to find again).


Here is a screenshot:

![Overview](http://software.rochus-keller.info/smalltalk80_image_viewer_0.5.png)


### A Smalltalk-80 Interpreted Virtual Machine in C++

This is a bare bone Blue Book implementation to understand and run the original Smalltalk-80 Virtual Image in the interchange format provided at http://www.wolczko.com/st80/image.tar.gz. The focus is on functionality and compliance with the Blue Book, not on performance (it performs decently though) or productive work. Saving snapshots is not implemented. My goal is to gradualy migrate the virtual machine to a LuaJIT backend, if feasible. The interpreter reproduces the original Xerox trace2 and trace3 files included with http://www.wolczko.com/st80/image.tar.gz. The initial screen after startup corresponds to the screenshot shown on page 3 of the "Smalltalk 80 Virtual Image Version 2" manual. This is still work in progress though; there are some view update issues and don't be surprised by sporadic crashes.

Note that you can press CTRL+left mouse button to simulate a right mouse button click, and CTRL+SHIFT+left mouse button to simulate a middle mouse button click. If you have a two button mouse, then you can also use SHIFT+right mouse button to simulate a middle mouse button click.

All keys on the Alto keyboard (see e.g. https://www.extremetech.com/wp-content/uploads/2011/10/Alto_Mouse_c.jpg) besides LF are supported; just type the key combination for the expected symbol on your local keyboard. Use the left and up arrow keys to enter a left and up arrow character.

The VM supports some debugging features. If you press ALT+B the interpreter breaks and the Image Viewer is shown with the current state of the object memory and the interpreter registers. The currently active process is automatically selected and the current call chain is shown. When the Image Viewer is open you can press F5 (or close the viewer) to continue, or press F10 to execute the next bytecode and show the Image Viewer again. There are also some other shortcuts for logging (ALT+L) and screen update recording (ALT+R), but these only work if the corresponding functions are enabled when compiling the source code (see ST_DO_TRACING and ST_DO_SCREEN_RECORDING).
If you press ALT+V, the text on the clipboard is sent to the VM as keystrokes; only characters with a corresponding Alto key combination are considered.


Here is a screenshot of the running VM after some interactions:

![Screenshot](http://software.rochus-keller.info/smalltalk80_vm_0.3.3.png)

I don't seem to be the only one interested in an original Blue Book Smalltalk-80 VM. Today (May 18 2020) I found this very interesting post on Reddit: https://www.reddit.com/r/smalltalk/comments/glqbrh/in_honor_of_alans_birthday_by_the_bluebook_c/ which refers to https://github.com/dbanay/Smalltalk. The initial commit was apparently on May 12, but most files have a creation date of February or March, one even of December; so obviously it was quite some work; the implementation is based on C++11 and SDL and seems to work very well (even a bit faster than this VM); I also found Blue Book fixes in Dan's code which I didn't see anywhere else (thanks, Dan!). He implemented a bunch of convenience features like copy paste with the host, and file system access/persistent snapshots, with a customized image. So if you're up to using it for productive work and don't care that you can't directly load the original Xerox virtual image, then better take a look at his implementation.

### A Smalltalk-80 Interpreted Virtual Machine on LuaJIT

This is a Lua translation of the C++ based VM described above. The interpreter including the primitives (besides BitBlt) was re-implemented in Lua. The object memory is replaced with the LuaJIT memory manager; nil, Boolean and SmallInteger are directly represented by Lua primitive types; Smalltalk objects are represented by Lua tables; byte and word arrays use LuaJIT FFI native types (see StLjObjectMemory.cpp for more information).

The interpreter reproduces the original Xerox trace2 and trace3 files included with http://www.wolczko.com/st80/image.tar.gz; the initial screen after startup corresponds to the screenshot shown on page 3 of the "Smalltalk 80 Virtual Image Version 2" manual; even the trace log of the first 121k cycles (the time to fully display the initial screen) is identical with the one produced by the C++ implementation.

I did some comparative measurements on my laptop (Intel Core Duo L9400 1.86GHz, 4GB RAM, Linux i386). It takes about 830 ms to run the first 121k cycles with the C++ version of the VM, and about 1000 ms with the LuaJIT version (with trace functions commented out, otherwise it takes about 250 ms more). A full run of the Smalltalk 'Benchmark testStandardTests' method takes 141 seconds with the C++ version (geometric mean of five runs) and 153 seconds with the LuaJIT version (geometric mean of ten runs, VM started from command line, without IDE). It is fair to conclude that the same program takes only a factor of 1.1 more time when run on LuaJIT compared to the native C++ version. This confirms the findings with my Oberon compiler (see https://github.com/rochus-keller/Oberon/blob/master/testcases/Hennessy_Results). 

Meanwhile I was daring and migrated BitBlt to Lua as well (in the previous measurements BitBlt was implemented in C++). The impact on the execution time is amazingly minor. The time to run the first 121k cycles increased by about 30ms, and running 'Benchmark testStandardTests' prolongates from 153 to 157 seconds (geometric mean of ten runs, VM started from command line, without IDE). The speed-down factor grows from 1.08 to 1.11, which still rounds to 1.1.

An analysis with Valgrind revealed that the most expensive functions are mutex locking/unlocking, malloc/free and QImage handling. So I did some optimizations concerning QImage. In commit 1f976e5b5ee3cfe40921c74b97edb8d84ff39dd3 I reorganized the Smalltalk bitmap to QImage conversion which resulted in testStandardTests running time of 142 seconds for the C++ version and 147 seconds for the Lua (including BitBlt) version, corresponding to a speed-down factor of 1.03! In commit 295f7fb5937d88792eff62f74ab235f4cb989ec8 I changed the QImage format from Mono to RGB32 because the latter is used by the Qt drawing pipeline and I thus hoped for a speed-up because of less format conversions; but the running time of the C++ version increased to 144 seconds and of the Lua version to 154 seconds; the resulting speed-down factor from C++ to Lua is 1.07, which is still very good; an explanation for the speed-down could be that bitmaps are copied more often than drawn and copying 8 pixels per byte is faster than 1 pixel per byte. 

After further analysis and tests I was able to achieve a considerable performance increase by giving computing time to Qt only each 30ms; this causes several drawing operations to be combined before the Qt drawing engine can update the display; 30ms is short enough that the interaction still appears smooth. In consequence the execution time of testStandardTests is reduced to 36 seconds for the C++ version and 50 seconds for the Lua version (geometric mean of five runs each). Interestingly the first run with the Lua version is the fastest; all other runs are about 10ms slower than the first one; this requires further investigation. The speed-down factor of the first run is 1.14, whereas the speed-down factor based on the geometric mean of all runs increases to 1.35. 

Since the whole interpreter is written in Lua which runs on LuaJIT the approach could be seen as a "meta-tracing JIT" (i.e. the tracing JIT directly effects the language interpreter, and only indirectly the user program running on it). But there is no speedup yet comparable to e.g. RPython where the JIT version of the VM runs four times faster than the C based interpreter. I will investigate this point further and consider ways to replace the interpreter with a translator from Bluebook directly into LuaJIT bytecode. For now, the approach can be used for representative performance comparisons between LuaJIT and C++ based on a sufficiently complex and realistic application (as opposed to micro benchmarks). 

The VM supports the ALT+V shortcut (but not the other shortcuts supported by the C++ version). 

The VM integrates a Lua IDE with source-level debugger (see https://github.com/rochus-keller/LjTools#lua-parser-and-ide-features); the IDE can be enabled by the -ide or -pro command line option.

Here is a screenshot of the running VM with the Lua IDE/debugger with stack trace and local variables stepping at cycle 121000:

![Screenshot](http://software.rochus-keller.info/smalltalk80_lj_vm_0.5.png)


### Binary versions

Here is a binary version of the class browser, the image viewer and the virtual machines for Windows: http://software.rochus-keller.info/St80Tools_win32.zip.
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



 
