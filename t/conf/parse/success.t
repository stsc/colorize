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

my $tests = 23;

my $conf = <<'EOT';
# comment
 # comment
	# comment
attr=bold
attr =bold
attr= bold
attr	=bold
attr=	bold
attr = bold
 color=green
color=green 
	color=green
color=green	
exclude-random=black
omit-color-empty=yes
rainbow-fg=no
attr=bold # comment
attr=bold	# comment
attr=
color=
exclude-random=
omit-color-empty=
rainbow-fg=
EOT

my $run_program_succeed = sub
{
    my ($program, $infile) = @_;

    my $err = gensym;

    my $pid = open3(gensym, gensym, $err, $program, qw(default), $infile);
    waitpid($pid, 0);

    my $output = do { local $/; <$err> };

    return ($? >> 8 == 0 && $output eq '') ? true : false;
};

plan tests => $tests;

SKIP: {
    my $program = tmpnam();
    my $conf_file = tmpnam();

    skip 'compiling failed (config parse success)', $tests unless system(qq($compiler -DTEST -DCONF_FILE_TEST=\"$conf_file\" -o $program $source)) == 0;

    my $infile = $write_to_tmpfile->('');

    foreach my $line (split /\n/, $conf) {
        open(my $fh, '>', $conf_file) or die "Cannot open `$conf_file' for writing: $!\n";
        print {$fh} $line, "\n";
        close($fh);
        ok($run_program_succeed->($program, $infile), $line);
    }

    unlink $program;
    unlink $conf_file;
}
