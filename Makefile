.PHONY: check clean

.SUFFIXES:
.SUFFIXES: .c .o

SHELL=/bin/sh
CC=gcc
CFLAGS=-ansi -pedantic
FLAGS= # command-line macro

colorize:	colorize.c
			perl ./version.pl > version.h
			$(CC) $(CFLAGS) -o colorize colorize.c -DCFLAGS="$(CFLAGS)" -DHAVE_VERSION $(FLAGS)

check:
			perl ./test.pl

clean:
			rm -f a.out colorize debug.txt version.h
