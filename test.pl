#!/usr/bin/perl

use strict;
use warnings;
use constant true => 1;

use File::Temp qw(tempfile tmpnam);
use Test::More tests => 9;

my $BUF_SIZE = 1024;
my $source = 'colorize.c';
my $warning_flags = '-Wall -Wextra -Wformat -Wswitch-default -Wuninitialized -Wunused';

my $write_to_tmpfile = sub
{
    my ($content) = @_;

    my ($fh, $tmpfile) = tempfile(UNLINK => true);
    print {$fh} $content;
    close($fh);

    return $tmpfile;
};

my $program;

SKIP: {
    skip "$source does not exist", 9 unless -e $source;

    $program = tmpnam();

    skip 'compiling failed', 9 unless system("gcc -DTEST -DBUF_SIZE=$BUF_SIZE $warning_flags -o $program $source") == 0;

    is(system("$program --help >/dev/null 2>&1"), 0, 'exit value for help screen');

    is(qx(echo    "hello world" | $program none/none), "hello world\n", 'line read from stdin with newline');
    is(qx(echo -n "hello world" | $program none/none), "hello world",   'line read from stdin without newline');

    my $text = do { local $/; <DATA> };

    my $infile1 = $write_to_tmpfile->($text);

    is_deeply([split /\n/, qx(cat $infile1 | $program none/none)], [split /\n/, $text], 'text read from stdin');
    is_deeply([split /\n/, qx($program none/none $infile1)],       [split /\n/, $text], 'text read from file');

    my $repeated = join "\n", ($text) x 7;
    my $infile2  = $write_to_tmpfile->($repeated);

    is_deeply([split /\n/, qx(cat $infile2 | $program none/none)], [split /\n/, $repeated], "read ${\length $repeated} bytes (BUF_SIZE=$BUF_SIZE)");

    is(qx(echo -n "hello\nworld\r\n" | $program none/none), "hello\nworld\r\n", 'stream mode');

    is(system("echo \"hello world\" | $program random --exclude-random=black >/dev/null 2>&1"), 0, 'switch exclude-random');

    skip 'valgrind not found', 1 unless system('which valgrind >/dev/null 2>&1') == 0;
    like(qx(valgrind $program none/none $infile1 2>&1 >/dev/null), qr/no leaks are possible/, 'valgrind memleaks');

    print <<'EOT';
Colors
======
EOT
    foreach my $color (qw(none black red green yellow blue cyan magenta white default random)) {
        system("echo $color | $program $color");
        next if $color eq 'none';
        my $bold_color = ucfirst $color;
        system("echo $bold_color | $program $bold_color");
    }
};

unlink $program if defined $program;

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
