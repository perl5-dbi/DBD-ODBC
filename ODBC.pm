# $Id$
#
# Copyright (c) 1994,1995,1996,1998  Tim Bunce
# portions Copyright (c) 1997,1998  Jeff Urlwin
# portions Copyright (c) 1997  Thomas K. Wenrich
#
# You may distribute under the terms of either the GNU General Public
# License or the Artistic License, as specified in the Perl README file.

require 5.004;

$DBD::ODBC::VERSION = '0.44';

{
    package DBD::ODBC;

    use DBI ();
    use DynaLoader ();

    @ISA = qw(DynaLoader);

    my $Revision = substr(q$Revision: 1.12 $, 10);

    require_version DBI 1.201;

    bootstrap DBD::ODBC $VERSION;

    $err = 0;		# holds error code   for DBI::err
    $errstr = "";	# holds error string for DBI::errstr
    $sqlstate = "00000";
    $drh = undef;	# holds driver handle once initialised

    sub driver{
	return $drh if $drh;
	my($class, $attr) = @_;

	$class .= "::dr";

	# not a 'my' since we use it above to prevent multiple drivers

	$drh = DBI::_new_drh($class, {
	    'Name' => 'ODBC',
	    'Version' => $VERSION,
	    'Err'    => \$DBD::ODBC::err,
	    'Errstr' => \$DBD::ODBC::errstr,
	    'State' => \$DBD::ODBC::sqlstate,
	    'Attribution' => 'ODBC DBD by Tim Bunce',
	    });

	$drh;
    }

    1;
}


{   package DBD::ODBC::dr; # ====== DRIVER ======
    use strict;

    sub connect {
	my $drh = shift;
	my($dbname, $user, $auth, $attr)= @_;
	$user = '' unless defined $user;
	$auth = '' unless defined $auth;

	# create a 'blank' dbh
	my $this = DBI::_new_dbh($drh, {
	    'Name' => $dbname,
	    'USER' => $user, 
	    'CURRENT_USER' => $user,
	    });

	# Call ODBC logon func in ODBC.xs file
	# and populate internal handle data.

	DBD::ODBC::db::_login($this, $dbname, $user, $auth, $attr) or return undef;

	$this;
    }

}


{   package DBD::ODBC::db; # ====== DATABASE ======
    use strict;

    sub prepare {
	my($dbh, $statement, @attribs)= @_;

	# create a 'blank' dbh
	my $sth = DBI::_new_sth($dbh, {
	    'Statement' => $statement,
	    });

	# Call ODBC func in ODBC.xs file.
	# (This will actually also call SQLPrepare for you.)
	# and populate internal handle data.

	DBD::ODBC::st::_prepare($sth, $statement, @attribs)
	    or return undef;

	$sth;
    }

    sub column_info {
	my ($dbh, $catalog, $schema, $table, $column) = @_;

	$catalog = "" if (!$catalog);
	$schema = "" if (!$schema);
	$table = "" if (!$table);
	$column = "" if (!$column);
	# create a "blank" statement handle
	my $sth = DBI::_new_sth($dbh, { 'Statement' => "SQLColumns" });

	_columns($dbh,$sth, $catalog, $schema, $table, $column)
	    or return undef;

	$sth;
    }
    
    sub columns {
	my ($dbh, $catalog, $schema, $table, $column) = @_;

	$catalog = "" if (!$catalog);
	$schema = "" if (!$schema);
	$table = "" if (!$table);
	$column = "" if (!$column);
	# create a "blank" statement handle
	my $sth = DBI::_new_sth($dbh, { 'Statement' => "SQLColumns" });

	_columns($dbh,$sth, $catalog, $schema, $table, $column)
	    or return undef;

	$sth;
    }


    sub table_info {
 	my($dbh, $catalog, $schema, $table, $type) = @_;

	if ($#_ == 1) {
	   my $attrs = $_[1];
	   $catalog = $attrs->{TABLE_CAT};
	   $schema = $attrs->{TABLE_SCHEM};
	   $table = $attrs->{TABLE_NAME};
	   $type = $attrs->{TABLE_TYPE};
 	}

	$catalog = "" if (!$catalog);
	$schema = "" if (!$schema);
	$table = "" if (!$table);
	$type = "" if (!$type);

	# create a "blank" statement handle
	my $sth = DBI::_new_sth($dbh, { 'Statement' => "SQLTables" });

	DBD::ODBC::st::_tables($dbh,$sth, $catalog, $schema, $table, $type)
	      or return undef;
	$sth;
    }

    sub primary_key_info {
       my ($dbh, $catalog, $schema, $table ) = @_;
 
       # create a "blank" statement handle
       my $sth = DBI::_new_sth($dbh, { 'Statement' => "SQLPrimaryKeys" });
 
       $catalog = "" if (!$catalog);
       $schema = "" if (!$schema);
       $table = "" if (!$table);
       DBD::ODBC::st::_primary_keys($dbh,$sth, $catalog, $schema, $table )
	     or return undef;
       $sth;
    }

    sub foreign_key_info {
       my ($dbh, $pkcatalog, $pkschema, $pktable, $fkcatalog, $fkschema, $fktable ) = @_;
 
       # create a "blank" statement handle
       my $sth = DBI::_new_sth($dbh, { 'Statement' => "SQLForeignKeys" });
 
       $pkcatalog = "" if (!$pkcatalog);
       $pkschema = "" if (!$pkschema);
       $pktable = "" if (!$pktable);
       $fkcatalog = "" if (!$fkcatalog);
       $fkschema = "" if (!$fkschema);
       $fktable = "" if (!$fktable);
       _GetForeignKeys($dbh, $sth, $pkcatalog, $pkschema, $pktable, $fkcatalog, $fkschema, $fktable) or return undef;
       $sth;
    }

    sub ping {
	my $dbh = shift;
	my $state = undef;

 	my ($catalog, $schema, $table, $type);

	$catalog = "";
	$schema = "";
	$table = "NOXXTABLE";
	$type = "";

	# create a "blank" statement handle
	my $sth = DBI::_new_sth($dbh, { 'Statement' => "SQLTables_PING" });

	DBD::ODBC::st::_tables($dbh,$sth, $catalog, $schema, $table, $type)
	      or return 0;
	$sth->finish;
	return 1;

    }

    # saved, just for posterity.
    sub oldping  {
	my $dbh = shift;
	my $state = undef;

	# should never 'work' but if it does, that's okay!
	# JLU incorporated patches from Jon Smirl 5/4/99
	{
	    local $dbh->{RaiseError} = 0 if $dbh->{RaiseError};
	    # JLU added local PrintError handling for completeness.
	    # it shouldn't print, I think.
	    local $dbh->{PrintError} = 0 if $dbh->{PrintError};
	    my $sql = "select sysdate from dual1__NOT_FOUND__CANNOT";
	    my $sth = $dbh->prepare($sql);
	    # fixed "my" $state = below.  Was causing problem with
	    # ping!  Also, fetching fields as some drivers (Oracle 8)
	    # may not actually check the database for activity until
	    # the query is "described".
	    # Right now, Oracle8 is the only known version which
	    # does not actually check the server during prepare.
	    my $ok = $sth && $sth->execute();

	    $state = $dbh->state;
	    $DBD::ODBC::err = 0;
	    $DBD::ODBC::errstr = "";
	    $DBD::ODBC::sqlstate = "00000";
	    return 1 if $ok;
	}
	return 1 if $state eq 'S0002';	# Base table not found
 	return 1 if $state eq '42S02';  # Base table not found.Solid EE v3.51
	return 1 if $state eq 'S0022';	# Column not found
	return 1 if $state eq '37000';  # statement could not be prepared (19991011, JLU)
	# return 1 if $state eq 'S1000';  # General Error? ? 5/30/02, JLU.  This is what Openlink is returning
	# We assume that any other error means the database
	# is no longer connected.
	# Some special cases may need to be added to the code above.
	return 0;
    }

    # New support for the next DBI which will have a get_info command.
    # leaving support for ->func(xxx, GetInfo) (above) for a period of time
    # to support older applications which used this.
    sub get_info {
	my ($dbh, $item) = @_;
	# handle SQL_DRIVER_HSTMT, SQL_DRIVER_HLIB and
	# SQL_DRIVER_HDESC specially
	if ($item == 5 || $item == 135 || $item == 76) {
	   return undef;
	}
	return _GetInfo($dbh, $item);
    }

    # new override of do method provided by Merijn Broeren
    # this optimizes "do" to use SQLExecDirect for simple
    # do statements without parameters.
    sub do {
        my($dbh, $statement, $attr, @params) = @_;
        my $rows = 0;

        if( -1 == $#params )
        {
          # No parameters, use execute immediate
          $rows = ExecDirect( $dbh, $statement );
          if( 0 == $rows )
          {
            $rows = "0E0";
          }
          elsif( $rows < -1 )
          {
            undef $rows;
          }
        }
        else
        {
          $rows = $dbh->SUPER::do( $statement, $attr, @params );
        }
        return $rows
    }

    #
    # can also be called as $dbh->func($sql, ExecDirect);
    # if, for some reason, there are compatibility issues
    # later with DBI's do.
    #
    sub ExecDirect {
       my ($dbh, $sql) = @_;
       _ExecDirect($dbh, $sql);
    }

    # Call the ODBC function SQLGetInfo
    # Args are:
    #	$dbh - the database handle
    #	$item: the requested item.  For example, pass 6 for SQL_DRIVER_NAME
    # See the ODBC documentation for more information about this call.
    #
    sub GetInfo {
	my ($dbh, $item) = @_;
	get_info($dbh, $item);
    }

    # Call the ODBC function SQLStatistics
    # Args are:
    # See the ODBC documentation for more information about this call.
    #
    sub GetStatistics {
			my ($dbh, $Catalog, $Schema, $Table, $Unique) = @_;
			# create a "blank" statement handle
			my $sth = DBI::_new_sth($dbh, { 'Statement' => "SQLStatistics" });
			_GetStatistics($dbh, $sth, $Catalog, $Schema, $Table, $Unique) or return undef;
			$sth;
    }

    # Call the ODBC function SQLForeignKeys
    # Args are:
    # See the ODBC documentation for more information about this call.
    #
    sub GetForeignKeys {
			my ($dbh, $PK_Catalog, $PK_Schema, $PK_Table, $FK_Catalog, $FK_Schema, $FK_Table) = @_;
			# create a "blank" statement handle
			my $sth = DBI::_new_sth($dbh, { 'Statement' => "SQLForeignKeys" });
			_GetForeignKeys($dbh, $sth, $PK_Catalog, $PK_Schema, $PK_Table, $FK_Catalog, $FK_Schema, $FK_Table) or return undef;
			$sth;
    }

    # Call the ODBC function SQLPrimaryKeys
    # Args are:
    # See the ODBC documentation for more information about this call.
    #
    sub GetPrimaryKeys {
			my ($dbh, $Catalog, $Schema, $Table) = @_;
			# create a "blank" statement handle
			my $sth = DBI::_new_sth($dbh, { 'Statement' => "SQLPrimaryKeys" });
			_GetPrimaryKeys($dbh, $sth, $Catalog, $Schema, $Table) or return undef;
			$sth;
    }

    # Call the ODBC function SQLSpecialColumns
    # Args are:
    # See the ODBC documentation for more information about this call.
    #
    sub GetSpecialColumns {
	my ($dbh, $Identifier, $Catalog, $Schema, $Table, $Scope, $Nullable) = @_;
	# create a "blank" statement handle
	my $sth = DBI::_new_sth($dbh, { 'Statement' => "SQLSpecialColumns" });
	_GetSpecialColumns($dbh, $sth, $Identifier, $Catalog, $Schema, $Table, $Scope, $Nullable) or return undef;
	$sth;
    }
	
    sub GetTypeInfo {
	my ($dbh, $sqltype) = @_;
	# create a "blank" statement handle
	my $sth = DBI::_new_sth($dbh, { 'Statement' => "SQLGetTypeInfo" });
	# print "SQL Type is $sqltype\n";
	_GetTypeInfo($dbh, $sth, $sqltype) or return undef;
	$sth;
    }

    sub type_info_all {
	my ($dbh, $sqltype) = @_;
	$sqltype = DBI::SQL_ALL_TYPES unless defined $sqltype;
	my $sth = DBI::_new_sth($dbh, { 'Statement' => "SQLGetTypeInfo" });
	_GetTypeInfo($dbh, $sth, $sqltype) or return undef;
	my $info = $sth->fetchall_arrayref;
	unshift @$info, {
	    map { ($sth->{NAME}->[$_] => $_) } 0..$sth->{NUM_OF_FIELDS}-1
	};
	return $info;
    }

}


{   package DBD::ODBC::st; # ====== STATEMENT ======
    use strict;

    sub ColAttributes {		# maps to SQLColAttributes
	my ($sth, $colno, $desctype) = @_;
	# print "before ColAttributes $colno\n";
	my $tmp = _ColAttributes($sth, $colno, $desctype);
	# print "After ColAttributes\n";
	$tmp;
    }

    sub cancel {
	my $sth = shift;
	my $tmp = _Cancel($sth);
	$tmp;
    }
}

1;
__END__

=head1 NAME

DBD::ODBC - ODBC Driver for DBI

=head1 SYNOPSIS

  use DBI;

  $dbh = DBI->connect('dbi:ODBC:DSN', 'user', 'password');

See L<DBI> for more information.

=head1 DESCRIPTION

=head2 Recent Updates

=over 4
=item B<An Important note about the tests!>

 Please note that some tests may fail or report they are
 unsupported on this platform.  Notably Oracle's ODBC driver
 will fail the "advanced" binding tests in t/08bind2.t.
 These tests run perfectly under SQL Server 2000. This is
 normal and expected.  Until Oracle fixes their drivers to
 do the right thing from an ODBC perspective, it's going to
 be tough to fix the issue.  The workaround for Oracle is to
 bind date types with SQL_TIMESTAMP.
   
 Also note that some tests may be skipped, such as
 t/09multi.t, if your driver doesn't seem to support
 returning multiple result sets.

=item B<DBD::ODBC 0.43>

=item B<DBD::ODBC 0.43>

Fix for FoxPro drivers -- HURRAY!!!!!!!!!  Bad call to SQLDescribeCol where the FoxPro driver
seems to clear out the buffer passed and the size of the buffer indicated in the parameters
was larger than the real buffer...need better fix long term, but fixed for now by allocating
extra space.
 
Added support for column_info (maps to function columns, previously provided)
 
Fix for binding undef value generated by a dereferencing an empty hash value.
I.e in the hash, there is no key value "1", but $hash{"1"} is passed to binding parameters.

A fix to make all bound columns word aligned.  This solved a problem on solaris when binding
timestamp values after a varchar value.  This patch forces all columns to start on word
boundaries (sizeof int boundaries, actually).  This may have a positive speed impact for
other systems.  Thanks to Joe Slagel for pointing this out and providing a patch for the fix!
 
=item B<DBD::ODBC 0.42>

A patch to the tests to get past any potential problems with
a user not defining the DBI_DSN environment variable.  Hopefully
this will allow ActiveState to keep up with DBD::ODBC in
their automated builds.  Thanks to Jan Dubois for helping
me determine what the issue(s) may be when testing.
Now skips all tests except the dll/so load if DBI_DSN is
not defined.  Also, issues a skip on a known Oracle ODBC
test.
 
A patch to fix ping() when a S1000 was returned.  I don't know how this
one is going to go, but it's worth a shot.  Now calls SQLTables with unlikely
parameters.  If SQLTables() succeeds and returns a result set, ping() will
now return true.  Thanks to Tim Bunce for the suggestion.
 
=item B<DBD::ODBC 0.41>

A patch to handle SQLDescribeParam failing in some
circumstances.  I believe this is a bug in the ODBC
driver, but this happens with SQL Server and INNER
JOIN syntax queries, but not when where a.i = b.i.
The behavior is now to revert to assume SQL_VARCHAR
if SQLDescribeParam fails for whatever reason.					     

Added warning during tests when the Oracle ODBC driver
is detected and it is a known error.  I didn't want
to make it pass the test silently, but I did want to print
a message indicating it was expected.
   
=item B<DBD::ODBC 0.40>

Two minor patches for building.  One for Cygwin support
and another to handle the case where both iODBC and
unixODBC libraries are installed.  The preference is
now given to unixODBC...  

Fixed problem in connect code introduced in 0.39 where
new code would only be executed if tracing was on and
> 3...

NEW: Changed default bind type to 0, to let DBD::ODBC
try to detect the correct bind type.  This is a
potentially significant change.  Please let me know
if there is a problem.  The best way is to post a message
to the dbi-users@perl.org mailing list.

=item B<DBD::ODBC 0.39>

 Removing iodbcsrc directory as newer/better versions
 can be found on the web at www.iodbc.org and
 www.unixodbc.org.  On Linux, I currently build
 with unixODBC verson 2.2.0.
 
 Added patch for handling setting the ODBC environment
 during the connect, thanks to Steffen Goeldner
 
 The attached patch makes it possible to choose the
 ODBC version. E.g.:
 
   my $dbh = DBI->connect( ..., { odbc_version => 3 } )
 
 directs the driver to exhibit ODBC 3.x behavior.

 Fix to SQLColAttributes thanks to Nicolas DeRico

 Changes to the connect sequence.  If SQLDriverConnect
 is supported and fails, SQLConnect will be called
 I<unless> (this is the new part) the length of the
 DSN is > SQL_MAX_DSN_LENGTH or the DSN begins with
 DSN=

 New test in mytest which demonstrates long binary
 types.  If you are having trouble inserting images
 into your database, please check here.
=item B<DBD::ODBC 0.38>

 Fixed do function (again) thanks to work by Martin Evans.
 
=item B<DBD::ODBC 0.37>

 Patches for get_info where return type is string.  Patches
 thanks to Steffen Goldner.  Thanks Steffen!

 Patched get_info to NOT attempt to get data for SQL_DRIVER_HSTMT
 and SQL_DRIVER_HDESC as they expect data in and have limited value
 (IMHO).

=item B<DBD::ODBC 0.37>

 Further fixed build for ODBC 2.x drivers.  The new SQLExecDirect
 code had SQLAllocHandle which is a 3.x function, not a 2.x function.
 Sigh.  I should have caught that the first time.  Signed, the Mad-and-
 not-thorough-enough-patcher.

 Additionally, a random core dump occurred in the tests, based upon the
 new SQLExecDirect code.  This has been fixed.
 
 
=item B<DBD::ODBC 0.36>

 Fixed build for ODBC 2.x drivers.  The new SQLExecDirect code
 had SQLFreeHandle which is a 3.x function, not a 2.x function.
 
=item B<DBD::ODBC 0.35>

 Fixed (finally) multiple result sets with differing
 numbers of columns.  The final fix was to call
 SQLFreeStmt(SQL_UNBIND) before repreparing
 the statement for the next query.

 Added more to the multi-statement tests to ensure
 the data retrieved was what was expected.

 Now, DBD::ODBC overrides DBI's do to call SQLExecDirect
 for simple statements (those without parameters).
 Please advise if you run into problems.  Hopefully,
 this will provide some small speed improvement for
 simple "do" statements.  You can also call
 $dbh->func($stmt, ExecDirect).  I'm not sure this has
 great value unless you need to ensure SQLExecDirect
 is being called.  Patches thanks to Merijn Broeren.
 Thanks Merijn!
   
=item B<DBD::ODBC 0.34>
 
 Further revamped tests to attempt to determine if SQLDescribeParam
 will work to handle the binding types.  The t/08bind.t attempts
 to determine if SQLDescribeParam is supported.  note that Oracle's
 ODBC driver under NT doesn't work correctly when binding dates
 using the ODBC date formatting {d } or {ts }.  So, test #3 will
 fail in t/08bind.t

 New support for primary_key_info thanks to patches by Martin Evans.
 New support for catalog, schema, table and table_type in table_info
 thanks to Martin Evans.  Thanks Martin for your work and your
 continuing testing, suggestions and general support!

 Support for upcoming dbi get_info.
 
=item B<DBD::ODBC 0.33_3>

 Revamped tests to include tests for multiple result sets.
 The tests are ODBC driver platform specific and will be skipped
 for drivers which do not support multiple result sets.
 
=item B<DBD::ODBC 0.33_2>

 Finally tested new binding techniques with SQL Server 2000,
 but there is a nice little bug in their MDAC and ODBC
 drivers according to the knowledge base article # Q273813, titled

   "FIX: "Incorrect Syntax near the Keyword 'by' "
   Error Message with Column Names of "C", "CA" or "CAS" (Q273813)

 DBD::ODBC now does not name any of the columns A, B, C, or D
 they are now COL_A, COL_B, COL_C, COL_D.

 *** NOTE: *** I AM STRONGLY CONSIDERING MAKING THE NEW
 BINDING the default for future versions.  I do not believe
 it will break much existing code (if any) as anyone binding
 to non VARCHAR (without the ODBC driver doing a good conversion
 from the VARCHAR) will have a problem.  It may be subtle, however,
 since much code will work, but say, binding dates may not with
 some drivers.
   
 Please comment soon...
   
=item B<DBD::ODBC 0.33_1>

*** WARNING: ***
 
 Changes to the binding code to allow the use of SQLDescribeParam
 to determine if the type of column being bound.  This is
 experimental and activated by setting
 
  $dbh->{odbc_default_bind_type} = 0; # before creating the query...

 Currently the default value of odbc_default_bind_type = SQL_VARCHAR
 which mimicks the current behavior.  If you set
 odbc_default_bind_type to 0, then SQLDescribeParam will be
 called to determine the columen type.  Not ALL databases
 handle this correctly.  For example, Oracle returns
 SQL_VARCHAR for all types and attempts to convert to the
 correct type for us.  However, if you use the ODBC escaped
 date/time format such as: {ts '1998-05-13 00:01:00'} then
 Oracle complains.  If you bind this with a SQL_TIMESTAMP type,
 however, Oracle's ODBC driver will parse the time/date correctly.
 Use at your own risk!

 Fix to dbdimp.c to allow quoted identifiers to begin/end
 with either " or '.
 The following will not be treated as if they have a bind placeholder:
   "isEstimated?"
   '01-JAN-1987 00:00:00'
   'Does anyone insert a ?'

				    
=item B<DBD::ODBC 0.32>

 More SAP patches to Makfile.PL to eliminate the call to Data Sources

 A patch to the test (for SAP and potentially others), to allow
 fallback to SQL_TYPE_DATE in the tests
 
=item B<DBD::ODBC 0.31>

 Added SAP patches to build directly against SAP driver instead of
 driver manager thanks to Flemming Frandsen (thanks!)

 Added support to fix ping for Oracle8.  May break other databases,
 so please report this as soon as possible.  The downside is that
 we need to actually execute the dummy query.
 

=item B<DBD::ODBC 0.30>

 Added ping patch for Solid courtesy of Marko Asplund

 Updated disconnect to rollback if autocommit is not on.
 This should silence some errors when disconnecting.

 Updated SQL_ROWSET_SIZE attribute.  Needed to force it to
 odbc_SQL_ROWSET_SIZE to obey the DBI rules.

 Added odbc_SQL_DRIVER_ODBC_VER, which obtains the version of
 the Driver upon connect.  This internal capture of the version is
 a read-only attributed and is used during array binding of parameters.
 
 Added odbc_ignore_named_placeholders attribute to facilicate
 creating triggers within SAPDB and Oracle, to name two. The
 syntax in these DBs is to allow use of :old and :new to
 access column values before and after updates.  Example:

 $dbh->{odbc_ignore_named_placeholders} = 1; # set it for all future statements
					  # ignores :foo, :new, etc, but not :1 or ?
 $dbh->do("create or replace etc :new.D = sysdate etc");
 

=item B<DBD::ODBC 0.29>

 Cygwin patches from Neil Lunn (untested by me).  Thanks Neil!
 
SQL_ROWSET_SIZE attribute patch from Andrew Brown 
> There are only 2 additional lines allowing for the setting of
> SQL_ROWSET_SIZE as db handle option.
>
> The purpose to my madness is simple. SqlServer (7 anyway) by default
> supports only one select statement at once (using std ODBC cursors).
> According to the SqlServer documentation you can alter the default setting
> of
> three values to force the use of server cursors - in which case multiple
> selects are possible.
>
> The code change allows for:
> $dbh->{odbc_SQL_ROWSET_SIZE} = 2;    # Any value > 1
>
> For this very purpose.
>
> The setting of SQL_ROWSET_SIZE only affects the extended fetch command as
> far as I can work out and thus setting this option shouldn't affect
> DBD::ODBC operations directly in any way.
>
> Andrew
>

VMS and other patches from Martin Evans (thanks!)

[1] a fix for Makefile.PL to build DBD::ODBC on OpenVMS.

[2] fix trace message coredumping after SQLDriverConnect

[3] fix call to SQLCancel which fails to pass the statement handle properly.

[4] consume diagnostics after SQLDriverConnect/SQLConnect call or they remain
    until the next error occurs and it then looks confusing (this is due to
    ODBC spec for SQLError). e.g. test 02simple returns a data truncated error
    only now instead of all the informational diags that are left from the
    connect call, like the "database changed", "language changed" messages you
    get from MS SQL Server.

Replaced C++ style comments with C style to support more platforms more easily.

Fixed bug which use the single quote (') instead of a double quote (") for "literal" column names.  This
   helped when having a colon (:) in the column name.

Fixed bug which would cause DBD::ODBC to core-dump (crash) if DBI tracing level was greater than 3.

Fixed problem where ODBC.pm would have "use of uninitialized variable" if calling DBI's type_info.

Fixed problem where ODBC.xs *may* have an overrun when calling SQLDataSources. 

Fixed problem with DBI 1.14, where fprintf was being called instead of PerlIO_printf for debug information

Fixed problem building with unixODBC per patch from Nick Gorham   

Added ability to bind_param_inout() via patches from Jeremy Cooper.  Haven't figured out a good, non-db specific
   way to test.  My current test platform attempts to determine the connected database type via
   ugly hacks and will test, if it thinks it can.  Feel free to patch and send me something...Also, my
   current Oracle ODBC driver fails miserably and dies.

Updated t/02simple.t to not print an error, when there is not one.
   
=item B<DBD::ODBC 0.28>

Added support for SQLSpecialColumns thanks to patch provided by Martin J. Evans [martin@easysoft.com]

Fixed bug introduced in 0.26 which was introduced of SQLMoreResults was not supported by the driver.

=item B<DBD::ODBC 0.27>

Examined patch for ping method to repair problem reported by Chris Bezil.  Thanks Chris!

Added simple test for ping method working which should identify this in the future.

=item B<DBD::ODBC 0.26>

Put in patch for returning only positive rowcounts from dbd_st_execute.  The original patch
was submitted by Jon Smirl and put back in by David Good.  Reasoning seems sound, so I put it
back in.  However, any databases that return negative rowcounts for specific reasons,
will no longer do so.

Put in David Good's patch for multiple result sets.  Thanks David!  See mytest\moreresults.pl for
an example of usage.

Added readme.txt in iodbcsrc explaining an issue there with iODBC 2.50.3 and C<data_sources>.

Put in rudimentary cancel support via SQLCancel.  Call $sth->cencel to utilize.  However, it is largely
untested by me, as I do not have a good sample for this yet.  It may come in handy with threaded
perl, someday or it may work in a signal handler.
   
=item B<DBD::ODBC 0.25>

Added conditional compilation for SQL_WVARCHAR and SQL_WLONGVARCHAR.  If they
are not defined by your driver manager, they will not be compiled in to the code.
If you would like to support these types on some platforms, you may be able to
 #define SQL_WVARCHAR (-9)
 #define SQL_WLONGVARCHAR (-10)

Added more long tests with binding in t\09bind.t.  Note use of bind_param!
 
=item B<DBD::ODBC 0.24>

Fixed Test #13 in 02simple.t.  Would fail, improperly, if there was only one data source defined.

Fixed (hopefully) SQL Server 7 and ntext type "Out of Memory!" errors via patch from Thomas Lowery.  Thanks Thomas!

Added more support for Solid to handle the fact that it does not support data_sources nor SQLDriverConnect.
 Patch supplied by Samuli Karkkainen [skarkkai@woods.iki.fi].  Thanks!  It's untested by me, however.

Added some information from Adam Curtin about a bug in iodbc 2.50.3's data_sources.  See
   iodbcsrc\readme.txt.

Added information in this pod from Stephen Arehart regarding DSNLess connections.

Added fix for sp_prepare/sp_execute bug reported by Paul G. Weiss.

Added some code for handling a hint on disconnect where the user gets an error for not committing.
 
=item B<DBD::ODBC 0.22>

Fixed for threaded perl builds.  Note that this was tested only on Win32, with no threads in use and using DBI 1.13.
Note, for ActiveState/PERL_OBJECT builds, DBI 1.13_01 is required as of 9/8/99.  
If you are using ActiveState's perl, this can be installed by using PPM.


=item B<DBD::ODBC 0.21>

Thanks to all who provided patches!

Added ability to connect to an ODBC source without prior creation of DSN.  See mytest/contest.pl for example with MS Access.
(Also note that you will need documentation for your ODBC driver -- which, sadly, can be difficult to find).

Fixed case sensitivity in tests.

Hopefully fixed test #4 in t/09bind.t.  Updated it to insert the date column and updated it to find the right
type of the column.  However, it doesn't seem to work on my Linux test machine, using the OpenLink drivers 
with MS-SQL Server (6.5).  It complains about binding the date time.  The same test works under Win32 with 
SQL Server 6.5, Oracle 8.0.3 and MS Access 97 ODBC drivers.  Hmmph.

Fixed some binary type issues (patches from Jon Smirl)

Added SQLStatistics, SQLForeignKeys, SQLPrimaryKeys (patches from Jon Smirl)
Thanks (again), Jon, for providing the build_results function to help reduce duplicate code!

Worked on LongTruncOk for Openlink drivers.

Note: those trying to bind variables need to remember that you should use the following syntax:

	use DBI;
	...
	$sth->bind_param(1, $str, DBI::SQL_LONGVARCHAR);

Added support for unixodbc (per Nick Gorham)
Added support for OpenLinks udbc (per Patrick van Kleef)
Added Support for esodbc (per Martin Evans)
Added Support for Easysoft (per Bob Kline)

Changed table_info to produce a list of views, too.
Fixed bug in SQLColumns call.
Fixed blob handling via patches from Jochen Wiedmann.
Added data_sources capability via snarfing code from DBD::Adabas (Jochen Wiedmann)

=item B<DBD::ODBC 0.20>

SQLColAttributes fixes for SQL Server and MySQL. Fixed tables method
by renaming to new table_info method. Added new tyoe_info_all method.
Improved Makefile.PL support for Adabase.

=item B<DBD::ODBC 0.19>

Added iODBC source code to distribution.Fall-back to using iODBC header
files in some cases.

=item B<DBD::ODBC 0.18>

Enhancements to build process. Better handling of errors in
error handling code.

=item B<DBD::ODBC 0.17>

This release is mostly due to the good work of Jeff Urlwin.
My eternal thanks to you Jeff.

Fixed "SQLNumResultCols err" on joins and 'order by' with some
drivers (see Microsoft Knowledge Base article #Q124899).
Thanks to Paul O'Fallon for that one.

Added more (probably incomplete) support for unix ODBC in Makefile.PL

Increased default SQL_COLUMN_DISPLAY_SIZE and SQL_COLUMN_LENGTH to 2000
for drivers that don't provide a way to query them dynamically. Was 100!

When fetch reaches the end-of-data it automatically frees the internal
ODBC statement handle and marks the DBI statement handle as inactive
(thus an explicit 'finish' is *not* required).

Also:

  LongTruncOk for Oracle ODBC (where fbh->datalen < 0)
  Added tracing into SQLBindParameter (help diagnose oracle odbc bug)
  Fixed/worked around bug/result from Latest Oracle ODBC driver where in
     SQLColAttribute cbInfoValue was changed to 0 to indicate fDesc had a value
  Added work around for compiling w/ActiveState PRK (PERL_OBJECT)
  Updated tests to include date insert and type
  Added more "backup" SQL_xxx types for tests                                  
  Updated bind test to test binding select
  NOTE: bind insert fails on Paradox driver (don't know why)

Added support for: (see notes below)

  SQLGetInfo       via $dbh->func(xxx, GetInfo)
  SQLGetTypeInfo   via $dbh->func(xxx, GetTypeInfo)
  SQLDescribeCol   via $sth->func(colno, DescribeCol)
  SQLColAttributes via $sth->func(xxx, colno, ColAttributes)
  SQLGetFunctions  via $dbh->func(xxx, GetFunctions)
  SQLColumns       via $dbh->func(catalog, schema, table, column, 'columns')

Fixed $DBI::err to reflect the real ODBC error code
which is a 5 char code, not necessarily numeric.

Fixed fetches when LongTruncOk == 1.

Updated tests to pass more often (hopefully 100% <G>)

Updated tests to test long reading, inserting and the LongTruncOk attribute.

Updated tests to be less driver specific.  

They now rely upon SQLGetTypeInfo I<heavily> in order to create the tables.
The test use this function to "ask" the driver for the name of the SQL type
to correctly create long, varchar, etc types.  For example, in Oracle the
SQL_VARCHAR type is VARCHAR2, while MS Access uses TEXT for the SQL Name.  
Again, in Oracle the SQL_LONGVARCHAR is LONG, while in Access it's MEMO.
The tests currently handle this correctly (at least with Access and Oracle,
MS SQL server will be tested also).

=head2 Private functions for ODBC API access

It is anticipated that at least some of the functions currently
implemented via the C<func> interface be "moved" into a more formal,
DBI specification.  This will be when the DBI specification
supports/formalizes the meta-data to implement.  Most of these
functions are to obtain more information from the driver and the data
source.


=item GetInfo

This function maps to the ODBC SQLGetInfo call.  This is a Level 1 ODBC
function.  An example of this is:

  $value = $dbh->func(6, GetInfo);

This function returns a scalar value, which can be a numeric or string value.  
This depends upon the argument passed to GetInfo. 

=item SQLGetTypeInfo

This function maps to the ODBC SQLGetTypeInfo call.  This is a Level 1
ODBC function.  An example of this is:

  use DBI qw(:sql_types);

  $sth = $dbh->func(SQL_ALL_TYPES, GetInfo);
  while (@row = $sth->fetch_row) {
    ...
  }

This function returns a DBI statement handle, which represents a result
set containing type names which are compatible with the requested
type.  SQL_ALL_TYPES can be used for obtaining all the types the ODBC
driver supports.  NOTE: It is VERY important that the use DBI includes
the qw(:sql_types) so that values like SQL_VARCHAR are correctly
interpreted.  This "imports" the sql type names into the program's name
space.  A very common mistake is to forget the qw(:sql_types) and
obtain strange results.

=item GetFunctions

This function maps to the ODBC API SQLGetFunctions.  This is a Level 1
API call which returns supported driver funtions.  Depending upon how
this is called, it will either return a 100 element array of true/false
values or a single true false value.  If it's called with
SQL_API_ALL_FUNCTIONS (0), it will return the 100 element array.
Otherwise, pass the number referring to the function.  (See your ODBC
docs for help with this).

=item SQLColumns

Support for this function has been added in version 0.17.  It looks to be
fixed in version 0.20.

=item Connect without DSN
The ability to connect without a full DSN is introduced in version 0.21.

Example (using MS Access):
	my $DSN = 'driver=Microsoft Access Driver (*.mdb);dbq=\\\\cheese\\g$\\perltest.mdb';
	my $dbh = DBI->connect("dbi:ODBC:$DSN", '','') 
		or die "$DBI::errstr\n";

=item SQLStatistics

=item SQLForeignKeys

=item SQLPrimaryKeys

=item SQLDataSources

All handled, currently (as of 0.21)

=item SQLSpecialColumns

Handled as of version 0.28
 
=item Others/todo?

Level 1

    SQLTables (use tables()) call

Level 2

    SQLColumnPrivileges
    SQLProcedureColumns
    SQLProcedures
    SQLTablePrivileges
    SQLDrivers
    SQLNativeSql

=back

=head2 Using DBD::ODBC with web servers under Win32. 

=over 4

=item General Commentary re web database access

This should be a DBI faq, actually, but this has somewhat of an
Win32/ODBC twist to it.

Typically, the Web server is installed as an NT service or a Windows
95/98 service.  This typically means that the web server itself does
not have the same environment and permissions the web developer does.
This situation, of course, can and does apply to Unix web servers.
Under Win32, however, the problems are usually slightly different.

=item Defining your DSN -- which type should I use?

Under Win32 take care to define your DSN as a system DSN, not as a user
DSN.  The system DSN is a "global" one, while the user is local to a
user.  Typically, as stated above, the web server is "logged in" as a
different user than the web developer.  This helps cause the situation
where someone asks why a script succeeds from the command line, but
fails when called from the web server.

=item Defining your DSN -- careful selection of the file itself is important!

For file based drivers, rather than client server drivers, the file
path is VERY important.  There are a few things to keep in mind.  This
applies to, for example, MS Access databases.

1) If the file is on an NTFS partition, check to make sure that the Web
B<service> user has permissions to access that file.

2) If the file is on a remote computer, check to make sure the Web
B<service> user has permissions to access the file.

3) If the file is on a remote computer, try using a UNC path the file,
rather than a X:\ notation.  This can be VERY important as services
don't quite get the same access permissions to the mapped drive letters
B<and>, more importantly, the drive letters themselves are GLOBAL to
the machine.  That means that if the service tries to access Z:, the Z:
it gets can depend upon the user who is logged into the machine at the
time.  (I've tested this while I was developing a service -- it's ugly
and worth avoiding at all costs).

Unfortunately, the Access ODBC driver that I have does not allow one to
specify the UNC path, only the X:\ notation.  There is at least one way
around that.  The simplest is probably to use Regedit and go to
(assuming it's a system DSN, of course)
HKEY_LOCAL_USERS\SOFTWARE\ODBC\"YOUR DSN" You will see a few settings
which are typically driver specific.  The important value to change for
the Access driver, for example, is the DBQ value.  That's actually the
file name of the Access database.

=item Connect without DSN
The ability to connect without a full DSN is introduced in version 0.21.

Example (using MS Access):
	my $DSN = 'driver=Microsoft Access Driver
(*.mdb);dbq=\\\\cheese\\g$\\perltest.mdb';
	my $dbh = DBI->connect("dbi:ODBC:$DSN", '','') 
		or die "$DBI::errstr\n";

The above sample uses Microsoft's UNC naming convention to point to the MSAccess
file (\\\\cheese\\g$\\perltest.mdb).  The dbq parameter tells the access driver
which file to use for the database.
   
Example (using MSSQL Server):
      my $DSN = 'driver={SQL Server};Server=server_name;
      database=database_name;uid=user;pwd=password;';
      my $dbh  = DBI->connect("dbi:ODBC:$DSN") or die "$DBI::errstr\n";

=head2 Random Links

These are in need of sorting and annotating. Some are relevant only
to ODBC developers (but I don't want to loose them).

	http://www.ids.net/~bjepson/freeODBC/index.html

	http://dataramp.com/

	http://www.syware.com

	http://www.microsoft.com/odbc

   For Linux/Unix folks, compatible ODBC driver managers can be found at:
   
        http://www.easysoft.com		unixODBC driver manager source
				        *and* ODBC-ODBC bridge for accessing Win32 ODBC sources from Linux

        http://www.iodbc.org		iODBC driver manager source

   Also, for Linux folks, you can checkout the following for another ODBC-ODBC bridge and support for iODBC.

	http://www.openlink.co.uk 
		or
	http://www.openlinksw.com 



=head2 Frequently Asked Questions
Answers to common DBI and DBD::ODBC questions:

=item How do I read more than N characters from a Memo | BLOB | LONG field?

See LongReadLen in the DBI docs.  

Example:
	$dbh->{LongReadLen} = 20000;
	$sth = $dbh->prepare("select long_col from big_table");
	$sth->execute;
	etc

=item What is DBD::ODBC?  Why can't I connect?  Do I need an ODBC driver?  What is the ODBC driver manager?

These, general questions lead to needing definitions.

1) ODBC Driver - the driver that the ODBC manager uses to connect
and interact with the RDBMS.  You DEFINITELY need this to 
connect to any database.  For Win32, they are plentiful and installed
with many applications.  For Linux/Unix, some hunting is required, but
you may find something useful at:

	http://www.openlinksw.com
        http://www.easysoft.com
	http://www.intersolv.com
	      

2) ODBC Driver Manager - the piece of software which interacts with the drivers
for the application.  It "hides" some of the differences between the
drivers (i.e. if a function call is not supported by a driver, it 'hides'
that and informs the application that the call is not supported.
DBD::ODBC needs this to talk to drivers.  Under Win32, it is built in
to the OS.  Under Unix/Linux, in most cases, you will want to use freeODBC,
unixODBC or iODBC.  iODBC was bundled with DBD::ODBC, but you will need to find one
which suits your needs.  Please see www.openlinksw.com, www.easysoft.com or www.iodbc.org

3) DBD::ODBC.  DBD::ODBC uses the driver manager to talk to the ODBC driver(s) on
your system.  You need both a driver manager and driver installed and tested
before working with DBD::ODBC.  You need to have a DSN (see below) configured
*and* TESTED before being able to test DBD::ODBC.

4) DSN -- Data Source Name.  It's a way of referring to a particular database by any
name you wish.  The name itself can be configured to hide the gory details of
which type of driver you need and the connection information you need to provide.
For example, for some databases, you need to provide a TCP address and port.
You can configure the DSN to have use information when you refer to the DSN.

=item Where do I get an ODBC driver manager for Unix/Linux?

DBD::ODBC comes with one (iODBC).  In the DBD::ODBC source release is a directory named iodbcsrc.  
There are others.  UnixODBC, FreeODBC and some of the drivers will come with one of these managers.
For example Openlink's drivers (see below) come with the iODBC driver manager.  Easysoft
supplies both ODBC-ODBC bridge software and unixODBC.

=item How do I access a MS SQL Server database from Linux?

Try using drivers from http://www.openlinksw.com or www.easysoft.com
The multi-tier drivers have been tested with Linux and Redhat 5.1.

=item How do I access an MS-Access database from Linux?

I believe you can use the multi-tier drivers from http://www.openlinksw.com, however, I have
not tested this.  Also, I believe there is a commercial solution from http://www.easysoft.com.  I
have not tested this.

If someone does have more information, please, please send it to me and I will put it in this
FAQ.

=item Almost all of my tests for DBD::ODBC fail.  They complain about not being able to connect
or the DSN is not found.  

Please, please test your configuration of ODBC and driver before trying to test DBD::ODBC.  Most
of the time, this stems from the fact that the DSN (or ODBC) is not configured properly.  iODBC
comes with a odbctest program.  Please use it to verify connectivity.

=item For Unix -> Windows DB see Tom Lowery's write-up.

http://tlowery.hypermart.net/perl_dbi_dbd_faq.html#HowDoIAccessMSWindowsDB

=item I'm attempting to bind a Long Var char (or other specific type) and the binding is not working.
The code I'm using is below:

	$sth->bind_param(1, $str, $DBI::SQL_LONGVARCHAR);
                                 ^^^
The problem is that DBI::SQL_LONGVARCHAR is not the same as $DBI::SQL_LONGVARCHAR and that
$DBI::SQL_LONGVARCHAR is an error!

It should be:

	$sth->bind_param(1, $str, DBI::SQL_LONGVARCHAR);


=cut
