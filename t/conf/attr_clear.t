#!/usr/bin/perl

use strict;
use warnings;
use lib qw(lib);

use Colorize::Common qw(:defaults $write_to_tmpfile);
use File::Temp qw(tmpnam);
use Test::More;

my $tests = 1;

plan tests => $tests;

SKIP: {
    my $program = tmpnam();
    my $conf_file = tmpnam();

    skip 'compiling failed (attr clear)', $tests unless system(qq($compiler -DTEST -DCONF_FILE_TEST=\"$conf_file\" -o $program $source)) == 0;

    my $infile = $write_to_tmpfile->('foo');

    open(my $fh, '>', $conf_file) or die "Cannot open `$conf_file' for writing: $!\n";
    print {$fh} "attr=blink\n";
    close($fh);

    is(qx($program default --attr=bold $infile), "\e[1;39mfoo\e[0m", 'discard conf attr string');

    unlink $program;
    unlink $conf_file;
}
