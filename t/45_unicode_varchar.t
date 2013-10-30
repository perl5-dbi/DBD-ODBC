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

$dbh->do(q/create table PERL_DBD_TABLE1 (a varchar(100))/);

my $sql = q/insert into PERL_DBD_TABLE1 (a) values(?)/;

# a simple unicode string
my $euro = "\x{20ac}\x{a3}";
diag "Inserting a unicode euro, utf8 flag on:\n";
my $s = $dbh->prepare($sql); # redo to ensure no sticky params
execute($s, $euro);
show_it($dbh, [2], [2]);

Test::NoWarnings::had_no_warnings() if ($has_test_nowarnings);
done_testing();
