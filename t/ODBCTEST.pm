#
# Package ODBCTEST
# 
# This package is a common set of routines for the DBD::ODBC tests.
# This is a set of routines to create, drop and test for existance of
# a table for a given DBI database handle (dbh).
#
# This set of routines currently depends greatly upon some ODBC meta-data.
# The meta data required is the driver's native type name for various ODBC/DBI
# SQL types.  For example, SQL_VARCHAR would produce VARCHAR2 under Oracle and TEXT
# under MS-Access.  This uses the function SQLGetTypeInfo.  This is obtained via
# the DBI C<func> method, which is implemented as a call to the driver.  In this case,
# of course, this is the DBD::ODBC.
#
# the SQL_TIMESTAMP may be dubious on many platforms, but SQL_DATE was not supported
# under Oracle, MS SQL Server or Access.  Those are pretty common ones.
#

require 5.004;
{
    package ODBCTEST;

    use DBI qw(:sql_types);

    $VERSION = '0.01';
    $table_name = "PERL_DBD_TEST";

    %TestFieldInfo = (
	'A' => [SQL_SMALLINT,SQL_BIGINT, SQL_TINYINT, SQL_NUMERIC, SQL_DECIMAL, SQL_FLOAT, SQL_REAL],
	'B' => [SQL_VARCHAR, SQL_CHAR],
	'C' => [SQL_LONGVARCHAR],
	'D' => [SQL_DATE, SQL_TIMESTAMP],
    );

    sub get_type_for_column {
	my $dbh = shift;
	my $column = shift;

	my $type;
	my @row;
	my $sth;
	foreach $type (@{ $TestFieldInfo{$column} }) {
	    $sth = $dbh->func($type, GetTypeInfo);
	    # may not correct behavior, but get the first compat type
	    @row = $sth->fetchrow();
	    $sth->finish();
	    last if @row;
	}
	die "Unable to find a suitable test type for field $column"
	    unless @row;
	# warn join(", ",@row);
	return @row;
    }
    sub tab_create {
	my $dbh = shift;
	$dbh->{PrintError} = 0;
	eval {
	    $dbh->do("DROP TABLE $table_name");
	};
	$dbh->{PrintError} = 1;

	# trying to use ODBC to tell us what type of data to use,
	# instead of the above.
	my $fields = undef;
	my $f;
	foreach $f (sort keys %TestFieldInfo) {
	    # print "$f: $TestFieldInfo{$f}\n";
	    $fields .= ", " unless !$fields;
	    $fields .= "$f ";
	    # print "-- $fields\n";
	    my @row = get_type_for_column($dbh, $f);
	    $fields .= $row[0];
	    if ($row[5]) {
		$fields .= "($row[2])"	 if ($row[5] =~ /LENGTH/i);
		$fields .= "($row[2],0)" if ($row[5] =~ /PRECISION,SCALE/i);
	    }
	    # print "-- $fields\n";
	}
	print "Using fields: $fields\n";
	$dbh->do("CREATE TABLE $table_name ($fields)");
    }


    sub tab_delete {
	my $dbh = shift;
	$dbh->do("DELETE FROM $table_name");
    }

    sub tab_exists {
	my $dbh = shift;
	my (@rows, @row, $rc);

	$rc = -1;

	unless ($sth = $dbh->table_info()) {
	    print "Can't list tables: $DBI::errstr\n";
	    return -1;
	}
	# TABLE_QUALIFIER,TABLE_OWNER,TABLE_NAME,TABLE_TYPE,REMARKS
	while ($row = $sth->fetchrow_hashref()) {
	    # XXX not fully true.  The "owner" could be different.  Need to check!
	    # In Oracle, testing $user against $row[1] works, but does NOT in SQL Server.
	    # SQL server returns the device and something else I haven't quite taken the time
	    # to figure it out, since I'm not a SQL server expert.  Anyone out there?
	    # (mine returns "dbo" for the owner on ALL my tables.  This is obviously something
	    # significant for SQL Server...one of these days I'll dig...
	    if (($table_name eq uc($row->{TABLE_NAME}))) {
		# and (uc($user) eq uc($row[1]))) 
		# qeDBF driver returns null for TABLE_OWNER
		my $owner = $row->{TABLE_OWNER} || '(unknown owner)';
		print "$owner.$row->{TABLE_NAME}\n";
		$rc = 1;
		last;
	    }
	}
	$sth->finish();
	$rc;
    }
	
    1;
}

