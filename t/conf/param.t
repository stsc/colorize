#!/usr/bin/perl

use strict;
use warnings;
use lib qw(lib);

use Colorize::Common qw(:defaults $write_to_tmpfile);
use File::Temp qw(tmpnam);
use Test::More;

my $tests = 2;

my $conf = <<'EOT';
attr=bold
color=blue
omit-color-empty=yes
EOT

my $expected = <<"EOT";
\e[1;34mfoo\e[0m

\e[1;34mbar\e[0m

\e[1;34mbaz\e[0m
EOT

plan tests => $tests;

SKIP: {
    my $program = tmpnam();
    my $conf_file = tmpnam();
                                                                    # -DTEST omitted on purpose
    skip 'compiling failed (config param)', $tests unless system(qq($compiler -o $program $source)) == 0;

    my $infile = $write_to_tmpfile->(<<'EOT');
foo

bar

baz
EOT
    open(my $fh, '>', $conf_file) or die "Cannot open `$conf_file' for writing: $!\n";
    print {$fh} $conf;
    close($fh);

    is(qx($program -c $conf_file $infile),       $expected, 'short option');
    is(qx($program --config=$conf_file $infile), $expected, 'long option');

    unlink $program;
    unlink $conf_file;
}
