.PHONY: check clean

.SUFFIXES:
.SUFFIXES: .c .o

SHELL=/bin/sh
CC=gcc
CFLAGS=-ansi -pedantic

colorize:	colorize.c
			$(CC) $(CFLAGS) -o colorize colorize.c -DCFLAGS="$(CFLAGS)"

check:
			perl ./test.pl

clean:
			[ -e colorize ] && rm colorize; exit 0
