.PHONY: check check_valgrind install clean release readme

.SUFFIXES:
.SUFFIXES: .c .o

SHELL=/bin/sh
CC=gcc
CFLAGS:=-ansi -pedantic $(CFLAGS)
FLAGS= # command-line macro

colorize:	colorize.c
			perl ./version.pl > version.h
			$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o colorize colorize.c \
  -DCPPFLAGS="\"$(CPPFLAGS)\"" -DCFLAGS="\"$(CFLAGS)\"" -DLDFLAGS="\"$(LDFLAGS)\"" \
  -DHAVE_VERSION $(FLAGS)

check:
			perl ./test.pl --regular

check_valgrind:
			@which valgrind >/dev/null 2>&1 || (printf '%s\n' "valgrind not found" && exit 1)
			perl ./test.pl --valgrind || exit 0

install:
			test ! -d $(DESTDIR)/usr/bin && mkdir -p $(DESTDIR)/usr/bin || exit 0
			cp colorize $(DESTDIR)/usr/bin

clean:
			rm -f a.out colorize debug.txt version.h

release:
			sh ./release.sh

readme:
			perl ./readme.pl
