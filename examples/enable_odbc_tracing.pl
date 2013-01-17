# $Id$
# Shows how to enable ODBC API tracing for this Perl script.
# NOTE: the ODBC Driver manager does the actual tracing
use strict;
use warnings;
use DBI;

my $h = DBI->connect($ENV{DBI_DSN}, $ENV{DBI_USER}, $ENV{DBI_PASS},
                     {odbc_trace_file => '/tmp/odbc.trc',
                      odbc_trace => 1});
my $s = $h->prepare('select 1');
$s->execute;
$s->fetch;
$s->fetch;
$h->disconnect;
