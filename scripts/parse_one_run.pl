#!/usr/bin/perl 
use strict;
use warnings;

my $fn = $ARGV[0];
my $proto = defined $ARGV[1]?$ARGV[1]:'proto';

my %results= ();

sub process {
	my ($fn, $nc, $reqs, $reps) = @_;
	open FIN, '<', $fn or die "Can't open $fn: $!";
	while (<FIN>) {
		next unless /(?:throughput)|(?:response time)/;
		if (/throughput :.*avg =\s+([\d.]+)\s+std dev =\s*((?:[\d.]+)|(?:inf))\s/) {
			$results{TH} = [$1, $2 eq 'inf'?0:$2];
		} elsif (/response time: avg =\s+([\d.e\-\+]+)\s+std dev =\s*((?:[\d.e\-\+]+)|(?:inf))\s/) {
			$results{RT} = [$1, $2];
		}
	}
	close FIN;
}

die "Can't parse filename\n" unless ($fn=~m/manager.out-(\d+)-(\d+)-(\d+)/);
my($nc,$reqs,$reps) = ($1,$2,$3);

print STDERR "Processing: $fn\n";
process($fn, $1, $2, $3);

print STDERR "OUTPUT:\n";
print STDOUT join "\t", ($proto, $reqs, $reps, $nc, 'c', @{$results{TH}}, @{$results{RT}});
print STDOUT "\n";
