#!/usr/bin/perl -I./t
$| = 1;


my $tests = 25;
# to help ActiveState's build process along by behaving (somewhat) if a dsn is not provided
BEGIN {
   unless (defined $ENV{DBI_DSN}) {
      print "1..0 # Skipped: DBI_DSN is undefined\n";
      exit;
   }
}

use DBI qw(:sql_types);
use ODBCTEST;
use Data::Dumper;
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
unless ($dbname =~ /Microsoft SQL Server/i) {
   print "1..0 # Skipped: Microsoft SQL Server tests not supported using ", $dbname, "\n";;
   exit 0;
} 

print "1..$tests\n";

# the times chosen below are VERY specific to NOT cause rounding errors, but may cause different
# errors on different versions of SQL Server.
#
my @data = (
    [undef, "z" x 13 ],
    ["2001-01-01 01:01:01.110", "a" x 12],   # "aaaaaaaaaaaa"
    ["2002-02-02 02:02:02.123", "b" x 114],
    ["2003-03-03 03:03:03.333", "c" x 251],
    ["2004-04-04 04:04:04.443", "d" x 282],
    ["2005-05-05 05:05:05.557", "e" x 131]
);

eval {
   local $dbh->{PrintError} = 0;
   $dbh->do("DROP TABLE PERL_DBD_TABLE1");
};

$dbh->{RaiseError} = 1;
$dbh->{LongReadLen} = 800;

my @types = (SQL_TYPE_TIMESTAMP, SQL_TIMESTAMP);
my $type;
my @row;
foreach $type (@types) {
   my $sth = $dbh->func($type, "GetTypeInfo");
   if ($sth) {
      @row = $sth->fetchrow();
      $sth->finish();
      last if @row;
   } else {
       # warn "Unable to get type for type $type\n";
   }	
}
die "Unable to find a suitable test type for date field\n"
   unless @row;

my $datetype = $row[0];
$dbh->do("CREATE TABLE PERL_DBD_TABLE1 (i INTEGER, time $datetype, str VARCHAR(4000))");


# Insert records into the database:
my $sth1 = $dbh->prepare("INSERT INTO PERL_DBD_TABLE1 (i,time,str) values (?,?,?)");
for (my $i=0; $i<@data; $i++) {
    my ($time,$str) = @{$data[$i]};
    print "Inserting:  $i, ";
    print  $time if (defined($time));
    print " string length " . length($str) . "\n";
    $sth1->bind_param (1, $i,    SQL_INTEGER);
    $sth1->bind_param (2, $time, SQL_TIMESTAMP);
    $sth1->bind_param (3, $str,  SQL_LONGVARCHAR);
    $sth1->execute  or die ($DBI::errstr);
}

# Retrieve records from the database, and see if they match original data:
my $sth2 = $dbh->prepare("SELECT i,time,str FROM PERL_DBD_TABLE1");
$sth2->execute  or die ($DBI::errstr);
my $iErrCount = 0;
while (my ($i,$time,$str) = $sth2->fetchrow_array()) {
    print "Retrieving: $i, ";
    print $time if (defined($time));
    print " string length ".length($str)."\t";
    if ((defined($time) && $time ne $data[$i][0]) || defined($time) != defined($data[$i][0])) {
       print "!time  ";
       $iErrCount++;
    }
    
    if ($str  ne $data[$i][1]) {
       print "!string" ;
       $iErrCount++;
    }
    print "\n";
}
Test($iErrCount == 0);


eval {
   local $dbh->{RaiseError} = 0;
   $dbh->do("DROP TABLE PERL_DBD_TABLE1");
};

my $sql = 'CREATE TABLE #PERL_DBD_TABLE1 (id INT PRIMARY KEY, val VARCHAR(4))';
$dbh->do($sql);
# doesn't work with prepare, etc...hmmm why not?
# $sth = $dbh->prepare($sql);
# $sth->execute;
# $sth->finish;

$sth = $dbh->prepare("INSERT INTO #PERL_DBD_TABLE1 (id, val) VALUES (?, ?)");
$sth2 = $dbh->prepare("INSERT INTO #PERL_DBD_TABLE1 (id, val) VALUES (?, ?)");
my @data2 = (undef, 'foo', 'bar', 'blet', undef);
my $i = 0;
my $val;
foreach $val (@data2) {
   $sth2->execute($i++, $val);
}
$i = 0;
$sth = $dbh->prepare("Select id, val from #PERL_DBD_TABLE1");
$sth->execute;
$iErrCount = 0;
while (@row = $sth->fetchrow_array) {
   unless ((!defined($row[1]) && !defined($data2[$i])) || ($row[1] eq $data2[$i])) {
      $iErrCount++ ;
      print "$row[1] ne $data2[$i]\n";
   }
   $i++;
}

Test($iErrCount == 0);
print STDERR "Please upgrade your ODBC drivers to the latest SQL Server drivers available.\n" if ($iErrCount != 0);

$dbh->{PrintError} = 0;
eval {$dbh->do("DROP TABLE PERL_DBD_TABLE1");};
eval {$dbh->do("CREATE TABLE PERL_DBD_TABLE1 (i INTEGER)");};

eval {$dbh->do("DROP PROCEDURE PERL_DBD_PROC1");};
eval {$dbh->do("CREATE PROCEDURE PERL_DBD_PROC1 \@inputval int AS ".
                "INSERT INTO PERL_DBD_TABLE1 VALUES (\@inputval); " .   
			"	return \@inputval;");};


$sth1 = $dbh->prepare ("{? = call PERL_DBD_PROC1(?) }");
my $output = undef;
$i = 1;
$iErrCount = 0;
while ($i < 4) {
   $sth1->bind_param_inout(1, \$output, 50, DBI::SQL_INTEGER);
   $sth1->bind_param(2, $i, DBI::SQL_INTEGER);

   $sth1->execute();
   print "$output";
   if ($output != $i) {
      $iErrCount++;
      print " error!";
   }
   print "\n";
   $i++;
}

Test($iErrCount == 0);
$iErrCount = 0;
eval {$dbh->do("DROP PROCEDURE PERL_DBD_PROC1");};
my $proc1 =
    "CREATE PROCEDURE PERL_DBD_PROC1 (\@i int, \@result int OUTPUT) AS ".
    "BEGIN ".
    "    SET \@result = \@i+1;".
    "END ";
print "$proc1\n";
$dbh->do($proc1);

# $dbh->{PrintError} = 1;
$sth1 = $dbh->prepare ("{call PERL_DBD_PROC1(?, ?)}");
$i = 12;
$output = undef;
$sth1->bind_param(1, $i, DBI::SQL_INTEGER);
$sth1->bind_param_inout(2, \$output, 100, DBI::SQL_INTEGER);
$sth1->execute;
Test($i == $output-1);

$iErrCount = 0;
$sth = $dbh->prepare("select * from PERL_DBD_TABLE1 order by i");
$sth->execute;
$i = 1;
while (@row = $sth->fetchrow_array) {
   if ($i != $row[0]) {
      print join(', ', @row), " ERROR!\n";
      $iErrCount++;
   }
   $i++;
}


Test($iErrCount == 0);

eval {$dbh->do("DROP TABLE PERL_DBD_TABLE1");};
eval {$dbh->do("CREATE TABLE PERL_DBD_TABLE1 (d DATETIME)");};
$sth = $dbh->prepare ("INSERT INTO PERL_DBD_TABLE1 (d) VALUES (?)");
$sth->bind_param (1, undef, SQL_TYPE_TIMESTAMP);
$sth->execute();
$sth->bind_param (1, "2002-07-12 05:08:37.350", SQL_TYPE_TIMESTAMP);
$sth->execute();
$sth->bind_param (1, undef, SQL_TYPE_TIMESTAMP);
$sth->execute();

$iErrCount = 0;
$sth2 = $dbh->prepare("select * from PERL_DBD_TABLE1 where d is not null");
$sth2->execute;
while (@row = $sth2->fetchrow_array) {
   $iErrCount++ if ($row[0] ne "2002-07-12 05:08:37.350");
   print join(", ", @row), "\n";
}
Test($iErrCount == 0);

eval {$dbh->do("DROP TABLE PERL_DBD_TABLE1");};
eval {$dbh->do("DROP PROCEDURE PERL_DBD_PROC1");};

eval {$dbh->do("CREATE TABLE PERL_DBD_TABLE1 (i INTEGER, j integer)");};
$proc1 = <<EOT;
CREATE PROCEDURE PERL_DBD_PROC1 (\@i INT) AS
DECLARE \@result INT;
BEGIN
   SET \@result = \@i;
   IF (\@i = 99)
      BEGIN
	 UPDATE PERL_DBD_TABLE1 SET i=\@i;
	 SET \@result = \@i + 1;
      END;
   SELECT \@result;
END
EOT
$dbh->{RaiseError} = 0;
eval {$dbh->do($proc1);};
my $sth = $dbh->prepare ("{call PERL_DBD_PROC1 (?)}");
my $success = -1;

$sth->bind_param (1, 99, SQL_INTEGER);
$sth->execute();
$success = -1;
while (my @data = $sth->fetchrow_array()) {($success) = @data;}
Test($success == 100);

$sth->bind_param (1, 10, SQL_INTEGER);
$sth->execute();
$success = -1;
while (my @data = $sth->fetchrow_array()) {($success) = @data;}
Test($success == 10);

$sth->bind_param (1, 111, SQL_INTEGER);
$sth->execute();
$success = -1;
do {
   my @data;
   while (@data = $sth->fetchrow_array()) {
      if ($#data == 0) {
	  ($success) = @data;
      }
   }
} while ($sth->{odbc_more_results});
Test($success == 111);


#
# special tests for even stranger cases...
#
eval {$dbh->do("DROP PROCEDURE PERL_DBD_PROC1");};
$proc1 = <<EOT;
   CREATE PROCEDURE PERL_DBD_PROC1 (\@i INT) AS
   DECLARE \@result INT;
   BEGIN
   SET \@result = \@i;
   IF (\@i = 99)
      BEGIN
	 UPDATE PERL_DBD_TABLE1 SET i=\@i;
	 SET \@result = \@i + 1;
      END;
   IF (\@i > 100)
      BEGIN
	 INSERT INTO PERL_DBD_TABLE1 (i, j) VALUES (\@i, \@i);
	 SELECT i, j from PERL_DBD_TABLE1;
      END;
   SELECT \@result;
   END
EOT

eval {$dbh->do($proc1);};

# set the required attribute and check it.
$dbh->{odbc_force_rebind} = 1;
Test($dbh->{odbc_force_rebind});
$dbh->{odbc_force_rebind} = 0;
Test($dbh->{odbc_force_rebind} == 0);

$sth = $dbh->prepare ("{call PERL_DBD_PROC1 (?)}");
Test($sth->{odbc_force_rebind} == 0);
$success = -1;

$sth->bind_param (1, 99, SQL_INTEGER);
$sth->execute();
$success = -1;
while (my @data = $sth->fetchrow_array()) {($success) = @data;}
Test($success == 100);
  
$sth->bind_param (1, 10, SQL_INTEGER);
$sth->execute();
$success = -1;
while (my @data = $sth->fetchrow_array()) {($success) = @data;}
Test($success == 10);

$sth->bind_param (1, 111, SQL_INTEGER);
$sth->execute();
$success = -1;
do {
   my @data;
   while (@data = $sth->fetchrow_array()) {
      if ($#data == 0) {
	 ($success) = @data;
      } else {
	 print "Data: ", join(',', @data), "\n";
      }
   }
} while ($sth->{odbc_more_results});
Test($success == 111);

# ensure the attribute is automatically set.
# the multiple result sets will trigger this.
Test($sth->{odbc_force_rebind});
   

#
# more special tests
# make sure output params are being set properly when
# multiple result sets are available.  Also, ensure fetchrow_hashref
# works with multiple statements.
#
eval {$dbh->do("DROP PROCEDURE PERL_DBD_PROC1");};
$dbh->do("CREATE PROCEDURE  PERL_DBD_PROC1
\@parameter1 int = 22
AS
	/* SET NOCOUNT ON */
	select 1 as some_data
	select isnull(\@parameter1, 0) as parameter1, 3 as some_more_data
	RETURN(\@parameter1 + 1)");

my $queryInputParameter1 = 2222;
my $queryOutputParameter = 0;
	
$sth = $dbh->prepare('{? = call PERL_DBD_PROC1(?) }');
$sth->bind_param_inout(1, \$queryOutputParameter, 30, { TYPE => DBI::SQL_INTEGER });
$sth->bind_param(2, $queryInputParameter1, { TYPE => DBI::SQL_INTEGER });
					
$sth->execute();

do {
   for(my $rowRef; $rowRef = $sth->fetchrow_hashref('NAME'); )  {
      my %outputData = %$rowRef;
      if (defined($outputData{some_data})) {
	 Test($outputData{some_data} == 1);
	 Test(!defined($outputData{parameter1}));
	 Test(!defined($outputData{some_more_data}));
      } else {
	 Test($outputData{parameter1} == 2222);
	 Test($outputData{some_more_data} == 3);
	 Test(!defined($outputData{some_data}));
      }
      print 'outputData ', Dumper(\%outputData), "\n";
   }
   print "out of for loop\n";
} while($sth->{odbc_more_results});
print "out of while loop\n";
Test($queryOutputParameter == $queryInputParameter1 + 1);

# clean up test table and procedure
eval {$dbh->do("DROP TABLE PERL_DBD_TABLE1");};
eval {$dbh->do("DROP PROCEDURE PERL_DBD_PROC1");};

$dbh->{odbc_async_exec} = 1;
print "odbc_async_exec is: $dbh->{odbc_async_exec}\n";
Test($dbh->{odbc_async_exec});

# not sure if this should be a test.  May have permissions problems, but it's the only sample
# of the error handler stuff I have.
my $testpass = 0;
sub err_handler {
   my ($state, $msg) = @_;
   # Strip out all of the driver ID stuff
   $msg =~ s/^(\[[\w\s]*\])+//;
   print "===> state: $state msg: $msg\n";
   $testpass++;
   return 0;
}
$dbh->{odbc_err_handler} = \&err_handler;

$sth = $dbh->prepare("dbcc TRACESTATUS(-1)");
$sth->execute;
Test($testpass > 0);

$dbh->disconnect;
