#!/usr/bin/perl -w -I./t
# $Id$
#
# rt62033 - not really this rt but a bug discovered when looking in to it
#
# Check active is enabled on a statement after SQLMoreResults indicates
# there is another result-set.
#
use Test::More;
use strict;

use DBI qw(:sql_types);
use_ok('ODBCTEST');

my $dbh;

BEGIN {
   if (!defined $ENV{DBI_DSN}) {
      plan skip_all => "DBI_DSN is undefined";
   }
}

END {
    if ($dbh) {
        eval {
            local $dbh->{PrintWarn} = 0;
            local $dbh->{PrintError} = 0;
            $dbh->do(q/drop table PERL_DBD_RT_62033/);
        };
    }
}

$dbh = DBI->connect();
unless($dbh) {
   BAIL_OUT("Unable to connect to the database $DBI::errstr\nTests skipped.\n");
   exit 0;
}
$dbh->{RaiseError} = 1;
$dbh->{ChopBlanks} = 1;

my $dbms_name = $dbh->get_info(17);
ok($dbms_name, "got DBMS name: $dbms_name"); # 2
my $dbms_version = $dbh->get_info(18);
ok($dbms_version, "got DBMS version: $dbms_version"); # 3
my $driver_name = $dbh->get_info(6);
ok($driver_name, "got DRIVER name: $driver_name"); # 4
my $driver_version = $dbh->get_info(7);
ok($driver_version, "got DRIVER version $driver_version"); # 5

my ($ev, $sth);

# this needs to be MS SQL Server
if ($dbms_name !~ /Microsoft SQL Server/) {
    note('Not Microsoft SQL Server');
    done_testing();
    exit 0;
}
eval {
    local $dbh->{PrintWarn} = 0;
    local $dbh->{PrintError} = 0;
    $dbh->do('drop table PERL_DBD_RT_62033');
};

# try and create a table to test with
eval {
    $dbh->do(
        'create table PERL_DBD_RT_62033 (a int identity, b char(10) not null)');
};
$ev = $@;

if ($@) {
    BAIL_OUT("Failed to create test table - aborting test ($ev)");
    exit 0;
}
pass('created test table');

sub doit
{
    my $dbh = shift;
    my $expect = shift;

    my $s = $dbh->prepare_cached(
        q/insert into PERL_DBD_RT_62033 (b) values(?);select @@identity/);
    $s->execute(@_);
    #diag "sql errors $DBI::errstr\n" if $DBI::errstr;

    my $x = $s->{odbc_more_results};
    my $identity;
    ($identity) = $s->fetchrow_array;
    #diag("identity = ", DBI::neat($identity), "\n");
    is($identity, $expect, "Identity");
    ($identity) = $s->fetchrow_array;
}

doit($dbh, undef, undef);
doit($dbh, 2, 'fred');

eval {
    local $dbh->{PrintWarn} = 0;
    local $dbh->{PrintError} = 0;
    $dbh->do('drop table PERL_DBD_RT_62033');
};

done_testing();

