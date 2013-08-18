.PHONY: clean

.SUFFIXES:
.SUFFIXES: .c .o

SHELL=/bin/sh
CC=gcc
CFLAGS=-Wall -Wextra -Wformat -Wswitch-default -Wuninitialized -Wunused -Wno-unused-function -Wno-unused-parameter

colorize:	colorize.c
			$(CC) $(CFLAGS) -o colorize colorize.c -DCFLAGS="$(CFLAGS)"

check:
			perl ./test.pl

clean:
			[ -e colorize ] && rm colorize; exit 0
