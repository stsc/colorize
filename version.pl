#!/usr/bin/perl

use strict;
use warnings;

my $version = '';

# git repository
if (system('which git >/dev/null 2>&1') == 0
and system('git ls-files colorize.c --error-unmatch >/dev/null 2>&1') == 0) {
    $version = `git describe --tags --dirty`;
    $version =~ s/\n$//g;
}

if (length $version) {
    print <<"EOT";
const char *const version = "$version";
EOT
}
else {
    print <<'EOT';
const char *const version = NULL;
EOT
}
