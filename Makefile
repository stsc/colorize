.PHONY: clean

.SUFFIXES:
.SUFFIXES: .c .o

SHELL=/bin/sh
CC=gcc
CFLAGS=-Wall -Wextra -Wformat -Wswitch-default -Wuninitialized -Wunused

colorize:	colorize.o
			$(CC) -o $@ $<

colorize.o:	colorize.c
			$(CC) $(CFLAGS) -c $< -DCFLAGS="$(CFLAGS)"

check:
			perl ./test.pl

clean:
			[ -e colorize.o ] && rm colorize.o; exit 0
