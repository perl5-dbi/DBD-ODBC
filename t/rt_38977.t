#!/usr/bin/perl -w -I./t
# $Id$
#
# rt 38977 and 48304
#
# test varbinary(MAX), varchar(MAX) and nvarchar(MAX) types in SQL Server
#
use Test::More;
use strict;
$| = 1;

my $has_test_nowarnings = 1;
eval "require Test::NoWarnings";
$has_test_nowarnings = undef if $@;
my $tests = 14;
$tests += 1 if $has_test_nowarnings;
plan tests => $tests;

# can't seem to get the imports right this way
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
            $dbh->do(q/drop table PERL_DBD_rt_38977/);
        };
    }
    Test::NoWarnings::had_no_warnings()
          if ($has_test_nowarnings);
}

$dbh = DBI->connect();
unless($dbh) {
   BAIL_OUT("Unable to connect to the database $DBI::errstr\nTests skipped.\n");
   exit 0;
}
$dbh->{RaiseError} = 1;

my $dbms_name = $dbh->get_info(17);
ok($dbms_name, "got DBMS name: $dbms_name");
my $dbms_version = $dbh->get_info(18);
ok($dbms_version, "got DBMS version: $dbms_version");
my $driver_name = $dbh->get_info(6);
ok($driver_name, "got DRIVER name: $driver_name");
my $driver_version = $dbh->get_info(7);
ok($driver_version, "got DRIVER version $driver_version");

my ($ev, $sth);

SKIP: {
    skip "not SQL Server", 9 if $dbms_name !~ /Microsoft SQL Server/;
    skip "Easysoft OOB", 6 if $driver_name =~ /esoobclient/;
    my $major_version = $dbms_version;
    $major_version =~ s/^(\d+)\..*$/$1/;
    #diag("Major Version: $major_version\n");
    skip "SQL Server version too old", 6 if $major_version < 9;

    eval {
        local $dbh->{PrintWarn} = 0;
        local $dbh->{PrintError} = 0;
        $dbh->do('drop table PERL_DBD_38977');
    };

    eval {
        $dbh->do('create table PERL_DBD_38977 (a VARCHAR(MAX))');
    };
    $ev = $@;
    ok(!$ev, 'create test table with varchar(max)');

  SKIP: {
        skip "Failed to create test table", 2 if ($ev);
        eval {
            $sth = $dbh->prepare('INSERT into PERL_DBD_38977 VALUES (?)');
        };
        $ev = $@;
        ok($sth && !$@, "prepare insert");
      SKIP: {
            skip "Failed to prepare", 1 if ($ev);
            my $x = 'x' x 500000;
            eval {
                $sth->execute($x);
            };
            $ev = $@;
            ok(!$ev, "execute insert");
            if ($ev) {
                diag("Execute for insert into varchar(max) failed with $ev");
                diag(q/Some SQL Server drivers such as the native client 09.00.1399 / .
                     q/driver fail this test with a HY104, "Invalid precision error". / .
                     qq/You have driver $driver_name at version $driver_version. / .
                     q/There is a free upgrade from Microsoft of the native client driver /.
                     q/to 10.00.1600 which you will need if you intend to insert / .
                     q/into varchar(max) columns./);
            }
        };
    };
    eval {
        local $dbh->{PrintWarn} = 0;
        local $dbh->{PrintError} = 0;
        $dbh->do('drop table PERL_DBD_38977');
    };

    eval {
        $dbh->do('create table PERL_DBD_38977 (a VARBINARY(MAX))');
    };
    $ev = $@;
    ok(!$ev, 'create test table with varbinary(max)');

  SKIP: {
        skip "Failed to create test table", 2 if ($ev);
        eval {
            $sth = $dbh->prepare('INSERT into PERL_DBD_38977 VALUES (?)');
        };
        $ev = $@;
        ok($sth && !$@, "prepare insert");
      SKIP: {
            skip "Failed to prepare", 1 if ($ev);
            my $x = 'x' x 500000;
            ok($sth->execute($x), "execute insert");
        };
    };

    eval {
        local $dbh->{PrintWarn} = 0;
        local $dbh->{PrintError} = 0;
        $dbh->do('drop table PERL_DBD_38977');
    };

    eval {
        $dbh->do('create table PERL_DBD_38977 (a NVARCHAR(MAX))');
    };
    $ev = $@;
    ok(!$ev, 'create test table with nvarchar(max)');

  SKIP: {
        skip "Failed to create test table", 2 if ($ev);
        eval {
            $sth = $dbh->prepare('INSERT into PERL_DBD_38977 VALUES (?)');
        };
        $ev = $@;
        ok($sth && !$@, "prepare insert");
      SKIP: {
            skip "Failed to prepare", 1 if ($ev);
            my $x = 'x' x 4001;
            ok($sth->execute($x), "execute insert");
        };
    };

};

#my $ev;
#
#eval {$h->do('drop table binary_meta');};
#$h->do('create table binary_meta (doc_id INTEGER NOT NULL, meta_name  VARCHAR (255), meta_value VARCHAR(MAX), meta_idx   INTEGER, from_ver  BIGINT, to_ver BIGINT)');
#my $s = $h->prepare('INSERT into binary_meta VALUES (?, ?, ?, ?, ?, ?)');
#my $x = 'x' x 5000000;
#$s->execute(1, 'fred', $x, 1, 1, 1);

