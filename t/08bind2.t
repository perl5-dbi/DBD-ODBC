#!/usr/bin/perl -I./t
$| = 1;



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

if (!$dbh->func(58, GetFunctions))  { #SQL_API_SQLDescribeParam
   print "SQLDescribeParam not supported under ", $dbh->get_info(17), "\n";
   print "1..0\n";
   exit 0;
} 

$dbh->{RaiseError} = 0;
$dbh->{PrintError} = 0;
$dbh->{LongReadLen} = 10000;

my $longstr = "This is a test of a string that is longer than 80 characters.  It will be checked for truncation and compared with itself.";
my $longstr2 = $longstr . "  " . $longstr . "  " . $longstr . "  " . $longstr;
my $longstr3 = $longstr2 . "  " . $longstr2;

my @data_no_dates = (
	[ 1, 'foo', 'test1', undef, undef ],
	[ 2, 'bar', 'test1', undef, undef ],
	[ 3, 'bletch', 'test1', undef, undef],
);

my @data_no_dates_with_long = (
	[ 4, 'foo2', $longstr, undef, undef ],
	[ 5, 'bar2', $longstr2, undef, undef ],
	[ 6, 'bletch2', $longstr3, undef, undef],
);

my @data_with_dates = (
	[ 7, 'foo22', 'test3',     "{d '1998-05-13'}", "{ts '1998-05-13 00:01:00'}"],
	[ 8, 'bar22', 'test3',    "{d '1998-05-14'}", "{ts '1998-05-14 00:01:00'}"],
	[ 9, 'bletch22', 'test3', "{d '1998-05-15'}", "{ts '1998-05-15 00:01:00'}"],
);

print "1..3\n";
print " Test 1:  insert various test data, without having this test tell the driver the type\n";
print "          that is being bound to a column.  This tests the use of SQLDescribeParam to obtain \n";
print "          the column type on the insert.  This is experimental and will most likely fail.\n";

# turn off default binding of varchar to test this!
$dbh->{odbc_default_bind_type} = 0;
$rc = ODBCTEST::tab_insert_bind($dbh, \@data_no_dates, 0);
unless ($rc) {
   warn "These are tests which rely upon the driver to tell what the parameter type is for the column.  This means you need to ensure you tell your driver the type of the column in bind_col().\n";
}
Test($rc);

$rc = ODBCTEST::tab_insert_bind($dbh, \@data_no_dates_with_long, 0);
Test($rc);

$rc = ODBCTEST::tab_insert_bind($dbh, \@data_with_dates, 0);
Test($rc);

ODBCTEST::tab_delete($dbh);

