#!/usr/bin/perl

use strict;
use warnings;
use constant true  => 1;
use constant false => 0;

use File::Temp qw(tempfile tempdir tmpnam);
use IPC::Open3 qw(open3);
use Symbol qw(gensym);
use Test::Harness qw(runtests);
use Test::More;

my $tests = 25;

my %BUF_SIZE = (
   normal => 1024,
   short  => 10,
);
my $source = 'colorize.c';
my $compiler_flags = '-ansi -pedantic -Wall -Wextra -Wformat -Wswitch-default -Wuninitialized -Wunused -Wno-unused-function -Wno-unused-parameter';

my $write_to_tmpfile = sub
{
    my ($content) = @_;

    my ($fh, $tmpfile) = tempfile(UNLINK => true);
    print {$fh} $content;
    close($fh);

    return $tmpfile;
};

{
    my @test_files = glob('t/*.t');
    eval { runtests(@test_files) } or warn $@;
}

plan tests => $tests;

SKIP: {
    skip "$source does not exist", $tests unless -e $source;

    my $binary = tmpnam();
    skip 'compiling failed', $tests unless system("gcc $compiler_flags -o $binary $source") == 0;
    unlink $binary;

    my $program = tmpnam();
    skip 'compiling failed (normal)', $tests unless system("gcc -DTEST -DBUF_SIZE=$BUF_SIZE{normal} -o $program $source") == 0;

    is(system("$program --help >/dev/null 2>&1"),    0, 'exit value for help screen');
    is(system("$program --version >/dev/null 2>&1"), 0, 'exit value for version data');

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

    {
        my $ok = true;

        my $file = $write_to_tmpfile->('abc');
        my $dir  = tempdir(CLEANUP => true);

        $ok &= $run_program_fail->($program, '--exclude-random=random', 'must be provided a plain color');
        $ok &= $run_program_fail->($program, '--clean --clean-all',     'mutually exclusive');
        $ok &= $run_program_fail->($program, '--clean file1 file2',     'more than one file');
        $ok &= $run_program_fail->($program, '--clean-all file1 file2', 'more than one file');
        $ok &= $run_program_fail->($program, '- file',                  'hyphen cannot be used as color string');
        $ok &= $run_program_fail->($program, '-',                       'hyphen must be preceeded by color string');
        $ok &= $run_program_fail->($program, "$file file",              'cannot be used as color string');
        $ok &= $run_program_fail->($program, "$file",                   'must be preceeded by color string');
        $ok &= $run_program_fail->($program, "$dir",                    'is not a valid file type');
        $ok &= $run_program_fail->($program, '/black',                  'foreground color missing');
        $ok &= $run_program_fail->($program, 'white/',                  'background color missing');
        $ok &= $run_program_fail->($program, 'white/black/yellow',      'one color pair allowed only');
        $ok &= $run_program_fail->($program, 'y3llow',                  'cannot be made of non-alphabetic characters');
        $ok &= $run_program_fail->($program, 'yEllow',                  'cannot be in mixed lower/upper case');
        $ok &= $run_program_fail->($program, 'None',                    'cannot be bold');
        $ok &= $run_program_fail->($program, 'white/Black',             'cannot be bold');

        foreach my $color_pair (qw(random/none random/default none/random default/random)) {
            $ok &= $run_program_fail->($program, $color_pair, 'cannot be combined with');
        }

        ok($ok, 'exit messages/values for failures');
    }

    is(qx(printf '%s\n' "hello world" | $program none/none), "hello world\n", 'line read from stdin with newline');
    is(qx(printf  %s    "hello world" | $program none/none), "hello world",   'line read from stdin without newline');

    my $text = do { local $/; <DATA> };

    my $infile1 = $write_to_tmpfile->($text);

    is_deeply([split /\n/, qx(cat $infile1 | $program none/none)], [split /\n/, $text], 'text read from stdin');
    is_deeply([split /\n/, qx($program none/none $infile1)],       [split /\n/, $text], 'text read from file');

    {
        my @fg_colors = (30..37, 39);
        my @bg_colors = (40..47, 49);

        my @bold_colors = map "1;$_", @fg_colors;

        my @values = (@fg_colors, @bg_colors, @bold_colors, 0);

        my $ok = true;
        foreach my $value (@values) {
            $ok &= qx(printf %s "\e[${value}m" | $program --clean) eq '';
        }
        ok($ok, 'clean color sequences');
    }

    my $check_clean = sub
    {
        my ($type) = @_;

        my $switch = "--$type";

        is(qx(printf %s "\e[35mhello\e[0m \e[36mworld\e[0m" | $program $switch),     'hello world', "$type colored words");
        is(qx(printf %s "hello world" | $program Magenta | $program $switch),        'hello world', "$type colored line");
        is_deeply([split /\n/, qx($program cyan $infile1 | $program $switch)], [split /\n/, $text], "$type colored text");

        ok(qx(printf %s "\e[\e[33m" | $program $switch) eq "\e[", "$type with invalid sequence");
    };

    $check_clean->($_) foreach qw(clean clean-all);

    is(qx(printf %s "\e[4munderline\e[24m" | $program --clean-all), 'underline', 'clean-all color sequences');

    my $check_clean_buf = sub
    {
        my ($program_buf, $type) = @_;

        my $switch = "--$type";

        # Check that line chunks are printed when cleaning text without sequences
        my $short_text = 'Linux dev 2.6.32-5-openvz-686 #1 SMP Sun Sep 23 11:40:07 UTC 2012 i686 GNU/Linux';
        is(qx(printf %s "$short_text" | $program_buf $switch), $short_text, "print ${\length $short_text} bytes (BUF_SIZE=$BUF_SIZE{short}, $type)");
    };

    SKIP: {
        my $program_buf = tmpnam();
        skip 'compiling failed (short buffer)', 2 unless system("gcc -DTEST -DBUF_SIZE=$BUF_SIZE{short} -o $program_buf $source") == 0;
        $check_clean_buf->($program_buf, $_) foreach qw(clean clean-all);
        unlink $program_buf;
    }

    my $repeated = join "\n", ($text) x 7;
    my $infile2  = $write_to_tmpfile->($repeated);

    is_deeply([split /\n/, qx(cat $infile2 | $program none/none)], [split /\n/, $repeated], "read ${\length $repeated} bytes (BUF_SIZE=$BUF_SIZE{normal})");

    {
        my $colored_text = qx(printf '%s\n' "foo bar baz" | $program red);
        my $sequences = 0;
        $sequences++ while $colored_text =~ /\e\[\d+m/g;
        is($sequences, 2, 'count of sequences printed');
    }

    is(qx(printf %s "hello\nworld\r\n" | $program none/none), "hello\nworld\r\n", 'stream mode');

    is(system(qq(printf '%s\n' "hello world" | $program random --exclude-random=black >/dev/null 2>&1)), 0, 'switch exclude-random');

    SKIP: {
        skip 'valgrind not found', 1 unless system('which valgrind >/dev/null 2>&1') == 0;
        like(qx(valgrind $program none/none $infile1 2>&1 >/dev/null), qr/no leaks are possible/, 'valgrind memleaks');
    }

    {
        my $debug = tmpnam();
        is(system("gcc -DDEBUG -o $debug $source"), 0, 'debugging build');
        unlink $debug if -e $debug;
    }

    print <<'EOT';
Colors
======
EOT
    foreach my $color (qw(none black red green yellow blue magenta cyan white default random)) {
        system(qq(printf '%s\n' "$color" | $program $color));
        next if $color eq 'none';
        my $bold_color = ucfirst $color;
        system(qq(printf '%s\n' "$bold_color" | $program $bold_color));
    }

    unlink $program;
};

__DATA__
Lorem ipsum dolor sit amet, consectetur adipiscing elit. Vivamus urna mauris, ultricies faucibus placerat sit amet, rutrum eu
nisi. Quisque dictum turpis non augue iaculis tincidunt nec a arcu. Donec euismod sapien ac dui blandit et adipiscing risus
semper. Sed ornare ligula magna, vitae molestie eros. Praesent ligula est, euismod a luctus non, porttitor quis nunc. Fusce vel
imperdiet turpis. Proin vitae mauris neque, fringilla vestibulum sapien. Pellentesque vitae nibh ipsum, non cursus diam. Cras
vitae ligula mauris. Etiam tortor enim, varius nec adipiscing sed, lobortis et quam. Quisque convallis, diam sagittis adipiscing
adipiscing, mi nibh fermentum sapien, et iaculis nisi sem sit amet odio. Cras a tortor at nibh tristique vehicula dapibus eu velit.

Vivamus porttitor purus eget leo suscipit sed posuere ligula gravida. In mollis velit quis leo pharetra gravida. Ut libero nisi,
elementum sed varius tincidunt, hendrerit ut dui. Duis sit amet ante eget velit dictum ultrices. Nulla tempus, lacus eu dignissim
feugiat, turpis mauris volutpat urna, quis commodo lorem augue id justo. Aenean consequat interdum sapien, sit amet
imperdiet ante dapibus at. Pellentesque viverra sagittis tincidunt. Quisque rhoncus varius magna, sit amet rutrum arcu
tincidunt eget. Etiam a lacus nec mauris interdum luctus sed in lacus. Ut pulvinar, augue at dictum blandit, nisl massa pretium
ligula, in iaculis nulla nisi iaculis nunc.

Vivamus id eros nunc. Cras facilisis iaculis ante sit amet consequat. Nunc vehicula imperdiet sem, ac vehicula neque
condimentum sed. Phasellus metus lacus, molestie ullamcorper imperdiet in, condimentum ut tellus. Nullam dignissim dui ut
enim ullamcorper in tempus risus posuere. Ut volutpat enim eleifend diam convallis tristique. Proin porttitor augue sed sapien
sagittis quis facilisis purus sodales. Integer auctor dolor rhoncus nisl consequat adipiscing. Aliquam eget ante sit amet quam
porta eleifend.
