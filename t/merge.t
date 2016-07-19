#!/usr/bin/perl

use strict;
use warnings;
use lib qw(lib);
use constant true  => 1;
use constant false => 0;

use Colorize::Common ':defaults';
use File::Temp qw(tmpnam);
use Test::More;

# sequence, buffer sizes
my @merge_success = (
    [ "\e[30m",     [ 1..4 ] ],
    [ "\e[31m",     [ 1..4 ] ],
    [ "\e[32m",     [ 1..4 ] ],
    [ "\e[33m",     [ 1..4 ] ],
    [ "\e[34m",     [ 1..4 ] ],
    [ "\e[35m",     [ 1..4 ] ],
    [ "\e[36m",     [ 1..4 ] ],
    [ "\e[37m",     [ 1..4 ] ],
    [ "\e[39m",     [ 1..4 ] ],
    [ "\e[1;30m",   [ 1..6 ] ],
    [ "\e[1;31m",   [ 1..6 ] ],
    [ "\e[1;32m",   [ 1..6 ] ],
    [ "\e[1;33m",   [ 1..6 ] ],
    [ "\e[1;34m",   [ 1..6 ] ],
    [ "\e[1;35m",   [ 1..6 ] ],
    [ "\e[1;36m",   [ 1..6 ] ],
    [ "\e[1;37m",   [ 1..6 ] ],
    [ "\e[1;39m",   [ 1..6 ] ],
    [ "\e[40m",     [ 1..4 ] ],
    [ "\e[41m",     [ 1..4 ] ],
    [ "\e[42m",     [ 1..4 ] ],
    [ "\e[43m",     [ 1..4 ] ],
    [ "\e[44m",     [ 1..4 ] ],
    [ "\e[45m",     [ 1..4 ] ],
    [ "\e[46m",     [ 1..4 ] ],
    [ "\e[47m",     [ 1..4 ] ],
    [ "\e[49m",     [ 1..4 ] ],
    [ "\e[0m",      [ 1..3 ] ],
    [ "\e[m",       [ 1..2 ] ],
    [ "\e[;;m",     [ 1..4 ] ],
    [ "\e[123456m", [ 1    ] ], # tightly coupled to ALLOC_COMPLETE_PART_LINE
);
# sequence, buffer size
my @merge_fail = (
    [ "\e30m", 1 ], # missing bracket
    [ "\e[am", 2 ], # not a digit nor ; nor m
);
# sequence
my @buffer = (
    "\e[30mz",
    "\e[31mz",
    "\e[32mz",
    "\e[33mz",
    "\e[34mz",
    "\e[35mz",
    "\e[36mz",
    "\e[37mz",
    "\e[39mz",
    "\e[1;30mz",
    "\e[1;31mz",
    "\e[1;32mz",
    "\e[1;33mz",
    "\e[1;34mz",
    "\e[1;35mz",
    "\e[1;36mz",
    "\e[1;37mz",
    "\e[1;39mz",
    "\e[40mz",
    "\e[41mz",
    "\e[42mz",
    "\e[43mz",
    "\e[44mz",
    "\e[45mz",
    "\e[46mz",
    "\e[47mz",
    "\e[49mz",
    "\e[0mz",
    "\e[mz",
    "\e[;;mz",
);
# sequence, buffer size
my @pushback = (
    [ "\ezm", 1 ],
    [ "\e[z", 2 ],
);

my $tests = 0;
foreach (@merge_success) {
    $tests += @{$_->[1]};
}
$tests += @merge_fail;
$tests += @buffer;
$tests += @pushback;

my %programs;

my $compile = sub
{
    my ($buf_size) = @_;
    return true if exists $programs{$buf_size};
    my $program = tmpnam();
    return false unless system("$compiler -DTEST_MERGE_PART_LINE -DBUF_SIZE=$buf_size -o $program $source") == 0;
    $programs{$buf_size} = $program;
    return true; # compiling succeeded
};

my $test_name = sub
{
    my ($sequence, $buf_size) = @_;
    my $substr = substr($sequence, 0, $buf_size);
    $substr   =~ s/^\e/ESC/;
    $sequence =~ s/^\e/ESC/;
    return "$sequence: $substr";
};

my $compiling_failed_msg = 'compiling failed (merge part line)';

plan tests => $tests;

foreach my $test (@merge_success) {
    foreach my $buf_size (@{$test->[1]}) {
        SKIP: {
            skip $compiling_failed_msg, 1 unless $compile->($buf_size);
            ok(qx(printf %s "$test->[0]" | $programs{$buf_size} --clean) eq $test->[0], 'merge success: ' . $test_name->($test->[0], $buf_size));
        }
    }
}
foreach my $test (@merge_fail) {
    my $buf_size = $test->[1];
    SKIP: {
        skip $compiling_failed_msg, 1 unless $compile->($buf_size);
        ok(qx(printf %s "$test->[0]" | $programs{$buf_size} --clean) eq substr($test->[0], 0, $buf_size), 'merge fail: ' . $test_name->($test->[0], $buf_size));
    }
}
foreach my $test (@buffer) {
    my $buf_size = length($test) - 1;
    SKIP: {
        skip $compiling_failed_msg, 1 unless $compile->($buf_size);
        ok(qx(printf %s "$test" | $programs{$buf_size} --clean) eq substr($test, 0, $buf_size), 'buffer: ' . $test_name->($test, $buf_size));
    }
}
foreach my $test (@pushback) {
    my $buf_size = $test->[1];
    SKIP: {
        my $program = tmpnam();
        skip $compiling_failed_msg, 1 unless system("$compiler -DBUF_SIZE=$buf_size -o $program $source") == 0;
        ok(qx(printf %s "$test->[0]" | $program --clean) eq $test->[0], 'pushback: ' . $test_name->($test->[0], $buf_size));
        unlink $program;
    }
}

unlink $programs{$_} foreach keys %programs;
