#/usr/bin/perl -w

use strict;
use warnings;
use Carp;

my $waitedpid = 0;

sub child_do($$);

use POSIX ":sys_wait_h";

my @children = ();

my $nodenames_regex = $ARGV[0];
my $num_replicas = $ARGV[1];
my $num_clients = $ARGV[2];
my $ssh_command = $ARGV[3];
my $login_name = $ARGV[4];
my $command = $ARGV[5];

my $replica_count = 0;
my $client_count = 0;

my %cmachines = ();

while (my $l = <STDIN>) {
	next unless $l =~ /^$nodenames_regex/o;
	next unless ++$replica_count > $num_replicas;
	
	my ($cn, @rest) = split / /, $l; 
	$cmachines{$cn}++;
	last if ++$client_count == $num_clients;
};

foreach my $key (keys %cmachines) {
	my $node = $key;
	$node =~ s/_//g;
	#print STDOUT "$ssh_command -n $login_name\@$node '$command $node $cmachines{$key}'\n";

	sleep 2;
	my $pid = fork();
        if ($pid) {
        # parent
        #print "pid is $pid, parent $$\n";
		push(@children, $pid);
        } elsif ($pid == 0) {
                # child
                child_do($node,$cmachines{$key});
                exit 0;
        } else {
                die "couldnt fork: $!\n";
        }
}

foreach (@children) {
        my $tmp = waitpid($_, 0);
	print "done with pid $tmp\n";
}   

sub child_do($$) {
	my ($node, $num) = @_;
	print STDERR "WILL EXEC $ssh_command -n $login_name\@$node '$command $node $num'\n";
	system "$ssh_command -n $login_name\@$node '$command $node $num'";
}
