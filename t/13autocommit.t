#!/usr/bin/perl -w -I./t
#
# Check that STORE, commit and rollback sets DBI's internal flags to match connection flags
#
use Test::More;
use Test::Output;
use strict;

use DBI;
use_ok('ODBCTEST');

eval "require Test::NoWarnings";
my $has_test_nowarnings = ($@ ? undef : 1);


BEGIN {
   if (!defined $ENV{DBI_DSN}) {
      plan skip_all => "DBI_DSN is undefined";
   }
    my $dbh = DBI->connect();
    unless($dbh) {
        BAIL_OUT("Unable to connect to the database $DBI::errstr\nTests skipped.\n");
        exit 0;
    }
}

END {
    Test::NoWarnings::had_no_warnings()
          if ($has_test_nowarnings);
    done_testing();
}

{
    # Test that setting AutoCommit directly sets DBI flags correctly
    my $dbh = DBI->connect();
    unless($dbh) {
        BAIL_OUT("Unable to connect to the database $DBI::errstr\nTests skipped.\n");
        exit 0;
    }

    $dbh->{AutoCommit} = 0;
    ok(!$dbh->{AutoCommit}, 'Connection AutoCommit off');
    output_unlike(sub { $dbh->dump_handle }, undef, qr/AutoCommit/, 'Internal AutoCommit flag off');
    # FLAGS 0x100117: COMSET IMPSET Active Warn PrintError PrintWarn

    $dbh->{AutoCommit} = 1;
    ok($dbh->{AutoCommit}, 'Connection AutoCommit on');
    output_like(sub { $dbh->dump_handle }, undef, qr/AutoCommit/, 'Internal AutoCommit flag on');
    # FLAGS 0x100317: COMSET IMPSET Active Warn PrintError PrintWarn AutoCommit
}

{
    # Test that commit sets DBI flags correctly
    my $dbh = DBI->connect();
    unless($dbh) {
        BAIL_OUT("Unable to connect to the database $DBI::errstr\nTests skipped.\n");
        exit 0;
    }

    $dbh->{AutoCommit} = 1;
    ok($dbh->FETCH('AutoCommit'), 'Connection AutoCommit on');
    output_like(sub { $dbh->dump_handle }, undef, qr/AutoCommit/, 'Internal AutoCommit flag on');
    # FLAGS 0x100317: COMSET IMPSET Active Warn PrintError PrintWarn AutoCommit

    $dbh->begin_work;
    ok(!$dbh->FETCH('AutoCommit'), 'AutoCommit off');
    output_unlike(sub { $dbh->dump_handle }, undef, qr/AutoCommit/, 'Internal AutoCommit flag off');
    # FLAGS 0x104117: COMSET IMPSET Active Warn PrintError PrintWarn BegunWork

    $dbh->commit;
    ok($dbh->FETCH('AutoCommit'), 'Connection AutoCommit on');
    output_like(sub { $dbh->dump_handle }, undef, qr/AutoCommit/, 'Internal AutoCommit should be on after commit, no automated rollback should happen');
    # FLAGS 0x100317: COMSET IMPSET Active Warn PrintError PrintWarn AutoCommit

    # Going out of scope here used to generate a warning like:
    # DBD::ODBC::db DESTROY failed: [FreeTDS][SQL Server]The ROLLBACK TRANSACTION request has no corresponding BEGIN TRANSACTION. (SQL-25000)
}

{
    # Same test for rollback
    my $dbh = DBI->connect();
    unless($dbh) {
        BAIL_OUT("Unable to connect to the database $DBI::errstr\nTests skipped.\n");
        exit 0;
    }

    $dbh->{AutoCommit} = 1;
    ok($dbh->{AutoCommit}, 'Connection AutoCommit on');
    output_like(sub { $dbh->dump_handle }, undef, qr/AutoCommit/, 'Internal AutoCommit flag on');

    $dbh->begin_work;
    ok(!$dbh->{AutoCommit}, 'AutoCommit off');
    output_unlike(sub { $dbh->dump_handle }, undef, qr/AutoCommit/, 'Internal AutoCommit flag off');

    $dbh->rollback;
    ok($dbh->{AutoCommit}, 'Connection AutoCommit on');
    output_like(sub { $dbh->dump_handle }, undef, qr/AutoCommit/, 'Internal AutoCommit flag should be on after rollback, so no automated rollback should happen');
}
