#!perl -w
# $Id$

use Test::More tests => 5;

require DBI;
require_ok('DBI');

import DBI;
pass("import DBI");

$switch = DBI->internal;
is(ref $switch, 'DBI::dr', "DBI->internal is DBI::dr");

$drh = DBI->install_driver('ODBC');
is(ref $drh, 'DBI::dr', "Install ODBC driver OK");

ok($drh->{Version}, "Version is not empty");

exit 0;
