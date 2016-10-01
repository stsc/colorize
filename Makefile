.PHONY: check check_valgrind clean release

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
			perl ./test.pl --regular

check_valgrind:
			@which valgrind >/dev/null 2>&1 || (printf '%s\n' "valgrind not found" && exit 1)
			perl ./test.pl --valgrind || exit 0

clean:
			rm -f a.out colorize debug.txt version.h

release:
			sh ./release.sh
