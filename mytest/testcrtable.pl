#!/usr/bin/perl -w -I./t
# $Id$


# use strict;
use DBI qw(:sql_types);

my (@row);

my $dbh = DBI->connect()
	  or exit(0);
# ------------------------------------------------------------


my %TypeTests = (
		 'SQL_ALL_TYPES' => 0,
		 'SQL_VARCHAR' => SQL_VARCHAR,
		 'SQL_CHAR' => SQL_CHAR,
		 'SQL_INTEGER' => SQL_INTEGER,
		 'SQL_SMALLINT' => SQL_SMALLINT,
		 'SQL_NUMERIC' => SQL_NUMERIC,
		 'SQL_LONGVARCHAR' => SQL_LONGVARCHAR,
		 'SQL_LONGVARBINARY' => SQL_LONGVARBINARY,
		);

my $ret; 
print "\nInformation for DBI_DSN=$ENV{'DBI_DSN'}\n\t", $dbh->get_info(17), "\n";
my $SQLInfo;

print "Listing all types\n";
my $sql = "create table PERL_TEST (\n";
my $icolno = 0;
$sth = $dbh->func(0, GetTypeInfo);
if ($sth) {
   my $colcount = $sth->func(1, 0, ColAttributes); # 1 for col (unused) 0 for SQL_COLUMN_COUNT
   # print "Column count is $colcount\n";
   my $i;
   my @coldescs = ();
   # column 0 should be an error/blank
   for ($i = 0; $i <= $colcount; $i++) {
      my $stype = $sth->func($i, 2, ColAttributes);
      my $sname = $sth->func($i, 1, ColAttributes);
      push(@coldescs, $sname);
   }	
	
   my @cols = ();
   while (@row = $sth->fetchrow()) {
	if (!($row[0] =~ /auto/)) {

	  my $tmp = " COL_$icolno $row[0]";

	  $tmp .= "($row[2])" if ($row[5] =~ /length/ );
	  push(@cols, $tmp); 
	}
	 $icolno++;
	 print "$row[0], ",
	  &nullif($row[1]), ", ", 
	  &nullif($row[2]), ", ", 
	  &nullif($row[3]), ", ", 
	  &nullif($row[4]), ", ", 
	  &nullif($row[5]), ", ", 
	  &nullif($row[6]), ", ", 
	"\n";
   }
   $sql .= join("\n , ", @cols) . ")\n";
   $sth->finish;
}	
print $sql;
eval {
	$dbh->do("drop table PERL_TEST");
};

$dbh->do($sql);

my @tables = $dbh->tables;

my @mtable = grep(/PERL_TEST/, @tables);
my ($catalog, $schema, $table) = split(/\./, $mtable[0]);
$catalog =~ s/"//g;
$schema =~ s/"//g;
$table =~ s/"//g;
$table="PERL_DBD_TEST";
print "Getting column info for: $catalog, $schema, $table\n";
my $sth = $dbh->column_info(undef, undef, $table, undef);
my @row;

print join(', ', @{$sth->{NAME}}), "\n";
while (@row = $sth->fetchrow_array) {

   # join prints nasty warning messages with -w. There's gotta be a better way...
   foreach (@row) { $_ = "" if (!defined); }

   print join(", ", @row), "\n";
}

$dbh->disconnect();

sub nullif ($) {
   my $val = shift;
   $val ? $val : "(null)";
}
