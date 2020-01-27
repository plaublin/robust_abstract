#!/bin/perl

use strict;
use warnings;

use feature 'say';

use Getopt::Long;
use List::MoreUtils qw/mesh/;

my $factor = 3000;
my ($proto, $time, $f, $tag, $nc, $reqs, $reps);
my ($replica, $pos, $total_samples, $total_ticks, $average);
my $query = '';
my $reduce = 0;

my @OUT = qw/proto texec f tag nc reqs reps replica pos total_samples total_ticks average/;

GetOptions ( "factor=i" => \$factor,
			 "query=s"  => \$query,
			 "reduce" => \$reduce,
);

my %query = ();
%query = split /[=,]/, $query if $query;

{
	my @out_v = ();
	my $cached = 0;
	sub checkQuery {
		my ($r, $q, $v) = @_;
		my $rv = 0;

		return @OUT unless %$q;

		foreach my $k (keys %$q) {
			return () unless $q->{$k} ~~ $v->{$k};
		}
		return @OUT if not $r;
		return @out_v if $cached;

		foreach my $k (@OUT) {
			push @out_v, $k if not exists $q->{$k};
		}
		$cached = 1;
		return @out_v;
	}
}

my %vals = ();

$" = "\t";
while (<>) {
	my %vals_t = ();
	if (/^::/) {
		my @a = qw{proto texec f tag nc reqs reps};
		my @b = ($_ =~ m/::(\w+)-([\w\d]+)-(\d+)-(.+)\.dat.*\[(\d+)\]\s+\[(\d+)\]\s+\[(\d+)\]/);
		%vals = mesh @a, @b;
	} elsif (/TIMINGS/) {
		%vals_t = %vals;
		my @a = qw(replica pos total_samples total_ticks average);
		my @b = ($_ =~ m/TIMINGS\[(\d+)\]\[(\d+)\]:\s+total samples:\s+(\d+),\s+total ticks: (\d+),\s+average ticks:\s+(\d+)/);
		my %n = mesh(@a, @b);
		@vals_t{keys %n} = values %n;

		my @r = checkQuery($reduce, \%query, \%vals_t);
		if (@r) {
			print "@vals_t{@r}";
			print "\t", $vals_t{total_ticks}/($factor*$vals_t{nc});
			print "\n";
		}
	}
}