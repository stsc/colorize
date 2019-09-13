#!/usr/bin/perl

use strict;
use warnings;
use lib qw(lib);
use constant true  => 1;
use constant false => 0;

use Colorize::Common qw(:defaults $write_to_tmpfile);
use File::Temp qw(tmpnam);
use IPC::Open3 qw(open3);
use Symbol qw(gensym);
use Test::More;

my $tests = 7;

my $run_program_fail = sub
{
    my ($program, $message, $infile) = @_;

    my $err = gensym;

    my $pid = open3(gensym, gensym, $err, $program, qw(default), $infile);
    waitpid($pid, 0);

    my $output = do { local $/; <$err> };

    return ($? >> 8 == 1 && $output =~ /$message/) ? true : false;
};

plan tests => $tests;

SKIP: {
    my $program = tmpnam();
    my $conf_file = tmpnam();

    skip 'compiling failed (config failure exit)', $tests unless system(qq($compiler -DTEST -DCONF_FILE_TEST=\"$conf_file\" -o $program $source)) == 0;

    my $infile = $write_to_tmpfile->('');

    my @set = (
        [ 'attr=:',                  'attr conf option must be provided a string'                ],
        [ 'attr=bold:underscore',    'attr conf option must have strings separated by ,'         ],
        [ 'attr=b0ld',               'attr conf option attribute \'b0ld\' is not valid'          ],
        [ 'attr=b0ld,underscore',    'attr conf option attribute \'b0ld\' is not valid'          ], # handle comma
        [ 'attr=bold,bold',          'attr conf option has attribute \'bold\' twice or more'     ],
        [ 'exclude-random=random',   'exclude-random conf option must be provided a plain color' ],
        [ 'omit-color-empty=unsure', 'omit-color-empty conf option is not valid'                 ],
    );
    foreach my $set (@set) {
        open(my $fh, '>', $conf_file) or die "Cannot open `$conf_file' for writing: $!\n";
        print {$fh} $set->[0], "\n";
        close($fh);
        ok($run_program_fail->($program, $set->[1], $infile), $set->[1]);
    }

    unlink $program;
    unlink $conf_file;
}
