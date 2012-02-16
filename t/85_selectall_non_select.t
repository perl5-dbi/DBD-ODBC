# $Id$
#
# Test what happens when selectall_* is called with a non select statement
#
use strict;
use warnings;
use DBI;
use Test::More;
use Data::Dumper;

my $has_test_nowarnings = 1;
eval "require Test::NoWarnings";
$has_test_nowarnings = undef if $@;

my $h;

BEGIN {
   if (!defined $ENV{DBI_DSN}) {
      plan skip_all => "DBI_DSN is undefined";
   }
}
END {
    if ($h) {
        local $h->{PrintWarn} = 0;
        local $h->{PrintError} = 0;
        $h->do(q/drop table PERL_DBD_TESTs/);
    }
}

$h = DBI->connect();
unless($h) {
   BAIL_OUT("Unable to connect to the database ($DBI::errstr)\nTests skipped.\n");
   exit 0;
}
$h->{RaiseError} = 1;
$h->{PrintError} = 0;
$h->{PrintWarn} = 0;

my $d;
eval {
     $d = $h->selectall_arrayref(q/create table PERL_DBD_TESTs (a char(10))/);
     ok($d, 'create');
};
ok(!$@, 'no error from selectall_arrayref');
is($h->err, 0, "warning") or note(explain($h->err));
like($h->errstr, qr/no select statement currently executing/,
     'warning string');

Test::NoWarnings::had_no_warnings()
  if ($has_test_nowarnings);

done_testing();
