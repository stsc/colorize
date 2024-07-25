#!/usr/bin/perl

use strict;
use warnings;
use lib qw(lib);

use Colorize::Common qw(:defaults $write_to_tmpfile);
use File::Temp qw(tmpnam);
use Test::More;

my $tests = 21;

my $conf = <<'EOT';
attr=underscore
color=yellow # tested also in color.t
omit-color-empty=yes
rainbow-fg=yes
EOT

plan tests => $tests;

SKIP: {
    my $program = tmpnam();
    my $conf_file = tmpnam();

    skip 'compiling failed (use config)', $tests unless system(qq($compiler -DTEST -DCONF_FILE_TEST=\"$conf_file\" -o $program $source)) == 0;

    my $infile1 = $write_to_tmpfile->(<<'EOT');
foo

bar

baz
EOT
    open(my $fh, '>', $conf_file) or die "Cannot open `$conf_file' for writing: $!\n";
    print {$fh} $conf;
    close($fh);

    is(qx($program $infile1), <<"EOT", 'use config');
\e[4;33mfoo\e[0m

\e[4;34mbar\e[0m

\e[4;35mbaz\e[0m
EOT
    my $infile2 = $write_to_tmpfile->('foo');

    open($fh, '>', $conf_file) or die "Cannot open `$conf_file' for writing: $!\n";
    print {$fh} "exclude-random=black\n";
    close($fh);

    for (my $i = 1; $i <= 20; $i++) {
        like(qx($program random $infile2), qr/^\e\[3[1-7]mfoo\e\[0m$/, 'use exclude-random');
    }

    unlink $program;
    unlink $conf_file;
}
