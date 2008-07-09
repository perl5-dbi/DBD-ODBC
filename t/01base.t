#!perl -w
# $Id$

use Test::More tests => 6;

require DBI;
require_ok('DBI');

import DBI;
pass("import DBI");

$switch = DBI->internal;
is(ref $switch, 'DBI::dr', "DBI->internal is DBI::dr");

eval {
    $drh = DBI->install_driver('ODBC');
};
my $ev = $@;
diag($ev) if ($ev);
ok(!$ev, 'install ODBC');

SKIP: {
    skip "driver could not be loaded", 2 if $ev;

    is(ref $drh, 'DBI::dr', "Install ODBC driver OK");

    ok($drh->{Version}, "Version is not empty");
}
exit 0;
