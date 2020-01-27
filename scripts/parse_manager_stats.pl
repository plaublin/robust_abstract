#!/usr/bin/perl 
use strict;
use warnings;

use File::Find;

my %results= ();

sub process {
	my ($fn, $reqs, $reps) = @_;
	open FIN, '<', $fn or die;
	while (<FIN>) {
		#print "<$_>";
		next unless /Statistics/;
		if (/Statistics\((\d+)\): \[(.)\] total bytes in: (\d+), out: (\d+)/) {
			$results{B}->{$reqs}->{$reps}->{$2}->{$1} = [$3, $4];
		}
	}
	close FIN;
}

sub wanted {
	return unless /^manager.out-(\d+)-(\d+)-(\d+)/;

	print STDERR "Processing: $_\n";
	process($File::Find::name, $2, $3);
}

find(\&wanted, '/tmp/');

foreach my $reqs (sort {$a<=>$b} keys %{$results{B}}) {
	foreach my $reps (sort {$a<=>$b} keys %{$results{B}->{$reqs}}) {
		my @ary = ();
		foreach my $type ('C','R') {
			foreach my $id (0..3) {
				my @a = @{$results{B}->{$reqs}->{$reps}->{$type}->{$id}};
				push @ary, @a;
			}
		}
		print "$reqs c\t\t";
		print join "\t", @ary;
		print "\n";

		#printf "%s\t%s\n", , $results{RT}->{$reqs}->{$reps}->{$nc};
		#printf "%d/%d %4d [TH]: %s\n", $reqs, $reps, $nc, $results{TH}->{$reqs}->{$reps}->{$nc};
		#printf "%d/%d %4d [RT]: %s\n", $reqs, $reps, $nc, $results{RT}->{$reqs}->{$reps}->{$nc};
	}
}
