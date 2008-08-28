#!/usr/bin/perl -w -I./t
# $Id$
#
# blob tests
# currently tests you can insert a clob with various odbc_putdata_start settings
#
use Test::More;
#####use strict;
$| = 1;

my $has_test_nowarnings = 1;
eval "require Test::NoWarnings";
$has_test_nowarnings = undef if $@;
my $tests = 21;
$tests += 1 if $has_test_nowarnings;
plan tests => $tests;

# can't seem to get the imports right this way
use DBI qw(:sql_types);
use_ok('ODBCTEST');

# to help ActiveState's build process along by behaving (somewhat) if a dsn is not provided
BEGIN {
   if (!defined $ENV{DBI_DSN}) {
      plan skip_all => "DBI_DSN is undefined";
   }
}
END {
    Test::NoWarnings::had_no_warnings()
          if ($has_test_nowarnings);
}

my $ev;

my $dbh = DBI->connect();
unless($dbh) {
   BAIL_OUT("Unable to connect to the database $DBI::errstr\nTests skipped.\n");
   exit 0;
}

my $putdata_start = $dbh->{odbc_putdata_start};
is($putdata_start, 32768, 'default putdata_start');

my $sth = $dbh->func(SQL_LONGVARCHAR, GetTypeInfo);
ok($sth, "GetTypeInfo");

my ($type_name, $type);

while (my @row = $sth->fetchrow) {
    if ($row[2] > 60000) {
        ($type_name, $type) = ($row[0], $row[1]);
    }
}

skip_all("ODBC Driver/Database has not got a big enough type", 5)
        if !$type_name;

eval { $dbh->do(qq/create table DBD_ODBC_drop_me(a $type_name)/); }; $ev = $@;
diag($ev) if $ev;
ok(!$ev, "table DBD_ODBC_drop_me created");

SKIP: {
    skip "Cannot create test table", 17 if $ev;

    my $bigval = "x" x 30000;
    test($dbh, $bigval);

    test($dbh, $bigval, 500);

    $bigval = 'x' x 60000;
    test($dbh, $bigval, 60001);
};

sub test
{
    my ($dbh, $val, $putdata_start) = @_;

    if ($putdata_start) {
        $dbh->{odbc_putdata_start} = $putdata_start;
        my $pds = $dbh->{odbc_putdata_start};
        is($pds, $putdata_start, "retrieved putdata_start = set value");
    }

    $sth = $dbh->prepare(q/insert into DBD_ODBC_drop_me values(?)/);
    ok($sth, "prepare for insert");
    ok($sth->execute($val), "insert clob");
    test_value($dbh, $val);

    eval {$dbh->do(q/delete from DBD_ODBC_drop_me/); };
    $ev = $@;
    diag($ev) if $ev;
    ok(!$ev, 'delete records from test table');

    return;
}

sub test_value
{
    my ($dbh, $value) = @_;

    local $dbh->{RaiseError} = 1;
    local $dbh->{LongReadLen} = 60001;
    my $row = $dbh->selectall_arrayref(q/select a from DBD_ODBC_drop_me/);
    $ev = $@;
    diag($ev) if $ev;
    ok(!$ev, 'select test data back');

    is($row->[0]->[0], $value, 'data read back compares');

    return;
}

END {
    if ($dbh) {
        eval {$dbh->do(q/drop table DBD_ODBC_drop_me/);};
    };
    warn ("Failed to delete test table DBD_ODBC_drop_me - $@, check this")
        if $@;
};
