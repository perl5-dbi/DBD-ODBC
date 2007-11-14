#!/usr/bin/perl -w -I./t
# $Id$

use Test::More;

$| = 1;

use_ok('ODBCTEST');
use_ok('Data::Dumper');

my $tests;
# to help ActiveState's build process along by behaving (somewhat) if a dsn is not provided
BEGIN {
   $tests = 10;
   if (!defined $ENV{DBI_DSN}) {
      plan skip_all => "DBI_DSN is undefined";
   } else {
      plan tests => $tests;
   }
}

my $dbh = DBI->connect();
unless($dbh) {
   BAILOUT("Unable to connect to the database $DBI::errstr\nTests skipped.\n");
   exit 0;
}
$dbh->{PrintError} = 0;
$dbh->{RaiseError} = 1;
#
# check error handler is called, the right args are passed and the error
# is propagated if the handler returns true
#
my ($errmsg, $errstate, $errnative, $handler_called);
my $handler_return = 1;
sub err_handler {
    ($errstate, $errmsg, $errnative) = @_;
    if (!defined($handler_called)) {
        $handler_called = 1;
    } else {
        $handler_called++;
    }
    #diag "===> state: $errstate\n";
    #diag "===> msg: $errmsg\n";
    #diag "===> nativeerr: $errnative\n";
    return $handler_return;
}
$dbh->{odbc_err_handler} = \&err_handler;
my $evalret = eval {
    # this sql is supposed to be invalid
    my $sth = $dbh->prepare('select * from');
    $sth->execute;
    return 99;
};
my $eval = $@;
#diag "eval returned " . ($evalret ? $evalret : "undef") . "\n";
#diag '$@: ' . ($eval ? $eval : "undef") . "\n";
ok($handler_called >= 1, 'Error handler called');
ok($errstate, 'Error handler called - state seen');
ok($errmsg, 'Error handler called - message seen');
ok($errnative, 'Error handler called - native seen');
ok(!defined($evalret), 'Error handler called - error passed on');
ok($eval, 'Error handler called - error propagated');

#
# check we can reset the error handler (bug in 1.14 prevented this)
#
($errmsg, $errstate, $errnative, $handler_called) =
    (undef, undef, undef, undef);
$dbh->{odbc_err_handler} = undef;
$evalret = eval {
    # this sql is supposed to be invalid
    my $sth = $dbh->prepare('select * from');
    $sth->execute;
    return 99;
};
ok(!defined($handler_called), 'Handler cancelled');

#
# check we can filter error messages in the handler by returning 0 from
# the handler
#
($errmsg, $errstate, $errnative, $handler_called) =
    (undef, undef, undef, undef);
$dbh->{odbc_err_handler} = \&err_handler;
$handler_return = 0;

$evalret = eval {
    # this sql is supposed to be invalid
    my $sth = $dbh->prepare('select * from');
    $sth->execute;
    return 99;
};
$eval = $@;
ok(!$eval, 'Handler filtered all messages');
    
$dbh->disconnect;
      

exit 0;
# get rid of use once warnings
print $DBI::errstr;
