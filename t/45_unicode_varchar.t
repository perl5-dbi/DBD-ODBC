#!/usr/bin/perl -w -I./t

use open ':std', ':encoding(utf8)';
use Test::More;
use strict;

$| = 1;

use DBI qw(:utils);
use DBI::Const::GetInfoType;
my $has_test_nowarnings = 1;
eval "require Test::NoWarnings";
$has_test_nowarnings = undef if $@;

my $dbh;

BEGIN {
	if ($] < 5.008001) {
		plan skip_all => "Old Perl lacking unicode support";
	} elsif (!defined $ENV{DBI_DSN}) {
             plan skip_all => "DBI_DSN is undefined";
      }
}

END {
    # tidy up
    if ($dbh) {
        local $dbh->{PrintError} = 0;
        local $dbh->{PrintWarn} = 0;
        eval {
            $dbh->do(q/drop table PERL_DBD_TABLE1/);
        };
    }
}

sub collations {
    my ($h, $table) = @_;

    # so we can use :: not meaning placeholders
    $h->{odbc_ignore_named_placeholders} = 1;

    # get database name to use later when finding collation for table
    my $database_name = $h->get_info($GetInfoType{SQL_DATABASE_NAME});
    diag "Database: ", $database_name;

    # now find out the collations
    # server collation:
    my $r = $h->selectrow_arrayref(
        q/SELECT CONVERT (varchar, SERVERPROPERTY('collation'))/);
    diag "Server collation: ", $r->[0], "\n";

    # database collation:
    $r = $h->selectrow_arrayref(
        q/SELECT CONVERT (varchar, DATABASEPROPERTYEX(?,'collation'))/,
        undef, $database_name);
    diag "Database collation: ", $r->[0];

    # now call sp_help to find out about our table
    # first result-set should be name, owner, type and create datetime
    # second result-set should be:
    #  column_name, type, computed, length, prec, scale, nullable, trimtrailingblanks,
    #  fixedlennullinsource, collation
    # third result-set is identity columns
    # fourth result-set is row guilded columns
    # there are other result-sets depending on the object
    # sp_help -> http://technet.microsoft.com/en-us/library/ms187335.aspx
    my $column_collation;
    diag "Calling sp_help for table:";
    my $s = $h->prepare(q/{call sp_help(?)}/);
    $s->execute($table);
    my $result_set = 1;
    do {
        my $rows = $s->fetchall_arrayref;
        if ($result_set <= 2) {
            foreach my $row (@{$rows}) {
                diag join(",", map {$_ ? $_ : 'undef'} @{$row});
            }
        }
        if ($result_set == 2) {
            foreach my $row (@{$rows}) {
                diag "column:", $row->[0], " collation:", $row->[9], "\n";
                $column_collation = $row->[9];
            }
        }
        $result_set++;
    } while $s->{odbc_more_results};

    # now using the last column collation from above find the codepage
    $r = $h->selectrow_arrayref(
        q/SELECT COLLATIONPROPERTY(?, 'CodePage')/,
        undef, $column_collation);
    diag "Code page for column collation: ", $r->[0];
}

sub code_page {
    eval {require Win32::API};
    if ($@) {
        diag("Win32::API not available");
        return;
    }
    Win32::API::More->Import("kernel32", "UINT GetConsoleOutputCP()");
    Win32::API::More->Import("kernel32", "UINT GetACP()");
    my $cp = GetConsoleOutputCP();
    diag "Current active console code page: $cp\n";
    $cp = GetACP();
    diag "active code page: $cp\n";
}

sub ords {
    my $str = shift;

    use bytes;

    diag "    ords of output string:";
    foreach my $s(split(//, $str)) {
        diag sprintf("%x", ord($s)), ",";
    }
}

sub show_it {
    my ($h, $expected_perl_length, $expected_db_length) = @_;

    my $r = $h->selectall_arrayref(q/select len(a), a from PERL_DBD_TABLE1/);

    foreach my $row(@$r) {
        is($row->[0], shift @{$expected_db_length}, "db character length") or
            diag("dsc: " . data_string_desc($row->[0]));
        if (!is(length($row->[1]), shift @{$expected_perl_length},
                "expected perl length")) {
            diag(data_string_desc($row->[1]));
            ords($row->[1]);
        }
    }
    $h->do(q/delete from PERL_DBD_TABLE1/);
}

sub execute {
    my ($s, $string) = @_;

    #diag "  INPUT:";

    #diag "    input string: $string";
    #diag "    data_string_desc of input string: ", data_string_desc($string);
    #diag "    ords of input string: ";
    #foreach my $s(split(//, $string)) {
    #    diag sprintf("%x,", ord($s));
    #}

    #{
    #    diag "    bytes of input string: ";
    #    use bytes;
    #    foreach my $s(split(//, $string)) {
    #        diag sprintf("%x,", ord($s));
    #    }
    #}

    ok($s->execute($string), "execute");
}

$dbh = DBI->connect();
unless($dbh) {
   BAIL_OUT("Unable to connect to the database $DBI::errstr\nTests skipped.\n");
   exit 0;
}
$dbh->{RaiseError} = 1;

my $dbname = $dbh->get_info($GetInfoType{SQL_DBMS_NAME});

if ($dbname !~ /Microsoft SQL Server/i) {
    note "Not MS SQL Server";
    done_testing();
    exit 0;
}

if (!$dbh->{odbc_has_unicode}) {
    note "Not a unicode build of DBD::ODBC";
    done_testing;
    exit 0;
}

if ($^O eq 'MSWin32') {
    code_page();
}

eval {
    $dbh->do(q/create table PERL_DBD_TABLE1 (a varchar(100) collate Latin1_General_CI_AS)/);
};
if ($@) {
    diag "Cannot create table with collation - $@";
    done_testing();
    exit 0;
}

collations($dbh, 'PERL_DBD_TABLE1');

my $sql = q/insert into PERL_DBD_TABLE1 (a) values(?)/;

# a simple unicode string
my $euro = "\x{20ac}\x{a3}";
diag "Inserting a unicode euro, utf8 flag on:\n";
my $s = $dbh->prepare($sql); # redo to ensure no sticky params
execute($s, $euro);
show_it($dbh, [2], [2]);

Test::NoWarnings::had_no_warnings() if ($has_test_nowarnings);
done_testing();
