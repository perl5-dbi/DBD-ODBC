#!perl -w
# $Id$

use DBI qw(:sql_types);
use Data::Dumper;
use strict;

my $dbh = DBI->connect( "dbi:ODBC:Northwind", "", "",
     {RaiseError => 1, PrintError => 1, AutoCommit => 1} );

my $sth = $dbh->prepare( "select * from Customers" );
$sth->execute( );
print "Table contains: " . $sth->{NUM_OF_FIELDS} . " columns.\n";
print "Column names are:\n\t" . join( "\n\t", @{$sth->{NAME}}, "" );

$sth->cancel;
$sth->finish;


$sth = $dbh->prepare( "select count(*) from Customers" );
$sth->execute( );
print "\nTable contains: " . $sth->{NUM_OF_FIELDS} . " columns.\n";
print "Column names are:\n\t" . join( "\n\t", @{$sth->{NAME}});

$sth->cancel;
$sth->finish;

$sth = $dbh->prepare( "select count(*) from Customers where CompanyName='FOO'" );
$sth->execute( );
print "\nTable contains: " . $sth->{NUM_OF_FIELDS} . " columns.\n";
print "Column names are:\n\t" . join( "\n\t", @{$sth->{NAME}});

$sth->cancel;
$sth->finish;


$sth = $dbh->prepare( "select count(*) from Customers where Company1Name='FOO'" );
$sth->execute( );
print "\nTable contains: " . $sth->{NUM_OF_FIELDS} . " columns.\n";
print "Column names are:\n\t" . join( "\n\t", @{$sth->{NAME}});

$sth->cancel;
$sth->finish;

$dbh->disconnect;
