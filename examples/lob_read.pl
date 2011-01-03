# $Id$
#
# Example of DBD::ODBC's lob_read
#
#use Log::Log4perl qw(:easy);
#Log::Log4perl->easy_init($DEBUG);
#use DBIx::Log4perl;
use DBI;
use strict;
use warnings;

#$h = DBIx::Log4perl->connect("dbi:ODBC:baugi","sa","easysoft". {PrintError => 1});
my $h = DBI->connect("dbi:ODBC:baugi","sa","easysoft",
                     {PrintError => 1, RaiseError => 1, PrintWarn => 1});
my $s = $h->prepare(q{select 'frederick'});
$s->execute;
$s->bind_col(1, undef, {TreatAsLOB=>1});
$s->fetch;
# SQL_SUCCESS = 0
# SQL_SUCCESS_WITH_INFO = 1
# SQL_NO_DATA = 100
my $len;
$s->{RaiseError} = 0;
while($len = $s->odbc_lob_read(1, \my $x, 8, {Type => 999})) {
    print "len=$len, x=$x\n";
}
print "len at end = $len\n";
my $x;
$len = $s->odbc_lob_read(1, \$x, 8);
$len = $s->odbc_lob_read(1, \$x, 8);
