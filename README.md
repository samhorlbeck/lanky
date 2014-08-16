Lanky
=====

A new interpreted language implemented in C.

## Background
The interpreter uses Flex and Bison to do tokenization and parsing. The grammar is hopefully relatively simple. I borrowed some of the Bison and Flex code from [this tutorial](http://gnuu.org/2009/09/18/writing-your-own-toy-compiler/4/); that project uses C++ with LLVM. My goal is to learn about interpreters and create a marginally useful language with which to play around.

## Compilation
There are two main parts: the `guts` (Flex and Bison) and the `glory` (the AST builder and interpreter). Each can be built from the makefile, and both can be built with `all`. Finally, a `clean` option is included to remove the auto-generated files from Bison and Flex (these should not be in the repo itself) and the object file. A while back I added a stand alone bytecode interpreter (the compiler outputs a binary file named `test` that contains all the information needed by the interpreter). That component can be compiled with `machine`. I need to update make all to include it but at this point I need to completely remake the makefile.

## Usage (subject to extreme change)
Right now, when you startd the program you can enter code. As soon as it hits the end of the line (by typing Control^D, for example) the tree is compiled to Lanky bytecode. In the earliest of early alpha stages, Lanky would walk the tree and interpret what it encountered. After comparing a Lanky loop that counted from 0 to 1,000,000 (printing each value along the way) to a similar loop in Python, I was horrified by how much slower Lanky was performing. Thus I decided to build a bytecode interpreter that emulates a stack machine (much like the CPython and JVM implementations). The VM has only recently (as of today, July 25) been able to perform the same operations as the original tree walking interpreter. Implementation details are in the following section.

## Bytecode Interpreter (Virtual Machine)
As of the most recent version, the VM has a dozen or so opcodes; this number will surely increase as I implement basic language features. Loops and if/elif/else constructs are fully implemented. Each of the builtin Lanky types (strings, doubles, integers, and functions) can all be serialized to a binary representation and reloaded. Right now, when you run the `lanky` program, you are prompted to enter code (or you can supply `lanky` with a file) and `lanky` compiles the code to bytecode and sends it to the VM. Before it is interpreted, however, it is saved to an example binary file (aptly named `test`) which contains everything needed for the VM to run. The structure of that file is also subject to extreme change. If you would like to interpret that binary file directly, use the makefile to build `machine`. This program takes a filename as its first argument and interprets the bytecode in that file. If you use the `-s` switch, it will instead print a human-readable list of opcodes. The interpreter has matured quite a lot in recent commits (let's call it a second grader as opposed to a kindergartner) and can run all of the examples in the `LankyExamples` folder without exception/memory errors. I'm sure, however, there are still plenty of unknown unknowns.

## The state of the memory management
Previous iterations of the Lanky interpreter used reference counting as the primary means of memory management. This was working fine (albeit a touch messy) until I started implementing closures and objects. Due to the nature of closures, strong reference cycles were being created. The same was true for classes, as I dislike Python's use of an added parameter for methods when referencing `self`. As such I recently switched to using a Garbage Collector. It is highly rudimentary (stop-the-world naïve mark and sweep) but it actually performs close enough to the reference counting to be acceptable. I plan to do lots of work on the garbage collector and allocator later, but for now it works and it is modular enough to be easily replaced with a new system in the future. For now I am going to continue maturing Lanky as a language since the memory management is beginning to be *good enough*. I'm sure there are still tons of bugs to work out but for now I am happy with the state of things.
