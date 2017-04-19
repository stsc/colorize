#!/usr/bin/perl

use strict;
use warnings;
use lib qw(lib);
use constant true  => 1;
use constant false => 0;

use Colorize::Common qw(:defaults $compiler_flags %BUF_SIZE $valgrind_command $write_to_tmpfile);
use File::Temp qw(tmpnam);
use Getopt::Long qw(:config no_auto_abbrev no_ignore_case);
use Test::Harness qw(runtests);
use Test::More;

my $tests = 28;

my $valgrind_cmd = '';
{
    my ($regular, $valgrind);
    GetOptions(regular => \$regular, valgrind => \$valgrind) or exit;
    if (not $regular || $valgrind) {
        die "$0: neither --regular nor --valgrind specified, exiting\n";
    }
    elsif ($regular && $valgrind) {
        die "$0: both --regular and --valgrind specified, exiting\n";
    }
    $valgrind_cmd = "$valgrind_command " if $valgrind;
}

{
    my @test_files = glob('t/*.t');
    eval { runtests(@test_files) } or warn $@;
}

plan tests => $tests;

SKIP: {
    skip "$source does not exist", $tests unless -e $source;

    my $binary = tmpnam();
    skip 'compiling failed', $tests unless system("$compiler $compiler_flags -o $binary $source") == 0;
    unlink $binary;

    my $program = tmpnam();
    skip 'compiling failed (normal)', $tests unless system("$compiler -DTEST -DBUF_SIZE=$BUF_SIZE{normal} -o $program $source") == 0;

    is(system("$valgrind_cmd$program --help >/dev/null"),    0, 'exit value for help screen');
    is(system("$valgrind_cmd$program --version >/dev/null"), 0, 'exit value for version data');

    is(qx(printf '%s\n' "hello world" | $valgrind_cmd$program none/none), "hello world\n", 'line read from stdin with newline');
    is(qx(printf  %s    "hello world" | $valgrind_cmd$program none/none), "hello world",   'line read from stdin without newline');

    my $text = do { local $/; <DATA> };

    my $infile1 = $write_to_tmpfile->($text);

    is_deeply([split /\n/, qx(cat $infile1 | $valgrind_cmd$program none/none)], [split /\n/, $text], 'text read from stdin');
    is_deeply([split /\n/, qx($valgrind_cmd$program none/none $infile1)],       [split /\n/, $text], 'text read from file');

    {
        my @fg_colors = (30..37, 39);
        my @bg_colors = (40..47, 49);

        my @bold_colors = map "1;$_", @fg_colors;

        my @values = (@fg_colors, @bg_colors, @bold_colors, 0);

        my $ok = true;
        foreach my $value (@values) {
            $ok &= qx(printf %s "\e[${value}m" | $valgrind_cmd$program --clean) eq '';
        }
        ok($ok, 'clean color sequences');
    }

    my $check_clean = sub
    {
        my ($type) = @_;

        my $switch = "--$type";

        is(qx(printf %s "\e[35mhello\e[0m \e[36mworld\e[0m" | $valgrind_cmd$program $switch),     'hello world', "$type colored words");
        is(qx(printf %s "hello world" | $program Magenta | $valgrind_cmd$program $switch),        'hello world', "$type colored line");
        is_deeply([split /\n/, qx($program cyan $infile1 | $valgrind_cmd$program $switch)], [split /\n/, $text], "$type colored text");

        {
            my @attrs = qw(bold underscore blink reverse concealed);

            my $ok = true;
            foreach my $attr (@attrs) {
                $ok &= qx(printf %s "$attr" | $program green --attr=$attr | $valgrind_cmd$program $switch) eq $attr;
            }
            ok($ok, "$type attribute");

            my $attrs = join ',', @attrs;
            is(qx(printf %s "$attrs" | $program green --attr=$attrs | $valgrind_cmd$program $switch), $attrs, "$type attributes");
        }

        ok(qx(printf %s "\e[\e[33m" | $valgrind_cmd$program $switch) eq "\e[", "$type with invalid sequence");
    };

    $check_clean->($_) foreach qw(clean clean-all);

    is(qx(printf %s "\e[4munderline\e[24m" | $valgrind_cmd$program --clean-all), 'underline', 'clean-all color sequences');

    my $check_clean_buf = sub
    {
        my ($program_buf, $type) = @_;

        my $switch = "--$type";

        # Check that line chunks are printed when cleaning text without sequences
        my $short_text = 'Linux dev 2.6.32-5-openvz-686 #1 SMP Sun Sep 23 11:40:07 UTC 2012 i686 GNU/Linux';
        is(qx(printf %s "$short_text" | $valgrind_cmd$program_buf $switch), $short_text, "print ${\length $short_text} bytes (BUF_SIZE=$BUF_SIZE{short}, $type)");
    };

    SKIP: {
        my $program_buf = tmpnam();
        skip 'compiling failed (short buffer)', 2 unless system("$compiler -DTEST -DBUF_SIZE=$BUF_SIZE{short} -o $program_buf $source") == 0;
        $check_clean_buf->($program_buf, $_) foreach qw(clean clean-all);
        unlink $program_buf;
    }

    my $repeated = join "\n", ($text) x 7;
    my $infile2  = $write_to_tmpfile->($repeated);

    is_deeply([split /\n/, qx(cat $infile2 | $valgrind_cmd$program none/none)], [split /\n/, $repeated], "read ${\length $repeated} bytes (BUF_SIZE=$BUF_SIZE{normal})");

    {
        my $colored_text = qx(printf '%s\n' "foo bar baz" | $valgrind_cmd$program red);
        my $sequences = 0;
        $sequences++ while $colored_text =~ /\e\[\d+m/g;
        is($sequences, 2, 'count of sequences printed');
    }

    is(qx(printf %s "hello\nworld\r\n" | $valgrind_cmd$program none/none), "hello\nworld\r\n", 'stream mode');

    is(system(qq(printf '%s\n' "hello world" | $valgrind_cmd$program random --exclude-random=black >/dev/null)), 0, 'switch exclude-random');

    SKIP: {
        skip 'valgrind not found', 1 unless system('which valgrind >/dev/null 2>&1') == 0;
        like(qx(valgrind $program none/none $infile1 2>&1 >/dev/null), qr/no leaks are possible/, 'valgrind memleaks');
    }

    {
        my $debug = tmpnam();
        is(system("$compiler -DDEBUG -o $debug $source"), 0, 'debugging build');
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

    print <<'EOT';
Attributes
==========
EOT
    foreach my $attr (qw(bold underscore blink reverse concealed)) {
        system(qq(printf '%s\n' "$attr" | $program green --attr=$attr));
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
