use DBI qw(:sql_types);
use Data::Dumper;
use strict;
use warnings;
use Encode;
#use Devel::Leak;

binmode(STDOUT, ':encoding(utf8)');
my $h = DBI->connect("dbi:ODBC:DSN=asus2","sa","easysoft",
		     {AutoCommit => 1, RaiseError => 1, PrintError => 0,odbc_disable_array_operations => 0});
eval {
    local $h->{PrintError} = 0;
    $h->do(q/drop table one/);
    $h->do(q/drop table two/);
    $h->do(q/drop table three/);
};

$h->do(q/create table one (a nvarchar(20))/); # source table
$h->do(q/create table two (a nvarchar(20) primary key)/); # destination table
$h->do(q/create table three (a integer)/); # destination table for binding as int
#$h->do(q/create table two (a nvarchar(20) null)/);

my $s = $h->prepare(q/insert into one values(?)/);
for (my $i = 0; $i < 10; $i++) {
    $s->execute("$i");
}

my $fetch_row = 0;
my $euro = "\x{20ac}";
print "$euro\n";
print "euro is utf8 : ", (Encode::is_utf8($euro) ? 'yes' : 'no'), "\n";
print "euro length : ", length($euro), "\n";
my @p = ($euro,			  # simple unicode char
	 2,			# simple number
	 'string',		# non unicode string
	 '12345678901234567890', # just fitting non unicode string
	 'aaaaaaaaaaaaaaaaaaaaa', # too big a non unicode string
	 2,			  # violation of primary key
	 3,
	 # just fitting unicode string
         "\x{20ac}\x{20ac}\x{20ac}\x{20ac}\x{20ac}\x{20ac}\x{20ac}\x{20ac}\x{20ac}\x{20ac}\x{20ac}\x{20ac}\x{20ac}\x{20ac}\x{20ac}\x{20ac}\x{20ac}\x{20ac}\x{20ac}\x{20ac}"
    );
#my @p = (undef,2,3,4,5);

#my $count = Devel::Leak::NoteSV($fred);
doit();
my $r = $h->selectall_arrayref(q/select * from two/);
print Dumper($r);
$h->do(q/delete from two/);

doit2();
$r = $h->selectall_arrayref(q/select * from two/);
print Dumper($r);

my $fetch_row = 0;
@p = ('1', '2');
doit3();
$r = $h->selectall_arrayref(q/select * from three/);
print Dumper($r);

#Devel::Leak::CheckSV($fred);
print "$euro\n";
print "euro is utf8 : ", (Encode::is_utf8($euro) ? 'yes' : 'no'), "\n";
print "euro length : ", length($euro), "\n";


#$h->do(q/delete from two/);
    
$h->disconnect;

sub doit {
    print "dbh odbc_batch_size=", $h->{odbc_batch_size}, "\n";
    $s = $h->prepare(q/insert into two values(?)/);
    print "sth odbc_batch_size=", $s->{odbc_batch_size}, "\n";
    my ($tuples, $rows, @tuple_status);
    print "About to run execute_for_fetch\n";
    eval {
        ($tuples, $rows) = $s->execute_for_fetch(\&fetch_sub, \@tuple_status);
    };
    if ($@) {
        print "execute_for_fetch died : $@ END\n";
    }
    print "tuples = ", Dumper($tuples), "rows = ", Dumper($rows), "\n";
    print "tuple status ", Dumper(\@tuple_status), "\n";

    print "execute_array\n";
    my $x = '1' x 2;
    my @x = split (//,$x);
    @x = (9,10,11);
    print "odbc_batch_size = ", $s->{odbc_batch_size}, "\n";
    eval {
        $s->execute_array(undef, \@x);
    };
    if ($@) {
        print "execute_array died : $@\n";
    }

    $s = undef;

}
# fetch from one statement and insert in another
sub doit2 {
    my $sel = $h->prepare(q/select a from one/);
    $sel->execute;

    my $ins = $h->prepare(q/insert into two (a) values(?)/);
    my $fts = sub {$sel->fetchrow_arrayref};
    my @ts;
    my $rc = $ins->execute_for_fetch($fts, \@ts);
    my @errors = grep { ref $_ } @ts;
    print Dumper(\@errors);
}

sub doit3 {
    $s = $h->prepare(q/insert into three values(?)/);
    $s->bind_param(1, undef, {TYPE => SQL_INTEGER});
    my ($tuples, $rows, @tuple_status);
    print "About to run execute_for_fetch\n";
    eval {
        ($tuples, $rows) = $s->execute_for_fetch(\&fetch_sub, \@tuple_status);
    };
    if ($@) {
        print "execute_for_fetch died : $@ END\n";
    }
    print "tuples = ", Dumper($tuples), "rows = ", Dumper($rows), "\n";
    print "tuple status ", Dumper(\@tuple_status), "\n";
}
sub fetch_sub {
    print "fetch_sub $fetch_row\n";
    if ($fetch_row == @p) {
        print "returning undef\n";
        $fetch_row = 0;
        return;
    }

    return [$p[$fetch_row++]];

}


