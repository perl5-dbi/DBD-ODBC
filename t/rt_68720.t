# $Id$
# rt 68720
#   ensure you get an error when doing a non-select with selectall_*
# rt 68510
#   add rc to args passed to odbc_err_handler
use DBI;
use strict;
use warnings;
use Test::More ;

$| = 1;
my $dbh;

my $has_test_nowarnings = 1;
eval "require Test::NoWarnings";
$has_test_nowarnings = undef if $@;
my $tests = 3;
$tests += 1 if $has_test_nowarnings;
plan tests => $tests;

BEGIN {
    if (!defined $ENV{DBI_DSN}) {
	plan skip_all => "DB_IDSN is undefined";
    }
}

END {
    if ($dbh) {
	eval {
	    local $dbh->{PrintWarn} = 0;
	    local $dbh->{PrintError} = 0;
	    $dbh->do(q/drop table PERL_DBD_RT_68720/);
	};
    }
    Test::NoWarnings::had_no_warnings() if ($has_test_nowarnings);
}

$dbh = DBI->connect();
unless ($dbh) {
    BAIL_OUT("Unable to connect to the database $DBI::errstr\nTests skipped.\n");
    exit 0;
}
$dbh->{PrintError} = 0;
my ($state, $msg, $native, $rc);

eval {
    $dbh->do(q/drop table PERL_DBD_RT_68720/);
};

$dbh->{odbc_err_handler} = sub {
    ($state, $msg, $native, $rc) = @_;
    #diag "$state, $msg, $native, $rc\n";
    return 0;
};

my $r = $dbh->selectall_arrayref(q/create table PERL_DBD_RT_68720 (a int)/);
is($state, "HY000", "Error state");
is($native, 1, "Error native");
is($rc, -1, "Error ODBC return status");
