It's been reported to me that there is a problem with the data_sources
call for this release of iodbc.  I'm sorry to say that I didn't have time
to patch it or test it, but I know it's been reported to OpenLinkSW.  Please
read the following from Adam (A Curtin [acurtin@ifeng.demon.co.uk]):

Hi,

It said in the DBD::ODBC 0.22 README that you'd like to know of
changes/problems. I've subscribed to dbi-users but haven't received a
confirmation yet, but I thought I could still "cc" it to you.

I'm using a Pentium II machine, RedHat Linux 6.0, perl 5.00503, DBI
1.11, DBD::ODBC 0.22 (just downloaded from CPAN) libiodbc 2.50.3 (as
distributed with DBD::ODBC), MyODBC 2.50.28, MySQL 3.23.7-alpha.

What a lot of software for such a simple job!

Anyway, I was getting segfaults in test 02/13 (data_sources). The
problem occurs from the command line too, i.e.

$ perl -MDBI -e "print DBI->data_sources('ODBC')"

I'm a good programmer but new to perl modules, but anyway I poked around
in ODBC.xs and found that the return values of dsn_length and
description_length from SQLDataSources were junk. Looking in
libiodbc/info.c at the code, it *NEVER SETS* these values! odbctest
works only because it doesn't use them ...

I had a look at unixODBC and it sets them OK. I've reported this just
now to iodbc@openlinksw.com, dunno what they're like at dealing with
reports like this. Frankly I'm astonished such a bug can still be in
there!

Anyway, fixing those values got data_sources in ODBC.xs through the
function and returning, but it crashed somewhere after the return. I
don't know how to debug that, but ...

I experimented putting XSRETURN(0) later and later down the function,
and changed the while(1) to while(0), etc. Eventually it seemed
something else in SQLDataSources was causing the problem, but what it
eventually turned out to be was that you're passing (... dsn+9,
sizeof(dsn) ...) when of course it should be (... dsn_9, sizeof(dsn)-9
...).

After that I didn't get the segfaults, although the tests still give
errors, something to do with AutoCommit not being implemented in the
driver. Is that OK?

I just can't understand why I'm the first to find this ...

I hope this report is of some help.

Adam.
-- 
I've been worng before


