#!/usr/bin/end perl
use strict;
use warnings;
use DBI;

# Whatever args you pass to tables are just passed on the ODBC SQLTables call
# so this does whatever ODBC says and what your ODBC driver dictates.
my $h = DBI->connect();

# deprecated:
my @tables = $h->tables;

print "all tables and views:\n", join("\n", @tables) , "\n";
# "master"."dbo"."DBD_ODBC_LOB_TEST"
# "master"."dbo"."long_bind"
# "master"."dbo"."mej"
print "\n";

# print a list of catalogs
my @catalogs = $h->tables('%', '', '');
print "all catalogs:\n", join("\n", @catalogs) , "\n";
# "master".
# "msdb".
# "tempdb".
print "\n";

my @schemas = $h->tables('', '%', '');
print "all schemas:\n", join("\n", @schemas), "\n";
# "dbo"
# "INFORMATION_SCHEMA"
# "sys"
print "\n";

my @types = $h->tables('', '', '', '%');
print "all types:\n", join("\n", @types), "\n";
# "dbo"
# "INFORMATION_SCHEMA"
# "sys"
print "\n";

my $tables = $h->table_info(undef, undef, undef, 'TABLE');
DBI::dump_results($tables);
