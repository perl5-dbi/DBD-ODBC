#!/usr/bin/perl -w -I./t
# $Id$
# loads of execute_array and execute_for_fetch tests using DBD::ODBC's native methods

use Test::More;
use strict;
use Data::Dumper;

$| = 1;

my $has_test_nowarnings = 1;
eval "require Test::NoWarnings";
$has_test_nowarnings = undef if $@;

my ($dbh, $ea);

use DBI qw(:sql_types);
use ExecuteArray;

BEGIN {
    plan skip_all => "DBI_DSN is undefined"
        if (!defined $ENV{DBI_DSN});
}
END {
    if ($dbh && $ea) {
        $ea->drop_table($dbh);
        $dbh->disconnect();
    }
    Test::NoWarnings::had_no_warnings()
          if ($has_test_nowarnings);
    done_testing();
}

diag("\n\nNOTE: This is an experimental test. Since DBD::ODBC added the execute_for_fetch method this tests the native method and not DBI's fallback method. If you fail this test and want to use DBI's execute_for_fetch or execute_array methods you should consider setting odbc_disable_array_operations to fall back to DBI's default handling of these methods. This is safer but not as quick. If it fails it should not stop you installing DBD::ODBC but if it fails with an error other than something indicating 'connection busy' it would be worth rerunning it with TEST_VERBOSE set or using prove and sending the results to the dbi-users mailing list.\n\n");
$dbh = DBI->connect();
unless($dbh) {
   BAIL_OUT("Unable to connect to the database $DBI::errstr\nTests skipped.\n");
   exit 0;
}
note("Using driver $dbh->{Driver}->{Name}");

$ea = ExecuteArray->new($dbh, 0); # don't set odbc_disable_array_operations

$ea->drop_table($dbh);
ok($ea->create_table($dbh), "create test table") or exit 1;
$ea->simple($dbh, {array_context => 1, raise => 1});
$ea->simple($dbh, {array_context => 0, raise => 1});
$ea->error($dbh, {array_context => 1, raise => 1});
$ea->error($dbh, {array_context => 0, raise => 1});
$ea->error($dbh, {array_context => 1, raise => 0});
$ea->error($dbh, {array_context => 0, raise => 0});

$ea->row_wise($dbh, {array_context => 1, raise => 1});

$ea->update($dbh, {array_context => 1, raise => 1});
