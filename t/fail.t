#!/usr/bin/perl

use strict;
use warnings;
use lib qw(lib);
use constant true  => 1;
use constant false => 0;

use Colorize::Common qw(:defaults $write_to_tmpfile);
use File::Temp qw(tempdir tmpnam);
use IPC::Open3 qw(open3);
use Symbol qw(gensym);
use Test::More;

my $tests = 20;

my $run_program_fail = sub
{
    my ($program, $args, $message) = @_;

    my @args = split /\s+/, $args;

    my $err = gensym;

    my $pid = open3(gensym, gensym, $err, $program, @args);
    waitpid($pid, 0);

    my $output = do { local $/; <$err> };

    return ($? >> 8 == 1 && $output =~ /$message/) ? true : false;
};

plan tests => $tests;

SKIP: {
    my $program = tmpnam();
    skip 'compiling failed (failure exit)', $tests unless system("$compiler -DTEST -o $program $source") == 0;

    my $file = $write_to_tmpfile->('abc');
    my $dir  = tempdir(CLEANUP => true);

    my @set = (
        [ '--exclude-random=random', 'must be provided a plain color'              ],
        [ '--clean --clean-all',     'mutually exclusive'                          ],
        [ '--clean file1 file2',     'more than one file'                          ],
        [ '--clean-all file1 file2', 'more than one file'                          ],
        [ '- file',                  'hyphen cannot be used as color string'       ],
        [ '-',                       'hyphen must be preceded by color string'     ],
        [ "$file file",              'cannot be used as color string'              ],
        [ "$file",                   'must be preceded by color string'            ],
        [ "$dir",                    'is not a valid file type'                    ],
        [ '/black',                  'foreground color missing'                    ],
        [ 'white/',                  'background color missing'                    ],
        [ 'white/black/yellow',      'one color pair allowed only'                 ],
        [ 'y3llow',                  'cannot be made of non-alphabetic characters' ],
        [ 'yEllow',                  'cannot be in mixed lower/upper case'         ],
        [ 'None',                    'cannot be bold'                              ],
        [ 'white/Black',             'cannot be bold'                              ],
        [ 'random/none',             'cannot be combined with'                     ],
        [ 'random/default',          'cannot be combined with'                     ],
        [ 'none/random',             'cannot be combined with'                     ],
        [ 'default/random',          'cannot be combined with'                     ],
    );
    foreach my $set (@set) {
        ok($run_program_fail->($program, $set->[0], $set->[1]), $set->[1]);
    }

    unlink $program;
}
