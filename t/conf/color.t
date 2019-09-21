#!/usr/bin/perl

use strict;
use warnings;
use lib qw(lib);

use Colorize::Common qw(:defaults $write_to_tmpfile);
use File::Temp qw(tmpnam);
use Test::More;

my $tests = 4;

plan tests => $tests;

SKIP: {
    my $program = tmpnam();
    my $conf_file = tmpnam();

    skip 'compiling failed (color)', $tests unless system(qq($compiler -DTEST -DCONF_FILE_TEST=\"$conf_file\" -o $program $source)) == 0;

    my $infile = $write_to_tmpfile->('foo');

    open(my $fh, '>', $conf_file) or die "Cannot open `$conf_file' for writing: $!\n";
    print {$fh} "color=green\n";
    close($fh);

    is(qx(printf %s "foo" | $program -), "\e[32mfoo\e[0m", 'color from config (stdin)');
    is(qx($program $infile), "\e[32mfoo\e[0m", 'color from config (file)');
    is(qx(printf %s "foo" | $program none/none), 'foo', 'read from stdin');
    is(qx($program none/none $infile), 'foo', 'read from file');

    unlink $program;
    unlink $conf_file;
}
