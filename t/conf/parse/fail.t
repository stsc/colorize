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

my $tests = 8;

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

    skip 'compiling failed (config parse failure)', $tests unless system(qq($compiler -DTEST -DCONF_FILE_TEST=\"$conf_file\" -o $program $source)) == 0;

    my $infile = $write_to_tmpfile->('');

    my $chars_exceed = 'x' x 256;

    my @set = (
        [ '[attr=bold',            'option \'\[attr\' cannot be made of non-option characters' ],
        [ 'attr1=bold',            'option \'attr1\' not recognized'                           ],
        [ 'color1=magenta',        'option \'color1\' not recognized'                          ],
        [ 'exclude-random1=black', 'option \'exclude-random1\' not recognized'                 ],
        [ 'omit-color-empty1=yes', 'option \'omit-color-empty1\' not recognized'               ],
        [ 'attr',                  'option \'attr\' not followed by ='                         ],
        [ 'attr bold',             'option \'attr\' not followed by ='                         ],
        [ "color=$chars_exceed",   'line exceeds maximum of'                                   ],
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
