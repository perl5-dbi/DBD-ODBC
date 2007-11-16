use strict;
use warnings;
use DBI;

my $h = DBI->connect();

eval {
    $h->do(q{drop table martin});
};

$h->do(q{create table martin (a int)});

$h->do('insert into martin values(1)');

my $s;
#$s = $h->prepare('select * into #tmp from martin',
#                    { odbc_exec_direct => 1}
#);
#$s->execute;
$h->do('select * into #tmp from martin');

print "NUM_OF_FIELDS: " . DBI::neat($s->{NUM_OF_FIELDS}), "\n";

$s = $h->selectall_arrayref(q{select * from #tmp});
use Data::Dumper;
print Dumper($s), "\n";
