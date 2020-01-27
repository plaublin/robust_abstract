#!/usr/bin/perl 
#===============================================================================
#
#         FILE:  parse_ta.pl
#
#        USAGE:  ./parse_ta.pl 
#
#  DESCRIPTION:  
#
#      OPTIONS:  ---
# REQUIREMENTS:  ---
#         BUGS:  ---
#        NOTES:  ---
#       AUTHOR:   (), <>
#      COMPANY:  
#      VERSION:  1.0
#      CREATED:  04/27/2010 08:46:43 AM MDT
#     REVISION:  ---
#===============================================================================

use strict;
use warnings;

use File::Find;

my %results = ();
my $TIMINGS_ID = 0;

sub process_file {
	my ($fn, $name, $nc, $reqs) = @_;
	open FIN, '<', $fn or (print "PROBLEM: $!\n" and return);
	while (<FIN>) {
		next unless /TIMINGS\[(\d+)\]\[(\d+)\]/;
		my $id = $1;
		my $timing_id = $2;
		next unless $timing_id eq $TIMINGS_ID;
		if (/average time usec: ([\d.]+)/) {
			push @{$results{$name}->{$nc}->{$reqs}->{$id}}, $1;
		}
	}
	close FIN;
}

sub wanted {
	return unless /^ta-(.*)-(\d+)-(\d+).data/;

	print STDERR "Processing: $_\n";
	process_file($File::Find::name, $1, $2, $3);
}

sub std_dev_ref_sum {
	my $ar = shift;
	my $elements = scalar @$ar;
	my $sum = 0;
	my $sumsq = 0;

	foreach (@$ar) {
		$sum += $_;
		$sumsq += ($_ **2);
	}
	my @rv = ($sum/$elements, sqrt( $sumsq/$elements -
		(($sum/$elements) ** 2)));
	return @rv;
}

sub process_data {
	foreach my $name (keys %results) {
		open FOUT, '>', "ta-$name.dat" or die "Can't open: $!\n";
		foreach my $nc (sort {$a<=>$b} keys %{$results{$name}}) {
			foreach my $reqs (sort {$a<=>$b} keys %{$results{$name}->{$nc}}) {
				print FOUT "$nc $reqs ";
				my @data = ();
				foreach my $id (sort {$a<=>$b} keys %{$results{$name}->{$nc}->{$reqs}}) {
					my $aref = $results{$name}->{$nc}->{$reqs}->{$id};
					my ($a, $sd) = std_dev_ref_sum($aref);
					push @data, @$aref;
					print FOUT "$a $sd ";
				}
				my ($a, $sd) = std_dev_ref_sum(\@data);
				print FOUT "$a $sd\n";
			}
		}
		close FOUT;
	}
	
	# aggregate per number of clients
	foreach my $name (keys %results) {
		foreach my $nc (sort {$a<=>$b} keys %{$results{$name}}) {
			open FOUT, '>', "ta-$name-per-nc-$nc.dat";
			foreach my $reqs (sort {$a<=>$b} keys %{$results{$name}->{$nc}}) {
				print FOUT "$reqs ";
				my @data = ();
				my @cdata = ();
				foreach my $id (sort {$a<=>$b} keys %{$results{$name}->{$nc}->{$reqs}}) {
					my $aref = $results{$name}->{$nc}->{$reqs}->{$id};
					push @{$data[$id]}, @$aref;
					push @cdata, @$aref;
				}
				foreach my $ar (@data) {
					my ($a, $sd) = std_dev_ref_sum($ar);
					print FOUT "$a $sd ";
				}
				my ($a, $sd) = std_dev_ref_sum(\@cdata);
				print FOUT "$a $sd\n";
			}
			close FOUT;
		}
	}
	
	# aggregate per clients and request sizes
	# append to file!!!
	open FOUT, '>', "ta-per-proto.dat";
	foreach my $name (keys %results) {
		my @data = ();
		my @cdata = ();
		foreach my $nc (sort {$a<=>$b} keys %{$results{$name}}) {
			foreach my $reqs (sort {$a<=>$b} keys %{$results{$name}->{$nc}}) {
				foreach my $id (sort {$a<=>$b} keys %{$results{$name}->{$nc}->{$reqs}}) {
					my $aref = $results{$name}->{$nc}->{$reqs}->{$id};
					push @{$data[$id]}, @$aref;
					push @cdata, @$aref;
				}
			}
		}
		print FOUT "$name ";
		foreach my $ar (@data) {
			my ($a, $sd) = std_dev_ref_sum($ar);
			print FOUT "$a $sd ";
		}
		my ($a, $sd) = std_dev_ref_sum(\@cdata);
		print FOUT "$a $sd\n";
	}
	close FOUT;
}

$TIMINGS_ID = (defined $ARGV[1])?$ARGV[1]:0;

find(\&wanted, defined $ARGV[0]?$ARGV[0]:'.');

process_data;


