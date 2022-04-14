# DBD::ODBC -- DBD module interfacing the ODBC databases.

[![build](https://github.com/perl5-dbi/DBD-ODBC/actions/workflows/action.yml/badge.svg?branch=master)](https://github.com/whindsx/DBD-ODBC/actions)
[![CPAN version](https://badge.fury.io/pl/DBD-ODBC.svg)](http://badge.fury.io/pl/DBD-ODBC)
[![CPANTS](http://cpants.cpanauthors.org/dist/DBD-ODBC.png)](http://cpants.cpanauthors.org/dist/DBD-ODBC)

See [LICENSE-AND-COPYRIGHT](https://metacpan.org/pod/DBD::ODBC#LICENSE-AND-COPYRIGHT)
section in ODBC.pm for usage and distribution rights.

# DEPENDENCIES

    Build, test and install Perl 5 (as per DBI specifications/compatibility)
    It is very important to TEST it and INSTALL it.

    Build, test and install the DBI module (at least DBI 1.609).
    It is very important to TEST it and INSTALL it.

    Remember to *read* the DBD::ODBC.pm POD documentation and the
    DBD::ODBC::Changes.pm POD documentation.

# BUILDING:

  set-up these environment variables:

    DBI_DSN   The dbi data source, e.g. 'dbi:ODBC:YOUR_DSN_HERE'

    DBI_USER  The username to use to connect to the database

    DBI_PASS  The username to use to connect to the database

    ODBCHOME  (Unix only) The dir your driver manager is installed in
              or specify this via -o argument to Makefile.PL

    DBD_ODBC_UNICODE Any value here will set the default install to
                     attempt to turn on UNICODE support.

  If you want UNICODE support on non-Windows platforms specify -u
  switch to Makefile.PL. If you don't want UNICODE support on Windows
  specify the -nou switch to Makefile.PL. On non-Windows platforms all
  UNICODE features will work correctly with the unixODBC driver
  manager.

  If the Makefile.PL finds iODBC and you would prefer to build with
  unixODBC you can use the -x switch to favor unixODBC. This only
  happens in rare case where you have binary packages of both
  installed but you can only have development packages on one
  installed and DBD::ODBC locates iODBC first. This quite often
  happens on Ubuntu and other Debian based Linux systems where an
  incomplete iODBC is often installed.

  perl Makefile.PL

  make                (or nmake/dmake, if VC++ on Win32)

  make test           (or nmake/dmake, if VC++ on Win32)

# TESTING

  make test

  make test TEST_VERBOSE=1   (if any of the t/* tests fail)

  make install               (if the tests look okay)

  Note that the tests currently all pass when using the Microsoft SQL
  server driver up to SQL Server 2008 and many other ODBC drivers on
  Windows and UNIX (too numerous to mention here).

  To run an individual test use:

  prove -vb t/test.t

  If the tests from t/09bind.t fail, that is an indication of lack of
  ODBC Level 2 functionality.  This does not (necessarily) mean that
  your installation is broken.  It does indicate that your ODBC driver
  does not support certain level 2 calls (see below).  All other tests
  should pass, however, since I've tested with a limited number of
  ODBC drivers, I could have something wrong in the test.  Please
  notify me if you have an incompatibility in the other tests.  (For
  example, some Paradox ODBC drivers *could* fail, if they don't
  support long file names, since in Paradox file names are
  tableName.db.).  Please let me know about any incompatibilities you
  encounter. I will say, though, that I may not be able to reproduce
  or fix problems without some help, since I can't possibly install
  all the known ODBC drivers.  Please try to "solve" the problem, in
  addition to discovering it.

  If any of the tests fail and you are using SQL Server and Windows drivers,
  ensure you updated to MDAC 2.7 or later.

# NOTES on ODBC Drivers and Compatibility with DBD-ODBC

  This version utilizes at least one "Level 2" ODBC call
  (SQLDescribeParam).  This *will* affect compatibility with specific
  ODBC drivers.  If the driver is not level 2 (or does not support the
  level 2 function(s) required), then full compatibility will not be
  available. The best thing to do is build DBD::ODBC against an ODBC
  driver manager like unixODBC (http://www.unixodbc.org) as it will
  resolve many incompatibilities between ODBC versions for you.

# IF YOU HAVE PROBLEMS:

  Do not hand edit the generated Makefile unless you are completely
  sure you understand the implications! Always try to make changes via
  the Makefile.PL command line and/or editing the Makefile.PL.

  You should not need to make any changes. If you do *please* let me
  know so that I can try to make it automatic in a later release.

  This software is supported via the dbi-users mailing list.  For more
  information and to keep informed about progress you can join the
  mailing list via http://dbi.perl.org (if you are unable to use the
  web you can subscribe by sending a message to
  dbi-users-subscribe@perl.org.

  Please post details of any problems (or changes you needed to make)
  to dbi-users@perl.org. But note...

  ** IT IS IMPORTANT TO INCLUDE THE FOLLOWING INFORMATION:

  1. A complete log of a all steps of the build, e.g.:

    perl Makefile.PL           (do a make realclean first)

    make

    make test

    make test TEST_VERBOSE=1   (if any tests fail)

  2. Full details of which software you are using, including:

    Perl version (the output of perl -V)

    ODBC Driver and version

    ODBC Driver manager used and version

  It is important to check that you are using the latest version
  before posting. If you're not then I'm *very* likely to simply say
  "upgrade to the latest". You would do yourself a favour by upgrading
  beforehand.

  Please remember that I'm _very_ busy. Try to help yourself first,
  then try to help me help you by following these guidelines
  carefully.

  Regards,
  Tim, Jeff and Martin.
