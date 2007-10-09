#!/usr/bin/perl -w
use strict;
use warnings;
use DBI;
use Data::Dumper;
use Getopt::Std;

my %opts;

sub usage
{
    print <<'EOF';
  Usage:
    rtcpan_28821.pl -d dsn -u username -p password

EOF
    return 'invalid command line';
}

getopt('d:u:p:', \%opts);

die usage() if (!defined($opts{d}) || !defined($opts{u}) || !defined($opts{p}));
my $h = DBI->connect("dbi:ODBC:$opts{d}", $opts{u}, $opts{p});
eval {
    $h->do(q/drop table rtcpan28821/);
};
$h->do(q/create table rtcpan28821 (a date)/);

my $df = $h->selectall_arrayref(q/select * from v$nls_parameters where parameter
 = 'NLS_DATE_FORMAT'/);
# got DD-MON-RR from my Oracle XE initially
# DD - Day of month (1-31)
# MON - Abbreviated name of month
# RR - Lets you store 20th century dates in the 21st century using only two digits
print Dumper($df);
$df = $h->selectall_arrayref(q/select name,type,value,isdefault from v$parameter where name = 'nls_date_format'/);
print Dumper($df);


# when the default NLS_DATE_FORMAT contains RR it always seems to fail
# with Oracle and MS Oracle driver
# (which incidentally support SQLDescribeParam) which causes DBD::ODBC to
# bind the dates as SQL_CHAR, SQL_TYPE_TIMESTAMP, len=9, dd=0, BufferLength=9
#
# All dates/times seem to work with the Easysoft driver as by default it does
# not supply SQLDescribeParam so DBD::OBDC always uses char binding.
#

do_it('23-MAR-62');
$h->do(q|alter session set nls_date_format='YYYY/MM/DD'|);
do_it('2007/03/23');
sub do_it {
    my $date = shift;

    my $s = $h->prepare(q/insert into rtcpan28821 values(?)/);
    $s->execute($date);
    my $r = $h->selectall_arrayref(q/select * from rtcpan28821/);
    print Dumper($r);
}
