.PHONY: check clean

.SUFFIXES:
.SUFFIXES: .c .o

SHELL=/bin/sh
CC=gcc
CFLAGS=-ansi -pedantic

colorize:	colorize.c
			perl ./version.pl > version.h
			$(CC) $(CFLAGS) -o colorize colorize.c -DCFLAGS="$(CFLAGS)" -DHAVE_VERSION

check:
			perl ./test.pl

clean:
			rm -f colorize version.h
