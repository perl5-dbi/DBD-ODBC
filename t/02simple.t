#!/usr/bin/perl -I./t
$| = 1;

use DBI qw(:sql_types);
use ODBCTEST;

print "1..$tests\n";

print "ok 1\n";

print " Test 2: connecting to the database\n";
#DBI->trace(2);
my $dbh = DBI->connect() || die "Connect failed: $DBI::errstr\n";
$dbh->{AutoCommit} = 1;

print "ok 2\n";


#### testing a simple select

print " Test 3: create test table\n";
$rc = ODBCTEST::tab_create($dbh);
print "not " unless($rc);
print "ok 3\n";

print " Test 4: check existance of test table\n";
my $rc = 0;
$rc = ODBCTEST::tab_exists($dbh);
print "not " unless($rc >= 0);
print "ok 4\n";

print " Test 5: insert test data\n";
$rc = ODBCTEST::tab_insert($dbh);
print "not " unless($rc);
print "ok 5\n";

print " Test 6: select test data\n";
$rc = tab_select($dbh);
print "not " unless($rc);
print "ok 6\n";

print " Tests 7,8: test LongTruncOk\n";
$rc = undef;
$dbh->{LongReadLen} = 50;
$dbh->{LongTruncOk} = 1;
$dbh->{PrintError} = 0;
$rc = select_long($dbh);
print "not " unless($rc);
print "ok 7\n";

$dbh->{LongTruncOk} = 0;
$rc = select_long($dbh);
print "not " if ($rc);
print "ok 8\n";

print " Test 9: test ColAttributes\n";
my $sth = $dbh->prepare("SELECT * FROM $ODBCTEST::table_name ORDER BY COL_A");

if ($sth) {
	$sth->execute();
	my $colcount = $sth->func(1, 0, ColAttributes); # 1 for col (unused) 0 for SQL_COLUMN_COUNT
	print "Column count is: $colcount\n";
	my ($coltype, $colname, $i, @row);
	my $is_ok = 0;
	for ($i = 1; $i <= $colcount; $i++) {
		# $i is colno (1 based) 2 is for SQL_COLUMN_TYPE, 1 is for SQL_COLUMN_NAME
		$coltype = $sth->func($i, 2, ColAttributes);
		$colname = $sth->func($i, 1, ColAttributes);
		print "$i: $colname = $coltype\n";
 		++$is_ok if grep { $coltype == $_ } @{$ODBCTEST::TestFieldInfo{$colname}};
	}
	print "not " unless $is_ok == $colcount;
	print "ok 9\n";
	
	$sth->finish;
}
else {
	print "not ok 9\n";
}

print " Test 10: test \$DBI::err\n";
$dbh->{RaiseError} = 0;
$dbh->{PrintError} = 0;
#
# some ODBC drivers will prepare this OK, but not execute.
# 
$sth = $dbh->prepare("SELECT XXNOTCOLUMN FROM $ODBCTEST::table_name");
$sth->execute() if $sth;
print "not " if (length($DBI::err) < 1);
print "ok 10\n";

print " Test 11: test date values\n";
$sth = $dbh->prepare("SELECT COL_D FROM $ODBCTEST::table_name WHERE COL_D > {d '1998-05-13'}");
$sth->execute();
my $count = 0;
while (@row = $sth->fetchrow) {
	$count++ if ($row[0]);
	# print "$row[0]\n";
}
print "not " if $count != 1;
print "ok 11\n";

print " Test 12: test group by queries\n";
$sth = $dbh->prepare("SELECT COL_A, COUNT(*) FROM $ODBCTEST::table_name GROUP BY COL_A");
$sth->execute();
$count = 0;
while (@row = $sth->fetchrow) {
	$count++ if ($row[0]);
	print "$row[0], $row[1]\n";
}
print "not " if $count == 0;
print "ok 12\n";

$rc = ODBCTEST::tab_delete($dbh);

# Note, this test will fail if no data sources defined or if
# data_sources is unsupported.
print " Test 13: test data_sources\n";
my @data_sources = DBI->data_sources('ODBC');
print "Data sources:\n\t", join("\n\t",@data_sources),"\n\n";
print "not " if ($#data_sources < 0);
print "ok 13\n";

print " Test 14: test ping method\n";
print "not " unless $dbh->ping;
print "ok 14\n";

print " Test 15: test storing of DBH parameter\n";
if ($dbh->{odbc_ignore_named_placeholders}) {
   print "Attrib not 0 to start (", $dbh->{odbc_ignore_named_placeholders}, ")\nnot ";
} else {
   $dbh->{odbc_ignore_named_placeholders} = 1;
   print "Attrib not true (", $dbh->{odbc_ignore_named_placeholders}, ")\nnot " unless $dbh->{odbc_ignore_named_placeholders};
}
print "ok 15\n";

print "ok 16\n";

print " Test 17: test get_info\n";
my $dbname;
$dbname = $dbh->get_info(17); # SQL_DBMS_NAME
print " connected to $dbname\n";
print "\nnot " unless (defined($dbname) && $dbname ne '');
print "ok 17\n";

BEGIN {$tests = 17;}
exit(0);

sub tab_select
{
    my $dbh = shift;
    my @row;
    my $rowcount = 0;

    $dbh->{LongReadLen} = 1000;

    my $sth = $dbh->prepare("SELECT * FROM $ODBCTEST::table_name ORDER BY COL_A")
		or return undef;
    $sth->execute();
    while (@row = $sth->fetchrow())	{
	print "$row[0]|$row[1]|$row[2]|\n";
	++$rowcount;
	if ($rowcount != $row[0]) {
	    print "Basic retrieval of rows not working!\nRowcount = $rowcount, while retrieved value = $row[0]\n";
	    $sth->finish;
	    return 0;
	}
    }
    $sth->finish();
    
    $sth = $dbh->prepare("SELECT COL_A,COL_C FROM $ODBCTEST::table_name WHERE COL_A>=4")
	   or return undef;
    $sth->execute();
    while (@row = $sth->fetchrow()) {
	if ($row[0] == 4) {
	    if ($row[1] eq $ODBCTEST::longstr) {
		print "retrieved ", length($ODBCTEST::longstr), " byte string OK\n";
	    } else {
		print "Basic retrieval of longer rows not working!\nRetrieved value = $row[0]\n";
		return 0;
	    }
	} elsif ($row[0] == 5) {
	    if ($row[1] eq $ODBCTEST::longstr2) {
		print "retrieved ", length($ODBCTEST::longstr2), " byte string OK\n";
	    } else {
		print "Basic retrieval of row longer than 255 chars not working!",
						"\nRetrieved ", length($row[1]), " bytes instead of ", 
						length($ODBCTEST::longstr2), "\nRetrieved value = $row[1]\n";
		return 0;
	    }
	}
    }

    return 1;
}


sub select_long
{
	my $dbh = shift;
	my @row;
	my $sth;
	my $rc = undef;
	
	$dbh->{RaiseError} = 1;
	$sth = $dbh->prepare("SELECT COL_A,COL_C FROM $ODBCTEST::table_name WHERE COL_A=4");
	if ($sth) {
		$sth->execute();
		eval {
			while (@row = $sth->fetchrow()) {
			}
		};
		$rc = 1 unless ($@) ;
	}
	$rc;
}

__END__




