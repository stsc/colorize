#!/bin/sh
source_file="colorize.c"
man_file="colorize.1"
printf '%s\n' "Setting version for $source_file"
perl -i -pe 's/(?<=#define VERSION ")([^"]+)(?=")/sprintf("%.2f",$1+0.01)/e' $source_file
printf '%s\n' "Setting version for $man_file"
perl -i -pe 's/(?<=\.TH COLORIZE 1 "\d{4}-\d{2}-\d{2}" "colorize v)([^"]+)(?=")/sprintf("%.2f",$1+0.01)/e' $man_file
exit 0
