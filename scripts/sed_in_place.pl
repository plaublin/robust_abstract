#!/usr/bin/perl
#
# Set the value of the macro $ARGV[0] to $ARGV[1]

use strict;
use warnings;
use Data::Dumper;

# The file on which we apply the modification
my $file = "ring_options.conf";


#print Dumper(\@ARGV);
die("Usage: $0 <MACRO> <VALUE>\n") if (!defined($ARGV[1]));


# 1. Open the file 
open(RFILE, "$file") or die "PATCH : can't open $file for reading\n";
my @lines = <RFILE>;


# 2. Modify the line
my $newline = "";
my $i = 0;
for my $line (@lines) {
  $newline = $line;
  $newline =~ s/($ARGV[0])(=)(.*)/$1$2$ARGV[1]/;
  $lines[$i] = $newline;
  $i++;
}
close(RFILE);


# 3. Delete the old file and write the new one
system("rm $file");
open(WFILE, ">$file") or die "Can't open $file for writing\n";
print(WFILE @lines);
close(WFILE);
