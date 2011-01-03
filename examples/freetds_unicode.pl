#
# $Id$
#
# insert Unicode data into MS SQL Server with freeTDS (0.83-dev dated Dec 2010).
#
# odbc.ini:
# [freetds8c]
#  Driver          = FreeTDS8
#  Description     = freetds connection SQL Server Express
#  Trace           = No
#  # ServerName refers to freetds.conf file
#  ServerName = baugi
#
# freetds.conf:
#  [baugi]
#  host = baugi.easysoft.local
#  instance = SQLEXPRESS
#  tds version = 7.2
#  #client-charset = UTF-8
#

use DBI;
use strict;
use warnings;
use Data::Dumper;

# when printing UTF-8 to windows console you need to
# chcp 65001
# ensure you are not using raster fonts
# and set :unix:utf8 on STDOUT
#
#binmode(STDOUT, ":unix:utf8");
my $h = DBI->connect('dbi:ODBC:freetds8c','sa','easysoft');

eval {$h->do(q/drop table mje/)};

$h->do(q/create table mje (a nchar(20))/);

my $s = $h->prepare(q/insert into mje values(?)/);
$s->bind_param(1, "AA\x{20ac}AA"
                   # it does not matter it you use SQL_WVARCHAR or SQL_VARCHAR
                   , DBI::SQL_WVARCHAR # the default for -u builds
              );
$s->execute;

# ChopBlanks does not work in selectall_arrayref
# my $x = $h->selectall_arrayref(q/select * from mje/, {ChopBlanks => 1});
$s = $h->prepare(q/select * from mje/, {ChopBlanks => 1}); # ChopBlanks does not work here
$s->execute;
# ChopBlanks did not work on unicode data in DBD::ODBC until 1.28_1
$s->{ChopBlanks} = 1;           # ChopBlanks works here
my $x = $s->fetchall_arrayref;
print Dumper($x);
#print $x->[0]->[0], "\n";
