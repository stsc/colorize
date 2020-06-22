#!/usr/bin/perl

use strict;
use warnings;

my $readme      = 'README';
my $readme_cgit = 'README.cgit';

die "$0: $readme does not exist\n" unless -e $readme;

open(my $fh, '<', $readme) or die "Cannot open $readme for reading: $!\n";
my $text = do { local $/; <$fh> };
close($fh);

$text = do {
    local $_ = $text;
    s/</&lt;/g;
    s/>/&gt;/g;
    $_
};

print "Writing $readme_cgit\n";

open($fh, '>', $readme_cgit) or die "Cannot open $readme_cgit for writing: $!\n";
print {$fh} <<"CGIT";
<pre>
$text
</pre>
CGIT
close($fh);
