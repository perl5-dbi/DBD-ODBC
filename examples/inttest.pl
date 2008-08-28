#!perl -w
# $Id$


use strict;
use DBI qw(:sql_types);

my $dbh=DBI->connect() or die "Can't connect";

$dbh->{RaiseError} = 1;
$dbh->{LongReadLen} = 800;
my $dbname = $dbh->get_info(17); # sql_dbms_name
print "Connected to $dbname\n";

eval {
   $dbh->do("drop table foo");
};

$dbh->do("Create table foo (id integer not null primary key, longint bigint)");


my $sth = $dbh->prepare("INSERT INTO FOO (ID, longint) values (?, ?)");
my $sth2 = $dbh->prepare("select id, longint from foo where id = ?");


my @numbers = (
	       4,
	       4,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       12,
	       12,
	       12,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       88,
	       7,
	       7,
	       7,
	       100,
	       100,
	       12,
	       7,
	       183,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       7,
	       114,
	       251,
	       282,
	       281,
	       276,
	       131,
	       284,
	       144,
	       131,
	       144,
	       144,
	       131,
	       284,
	       144,
	       251,
	       284,
	       144,
	       284,
	       3,
	       284,
	       276,
	       284,
	       276,
	       3,
	       284,
	       144,
	       284,
	       7,
	       131,
	       144,
	       284,
	       284,
	       276,
	       131,
	       131,
	       114,
	       122
		     );

my $tmp;
my $i = 0;

while ($i <= $#numbers) {
   $sth->execute($i, $numbers[$i]);
   $i++;
}

print "Inserted $i records.\n";
$i = 0;

while ($i <= $#numbers) {
   $sth2->execute($i);
   my @row = $sth2->fetchrow_array();
   $sth2->finish;
   print "Checking row $row[0] ($row[1])\n";
   if ($numbers[$i] != $row[1]) {
      print "Mismatch @ $i, ", $numbers[$i], " != ", $row[1], ": \n";
   }
   # print "$i: $txtinserted[$i]\n";
   
   $i++;
}

print "Checked $i records\n";
$dbh->disconnect;
