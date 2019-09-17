#!/usr/bin/perl

use strict;
use warnings;
use lib qw(lib);

use Colorize::Common qw(:defaults $write_to_tmpfile);
use File::Temp qw(tmpnam);
use Test::More;

my $tests = 1;

my $conf = <<'EOT';
attr=reverse
color=red
EOT

plan tests => $tests;

SKIP: {
    my $program = tmpnam();
    my $conf_file = tmpnam();

    skip 'compiling failed (endings)', $tests unless system(qq($compiler -DTEST -DCONF_FILE_TEST=\"$conf_file\" -o $program $source)) == 0;

    my $infile = $write_to_tmpfile->('foo');

    open(my $fh, '>', $conf_file) or die "Cannot open `$conf_file' for writing: $!\n";
    print {$fh} join "\015\012", split /\n/, $conf;
    close($fh);

    is(qx($program $infile), "\e[7;31mfoo\e[0m", 'CRLF line endings');

    unlink $program;
    unlink $conf_file;
}
