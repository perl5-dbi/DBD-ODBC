#!/usr/bin/perl -w -I./t
# $Id$
#
# Test sql_type_cast via DiscardString and StrictlyTyped
#
use Test::More;
use strict;
use Devel::Peek;
use B qw( svref_2object SVf_IOK SVf_NOK SVf_POK );

#use JSON::XS;

#my $got_json_xs;
#eval {
#    use JSON::XS
#};
#$go_json_xs = 1 unless $@;

$| = 1;

my $has_test_nowarnings = 1;
eval "require Test::NoWarnings";
$has_test_nowarnings = undef if $@;
my $tests = 9;
$tests += 1 if $has_test_nowarnings;
plan tests => $tests;

use DBI qw(:sql_types);
#1
use_ok('ODBCTEST');

my $dbh;

BEGIN {
   if (!defined $ENV{DBI_DSN}) {
      plan skip_all => "DBI_DSN is undefined";
   }
   if ($DBI::VERSION < 1.611) {
       plan skip_all => "DBI version is too old for this test";
   }
}

END {
    if ($dbh) {
        eval {
            local $dbh->{PrintWarn} = 0;
            local $dbh->{PrintError} = 0;
            $dbh->do(q/drop table PERL_DBD_drop_me/);
        };
        $dbh->disconnect;
    }
    Test::NoWarnings::had_no_warnings()
          if ($has_test_nowarnings);
}

sub is_iv {
   my $sv = svref_2object(my $ref = \$_[0]);
   my $flags = $sv->FLAGS;

   my $x = $sv->PV;
  
   if (wantarray) {
       return ($flags & SVf_IOK, $x);
   } else {
       return $flags & SVf_IOK;
   }
}

#sub is_json_iv {
#    my $x = encode_json($_[0]);
#    if ($x =~ /"/) {
#        return 0;
#    } else {
#        return 1;
#    }
#}

$dbh = DBI->connect();
$dbh->{RaiseError} = 1;

unless($dbh) {
   BAIL_OUT("Unable to connect to the database $DBI::errstr\nTests skipped.\n");
   exit 0;
}


my ($ev, $sth);

eval {
    local $dbh->{PrintWarn} = 0;
    local $dbh->{PrintError} = 0;
    $dbh->do('drop table PERL_DBD_drop_me');
};

eval {
    $dbh->do('create table PERL_DBD_drop_me (a varchar(10))');
};
$ev = $@;
#2
diag($ev) if $ev;
ok(!$ev, 'create test table with integer');

BAIL_OUT("Failed to create test table") if $ev;

eval {
    $dbh->do(q/insert into PERL_DBD_drop_me (a) values(100)/);
};
$ev = $@;
#3
diag($ev) if $ev;
ok(!$ev, 'insert into table');

BAIL_OUT("Failed to insert test data") if $ev;

# try as normal just fetching without binding
# we'd expect to get a string and the scalar not to have IOK
$sth = $dbh->prepare(q/select a from PERL_DBD_drop_me/);
$sth->execute;
my ($r) = $sth->fetchrow;

#my $j1 = encode_json [$r];
is(is_iv($r), 0, "! ivok no bind") or Dump($r);

#
# try binding - no type specified
# should be as above
#
$sth->bind_col(1, \$r);
$sth->execute;
$sth->fetch;
#my $j2 = encode_json [$r];
is(is_iv($r), 0, "! ivok bind") or Dump($r);

#
# try binding specifying an integer type
# expect IOK
#
$sth->bind_col(1, \$r, {TYPE => SQL_INTEGER});
$sth->execute;
$sth->fetch;
#my $j2 = encode_json [$r];
my ($iv, $pv) = is_iv($r);
ok($iv, "ivok bind integer") or Dump($r);
ok($pv, "pv not null bind integer") or Dump($r);

#
# try binding specifying an integer type and say discard the pv
# expect IOK
#
$sth->bind_col(1, \$r, {TYPE => SQL_INTEGER, DiscardString => 1});
$sth->execute;
$sth->fetch;
#my $j2 = encode_json [$r];
($iv, $pv) = is_iv($r);
ok($iv, "ivok bind integer discard") or Dump($r);
ok(!$pv, "pv null bind integer discard") or Dump($r);

# cannot do the following since the driver will whinge the type cannot
# be cast to an integer
# Invalid character value for cast specification (SQL-22018)
###### test StrictlyTyped
#####eval {$dbh->do(q/delete from PERL_DBD_drop_me/)};
#####$ev = $@;
#####diag($ev) if $ev;
#####BAIL_OUT('Cannot delete rows from table') if $ev;
#####
#####eval {$dbh->do(q/insert into PERL_DBD_drop_me (a) values('1fred')/)};
#####$ev = $@;
#####diag($ev) if $ev;
#####BAIL_OUT('Cannot insert secondary test rows') if $ev;
#####
#####$sth = $dbh->prepare(q/select a from PERL_DBD_drop_me/);
#####$sth->execute;
#####$sth->bind_col(1, \$r, {TYPE => SQL_INTEGER, StrictlyTyped => 0});
#####$sth->fetch;
#####($iv, $pv) = is_iv($r);
#####ok(!$iv, "ivok bind integer for strict") or Dump($r);
#####ok($pv, "pv null bind integer for strict") or Dump($r);
