#!perl -w
# $Id$


use strict;
use DBI;

my $dbh1 = DBI->connect() or die "Can't connect 1\n$DBI::errstr\n";

my $dbh2 = DBI->connect() or die "Can't connect 2\n$DBI::errstr\n";

$dbh1->disconnect;
$dbh2->disconnect;