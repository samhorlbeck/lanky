SOURCES=main.c parser.c tokens.c tools.c ast.c mempool.c hashmap.c ast_compiler.c lky_object.c arraylist.c lky_machine.c lkyobj_builtin.c
CFLAGS=-lm

guts: lanky.l lanky.y
	bison -d -o parser.c lanky.y
	lex -o tokens.c lanky.l

glory: $(SOURCES)
	gcc -o lanky $(SOURCES) $(CFLAGS) -g

all: guts glory

clean:
	rm lanky bnm machine arrtests parser.c parser.h tokens.c

arraytests: arrtests.c arraylist.c
	gcc -o arrtests arrtests.c arraylist.c -g

bnm: binary_file_maker.c
	gcc -o bnm binary_file_maker.c lky_object.c lkyobj_builtin.c arraylist.c

machine: lky_machine.c lky_object.c lkyobj_builtin.c arraylist.c bin_main.c
	gcc -o machine lky_machine.c arraylist.c lky_object.c lkyobj_builtin.c bin_main.c -g