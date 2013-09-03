# $Id$
#
# Test type_info
#
use strict;
use warnings;
use DBI;
use Test::More;
use Data::Dumper;

my $has_test_nowarnings = 1;
eval "require Test::NoWarnings";
$has_test_nowarnings = undef if $@;

BEGIN {
   if (!defined $ENV{DBI_DSN}) {
      plan skip_all => "DBI_DSN is undefined";
   }
}

my $h = DBI->connect();
unless($h) {
   BAIL_OUT("Unable to connect to the database ($DBI::errstr)\nTests skipped.\n");
   exit 0;
}
$h->{RaiseError} = 1;
$h->{PrintError} = 0;

# test type_info('%','','') which should return catalogs only
my $s = $h->table_info('%', '', '');
my $r = $s->fetchall_arrayref;
if ($r && scalar(@$r)) {        # assuming we get something back
    my $pass = 1;
    foreach my $row (@$r) {
        if (!defined($row->[0])) {
            $pass = 0;
            diag("Catalog is not defined");
        }

        if (defined($row->[1])) {
            $pass = 0;
            diag("Schema is defined as $row->[1]");
        }

        if (defined($row->[2])) {
            $pass = 0;
            diag("Table is defined as $row->[2]");
        }
    }
    ok($pass, "catalogs only") or diag(Dumper($r));
}

# test type_info('',%'','') which should return schema only
$s = $h->table_info('', '%', '');
$r = $s->fetchall_arrayref;
if ($r && scalar(@$r)) {        # assuming we get something back
    my $pass = 1;
    foreach my $row (@$r) {
        if (defined($row->[0])) {
            $pass = 0;
            diag("Catalog is defined as $row->[0]");
        }

        if (!defined($row->[1])) {
            $pass = 0;
            diag("Schema is not defined");
        }

        if (defined($row->[2])) {
            $pass = 0;
            diag("Table is defined as $row->[2]");
        }
    }
    ok($pass, "schema only") or diag(Dumper($r));
}

# test type_info('','','', '%')  which should return table types only
$s = $h->table_info('', '', '', '%');
$r = $s->fetchall_arrayref;
if ($r && scalar(@$r)) {        # assuming we get something back
    my $pass = 1;
    foreach my $row (@$r) {
        if (defined($row->[0])) {
            $pass = 0;
            diag("Catalog is defined as $row->[0]");
        }

        if (defined($row->[1])) {
            $pass = 0;
            diag("Schema is defined as $row->[1]");
        }

        if (defined($row->[2])) {
            $pass = 0;
            diag("Table is defined as $row->[2]");
        }

        if (!defined($row->[3])) {
            $pass = 0;
            diag("table type is not defined");
        }
    }
    ok($pass, "table type only") or diag(Dumper($r));
}


Test::NoWarnings::had_no_warnings()
  if ($has_test_nowarnings);

done_testing();
