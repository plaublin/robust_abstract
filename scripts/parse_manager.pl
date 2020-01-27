#!/usr/bin/perl 
use strict;
use warnings;

use File::Find;

my %results= ();

sub process {
	my ($fn, $nc, $reqs, $reps) = @_;
	open FIN, '<', $fn or die;
	while (<FIN>) {
		next unless /(?:throughput)|(?:response time)/;
		if (/throughput :.*avg =\s+([\d.]+)\s+std dev =\s*([\d.]+)\s/) {
			$results{TH}->{$reqs}->{$reps}->{$nc} = [$1, $2];
		} elsif (/response time: avg =\s+([\d.e\-\+]+)\s+std dev =\s*([\d.e\-\+]+)\s/) {
			$results{RT}->{$reqs}->{$reps}->{$nc} = [$1, $2];
		}
	}
	close FIN;
}

sub wanted {
	return unless /^manager.out-(\d+)-(\d+)-(\d+)/;

	print STDERR "Processing: $_\n";
	process($File::Find::name, $1, $2, $3);
}

find(\&wanted, '/tmp/');

foreach my $reqs (sort {$a<=>$b} keys %{$results{TH}}) {
	foreach my $reps (sort {$a<=>$b} keys %{$results{TH}->{$reqs}}) {
		foreach my $nc (sort {$a<=>$b} keys %{$results{TH}->{$reqs}->{$reps}}) {
			printf "# [%s %s %s]\n", $reqs, $reps, $nc;
		}
	}
}

foreach my $reqs (sort {$a<=>$b} keys %{$results{TH}}) {
	foreach my $reps (sort {$a<=>$b} keys %{$results{TH}->{$reqs}}) {
		foreach my $nc (sort {$a<=>$b} keys %{$results{TH}->{$reqs}->{$reps}}) {
			my @a = @{$results{TH}->{$reqs}->{$reps}->{$nc}};
			my @b = @{$results{RT}->{$reqs}->{$reps}->{$nc}};

			print "$nc c\t\t";
			print join "\t", @a, @b;
			print "\n";

			#printf "%s\t%s\n", , $results{RT}->{$reqs}->{$reps}->{$nc};
			#printf "%d/%d %4d [TH]: %s\n", $reqs, $reps, $nc, $results{TH}->{$reqs}->{$reps}->{$nc};
			#printf "%d/%d %4d [RT]: %s\n", $reqs, $reps, $nc, $results{RT}->{$reqs}->{$reps}->{$nc};
		}
	}
}
