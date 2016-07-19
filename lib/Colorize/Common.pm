package Colorize::Common;

use strict;
use warnings;
use base qw(Exporter);
use constant true => 1;

use File::Temp qw(tempfile);

our (@EXPORT_OK, %EXPORT_TAGS);
my @defaults;

@defaults    = qw($source $compiler);
@EXPORT_OK   = (qw($compiler_flags %BUF_SIZE $write_to_tmpfile), @defaults);
%EXPORT_TAGS = ('defaults' => [ @defaults ]);

our ($source, $compiler, $compiler_flags, %BUF_SIZE, $write_to_tmpfile);

#---------------#
# START of data #
#---------------#

$source = 'colorize.c';
$compiler = 'gcc';
$compiler_flags = '-ansi -pedantic -Wall -Wextra -Wformat -Wswitch-default -Wuninitialized -Wunused -Wno-unused-function -Wno-unused-parameter';
%BUF_SIZE = (
    normal => 1024,
    short  => 10,
);
$write_to_tmpfile = sub
{
    my ($content) = @_;

    my ($fh, $tmpfile) = tempfile(UNLINK => true);
    print {$fh} $content;
    close($fh);

    return $tmpfile;
};

#-------------#
# END of data #
#-------------#

1;
