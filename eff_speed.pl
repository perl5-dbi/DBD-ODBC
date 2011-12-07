use DBI;
use Data::Dumper;
use strict;
use warnings;

my $h = DBI->connect("dbi:ODBC:DSN=baugi","sa","easysoft",
		     {RaiseError => 1, PrintError => 0,odbc_disable_array_operations => 0});
eval {
    local $h->{PrintError} = 0;
    $h->do(q/drop table two/);
};

$h->do(q/create table two (a varchar(20))/);

my $fetch_row = 0;

my $x = '1' x 2000;

my @p = split (//,$x);

for (my $i = 0; $i < 100; $i++) {
    doit();
}

$h->disconnect;

sub doit {
    print "dbh odbc_batch_size=", $h->{odbc_batch_size}, "\n";
    my $s = $h->prepare(q/insert into two values(?)/);
    print "sth odbc_batch_size=", $s->{odbc_batch_size}, "\n";
    my ($tuples, $rows, @tuple_status);
    print "About to run execute_for_fetch\n";
    eval {
        ($tuples, $rows) = $s->execute_for_fetch(\&fetch_sub, \@tuple_status);
    };
    if ($@) {
        print "execute_for_fetch died : $@ END\n";
    }
    #print "tuples = ", Dumper($tuples), "rows = ", Dumper($rows), "\n";
    #print "tuple status ", Dumper(\@tuple_status), "\n";

    $s = undef;


    #my $r = $h->selectall_arrayref(q/select * from two/);
    #print Dumper($r);

    #$h->do(q/delete from two/);
    
}

sub fetch_sub {
    #print "fetch_sub $fetch_row\n";
    if ($fetch_row == @p) {
        print "returning undef\n";
        $fetch_row = 0;
        return;
    }

    return [$p[$fetch_row++]];

}


