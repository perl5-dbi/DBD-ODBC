#!/usr/bin/perl -I./t
$| = 1;


# to help ActiveState's build process along by behaving (somewhat) if a dsn is not provided
my $tests = 3;
BEGIN {
   unless (defined $ENV{DBI_DSN}) {
      print "1..0 # Skipped: DBI_DSN is undefined\n";
      exit;
   }
}

use DBI qw(:sql_types);
use ODBCTEST;

{
    my $numTest = 0;
    sub Test($;$) {
	my $result = shift; my $str = shift || '';
	printf("%sok %d%s\n", ($result ? "" : "not "), ++$numTest, $str);
	$result;
    }
}


my $dbh = DBI->connect() || die "Connect failed: $DBI::errstr\n";
my $dbname = $dbh->get_info(17); # DBI::SQL_DBMS_NAME
unless ($dbname =~ /Oracle/i) {
   print "1..0 # Skipped: Oracle tests not supported using ", $dbname, "\n";;
   exit 0;
} 

print "1..$tests\n";
Test(1);
# $dbh->trace(9, "c:/trace.txt");
$dbh->do("create or replace function PERL_DBD_TESTFUNC(a in integer, b in integer) return integer is c integer; begin if b is null then c := 0; else c := b; end if; return a * c + 1; end;");
my $sth = $dbh->prepare("{ ? = call PERL_DBD_TESTFUNC(?, ?) }");
my $value = undef;
my $b = 30;
$sth->bind_param_inout(1, \$value, 50, SQL_INTEGER);
$sth->bind_param(2, 10, SQL_INTEGER);
$sth->bind_param(3, 30, SQL_INTEGER);
$sth->execute;
$value += 0;
Test($value == 301);

$b = undef;
$sth->bind_param_inout(1, \$value, 50, SQL_INTEGER);
$sth->bind_param(2, 20, SQL_INTEGER);
$sth->bind_param(3, undef, SQL_INTEGER);
$sth->execute;
$value += 0;
Test($value == 1);

   
$dbh->disconnect;
