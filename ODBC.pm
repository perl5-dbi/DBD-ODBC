# $Id$
#
# Copyright (c) 1994,1995,1996,1998  Tim Bunce
# portions Copyright (c) 1997-2004  Jeff Urlwin
# portions Copyright (c) 1997  Thomas K. Wenrich
#
# You may distribute under the terms of either the GNU General Public
# License or the Artistic License, as specified in the Perl README file.

require 5.006;

$DBD::ODBC::VERSION = '1.15_3';

{
    package DBD::ODBC;

    use DBI ();
    use DynaLoader ();
    use Exporter ();
    
    @ISA = qw(Exporter DynaLoader);

    # my $Revision = substr(q$Id$, 13,2);

    require_version DBI 1.21;

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
	    'Attribution' => 'ODBC DBD by Jeff Urlwin',
	    });

	$drh;
    }

    sub CLONE { undef $drh }
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

    sub private_attribute_info {
        return {
                odbc_ignore_named_placeholders => undef, # sth and dbh
                odbc_default_bind_type => undef, # sth and dbh
                odbc_force_rebind => undef, # sth & dbh
                odbc_async_exec => undef, # sth and dbh
                odbc_exec_direct => undef,
                odbc_SQL_ROWSET_SIZE => undef,
                SQL_DRIVER_ODBC_VER => undef,
                odbc_cursortype => undef,
                odbc_query_timeout => undef, # sth and dbh
                odbc_has_unicode => undef,
                odbc_version => undef,
                odbc_err_handler => undef
               };
    }

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

    sub private_attribute_info {
        return {
                odbc_ignore_named_placeholders => undef, # sth and dbh
                odbc_default_bind_type => undef, # sth and dbh
                odbc_force_rebind => undef, # sth & dbh
                odbc_async_exec => undef, # sth and dbh
                odbc_query_timeout => undef # sth and dbh
               };
    }

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

=head2 Change log and FAQs

Please note that the change log has been moved to
DBD::ODBC::Changes.pm. To access this documentation, use
C<perldoc DBD::ODBC::Changes>.

For FAQs see L</Frequently Asked Questions> or
L<http://dbi.perl.org>.

=head2 Important note about the tests

Please note that some tests may fail or report they are unsupported on
this platform.  Notably Oracle's ODBC driver will fail the "advanced"
binding tests in t/08bind2.t.  These tests run perfectly under SQL
Server 2000. This is normal and expected.  Until Oracle fixes their
drivers to do the right thing from an ODBC perspective, it's going to
be tough to fix the issue.  The workaround for Oracle is to bind date
types with SQL_TIMESTAMP.  Also note that some tests may be skipped,
such as t/09multi.t, if your driver doesn't seem to support returning
multiple result sets.  This is normal.

=head2 Version Control

DBD::ODBC source code is under version control at svn.perl.org.  If
you would like to use the "bleeding" edge version, you can get the
latest from svn.perl.org via Subversion version control.  Note there
is no guarantee that this version is any different than what you get
from the tarball from CPAN, but it might be :)

You may read about Subversion at L<http://subversion.tigris.org>

You can get a subversion client from there and check dbd-odbc out via:

   svn checkout http://svn.perl.org/modules/dbd-odbc/trunk <your directory name here>

Which will pull all the files from the subversion trunk to your
specified directory. If you want to see what has changed since the
last release of DBD::ODBC read the Changes file or use "svn log" to
get a list of checked in changes.

=head2 Contributing

Please use Subversion (see above) to get the latest version of
DBD::ODBC from the trunk and submit any patches against that.

Please, before submitting a patch:

   svn update
   <try and included a test which demonstrates the fix/change working>
   <test your patch>
   svn diff > describe_my_diffs.patch

and send the resulting file to me and cc the dbi-users@perl.org
mailing list (if you are not a member - why not!).

=head2 Private attributes common to connection and statement handles

=head3 odbc_ignore_named_placeholders

Use this if you have special needs (such as Oracle triggers, etc)
where :new or :name mean something special and are not just place
holder names You I<must> then use ? for binding parameters.  Example:

 $dbh->{odbc_ignore_named_placeholders} = 1;
 $dbh->do("create trigger foo as if :new.x <> :old.x then ... etc");

Without this, DBD::ODBC will think :new and :old are placeholders for
binding and get confused.

=head3 odbc_default_bind_type

This value defaults to 0.

Older versions of DBD::ODBC assumed that the binding type was 12
(SQL_VARCHAR).  Newer versions default to 0, which means that
DBD::ODBC will attempt to query the driver via SQLDescribeParam to
determine the correct type.  If the driver doesn't support
SQLDescribeParam, then DBD::ODBC falls back to using SQL_VARCHAR as
the default, unless overridden by bind_param().

=head3 odbc_force_rebind

This is to handle special cases, especially when using multiple result sets.
Set this before execute to "force" DBD::ODBC to re-obtain the result set's
number of columns and column types for each execute.  Especially useful for
calling stored procedures which may return different result sets each
execute.  The only performance penalty is during execute(), but I didn't
want to incur that penalty for all circumstances.  It is probably fairly
rare that this occurs.  This attribute will be automatically set when
multiple result sets are triggered.  Most people shouldn't have to worry
about this.

=head3 odbc_async_exec

Allow asynchronous execution of queries.  This causes a spin-loop
(with a small "sleep") until the SQL is complete.  This is useful,
however, if you want the error handling and asynchronous messages (see
the L</odbc_err_handler> and t/20SQLServer.t for an example of this.

=head3 odbc_query_timeout

This allows the end user to set a timeout for queries on the ODBC
side.  Add

  { odbc_query_timeout => 30 }

to you connect or set on the dbh before executing the statement. The
default is 0, no timeout.

Note that some drivers may not support this attribute.

See t/20SqlServer.t for an example.

=head2 Private connection attributes

=head3 odbc_err_handler

Allow errors to be handled by the application.  A call-back function
supplied by the application to handle or ignore messages.

The callback function receives three parameters: state (string),
error (string) and the native error code (number).

If the error handler returns 0, the error is ignored, otherwise the
error is passed through the normal DBI error handling.

This can also be used for procedures under MS SQL Server (Sybase too,
probably) to obtain messages from system procedures such as DBCC.
Check t/20SQLServer.t and t/10handler.t.

  $dbh->{RaiseError} = 1;
  sub err_handler {
     ($state, $msg, $native) = @_;
     if ($state = '12345')
         return 0; # ignore this error
     else
         return 1; # propagate error
  }
  $dbh->{odbc_err_handler} = \$err_handler;
  # do something to cause an error
  $dbh->{odbc_err_handler} = undef; # cancel the handler

=head3 odbc_SQL_ROWSET_SIZE

Here is the information from the original patch, however, I've learned
since from other sources that this could/has caused SQL Server to
"lock up".  Please use at your own risk!

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
> $dbh->{SQL_ROWSET_SIZE} = 2;    # Any value > 1
>
> For this very purpose.
>
> The setting of SQL_ROWSET_SIZE only affects the extended fetch command as
> far as I can work out and thus setting this option shouldn't affect
> DBD::ODBC operations directly in any way.
>
> Andrew
>

=head3 odbc_exec_direct

Force DBD::ODBC to use SQLExecDirect instead of SQLPrepare/SQLExecute.

There are drivers that only support SQLExecDirect and the DBD::ODBC
do() override does not allow returning result sets.  Therefore, the
way to do this now is to set the attribute odbc_exec_direct.

NOTE: You may also want to use this option if you are creating
temporary objects (e.g., tables) in MS SQL Server and for some
reason cannot use the C<do> method. see
L<http://technet.microsoft.com/en-US/library/ms131667.aspx> which says
I<Prepared statements cannot be used to create temporary objects on
SQL Server 2000 or later...>. Without odbc_exec_direct, the temporary
object will disappear before you can use it.

There are currently two ways to get this:

    $dbh->prepare($sql, { odbc_exec_direct => 1});

and

    $dbh->{odbc_exec_direct} = 1;

=head3 SQL_DRIVER_ODBC_VER

This, while available via get_info() is captured here.  I may get rid
of this as I only used it for debugging purposes.

=head3 odbc_cursortype

This allows multiple concurrent statements on SQL*Server.  In your
connect, add

  { odbc_cursortype => 2 }.

If you are using DBI > 1.41, you should also be able to use

 { odbc_cursortype => DBI::SQL_CURSOR_DYNAMIC }

instead.  For example:

    my $dbh = DBI->connect("dbi:ODBC:$DSN", $user, $pass,
                  { RaiseError => 1, odbc_cursortype => 2});
    my $sth = $dbh->prepare("one statement");
    my $sth2 = $dbh->prepare("two statement");
    $sth->execute;
    my @row;
    while (@row = $sth->fetchrow_array) {
       $sth2->execute($row[0]);
    }

See t/20SqlServer.t for an example.

=head3 odbc_has_unicode

A read-only attribute signifying whether DBD::ODBC was built with the
C macro WITH_UNICODE or not. A value of 1 indicates DBD::ODBC was built
with WITH_UNICODE else the value returned is 0.

Building WITH_UNICODE affects columns and parameters which are
SQL_C_WCHAR, SQL_WCHAR, SQL_WVARCHAR, and SQL_WLONGVARCHAR.

When odbc_has_unicode is 1, DBD::ODBC will:

=over

=item bind columns the database declares as wide characters as SQL_Wxxx

This means that UNICODE data stored in these columns will be returned
to Perl in UTF-8 and with the UTF8 flag set.

=item bind parameters the database declares as wide characters as SQL_Wxxx

Parameters bound where the database declares the parameter as being
a wide character (or where the parameter type is explicitly set to a
wide type - SQL_Wxxx) can be UTF8 in Perl and will be mapped to UTF16
before passing to the driver.

=back

NOTE: You will need at least Perl 5.8.1 to use UNICODE with DBD::ODBC.

NOTE: At this time SQL statements are still treated as native encoding
i.e. DBD::ODBC does not call SQLPrepareW with UNICODE strings. If you
need a unicode constant in an SQL statement, you have to pass it as
parameter or use SQL functions to convert your constant from native
encoding to Unicode.

NOTE: Binding of unicode output parameters is coded but untested.

NOTE: When building DBD::ODBC on Windows ($^O eq 'MSWin32') the
WITH_UNICODE macro is automatically added. To disable specify -nou as
an argument to Makefile.PL (e.g. nmake Makefile.PL -nou). On non-Windows
platforms the WITH_UNICODE macro is B<not> enabled by default and to enable
you need to specify the -u argument to Makefile.PL. Please bare in mind
that some ODBC drivers do not support SQL_Wxxx columns or parameters.

NOTE: Unicode support on Windows 64 bit platforms is currently
untested.  Let me know how you get on with it.

UNICODE support in ODBC Drivers differs considerably. Please read the
README.unicode file for further details.

=head3 odbc_version

This was added prior to the move to ODBC 3.x to allow the caller to
"force" ODBC 3.0 compatibility.  It's probably not as useful now, but
it allowed get_info and get_type_info to return correct/updated
information that ODBC 2.x didn't permit/provide.  Since DBD::ODBC is
now 3.x, this can be used to force 2.x behavior via something like: my

  $dbh = DBI->connect("dbi:ODBC:$DSN", $user, $pass,
                      { odbc_version =>2});

=head2 Private statement attributes

=head3 odbc_more_results

Use this attribute to determine if there are more result sets
available.  SQL Server supports this feature.  Use this as follows:

do {
   my @row;
   while (@row = $sth->fetchrow_array()) {
      # do stuff here
   }
} while ($sth->{odbc_more_results});

Note that with multiple result sets and output parameters (i.e. using
bind_param_inout, don't expect output parameters to be bound until ALL
result sets have been retrieved.

=head2 Private DBD::ODBC Functions

You use DBD::ODBC private functions like this:

  $dbh->func(arg, private_function_name);

=head3 GetInfo

B<This private function is now superceded by DBI's get_info method.>

This function maps to the ODBC SQLGetInfo call and the argument
should be a valid ODBC information type (see ODBC specification).
e.g.

  $value = $dbh->func(6, 'GetInfo');

which returns the SQL_DRIVER_NAME.

This function returns a scalar value, which can be a numeric or string
value depending on the information value requested.

=head3 SQLGetTypeInfo

B<This private function is now superceded by DBI's type_info and
type_info_all methods.>

This function maps to the ODBC SQLGetTypeInfo API and the argument
should be a SQL type number (e.g. SQL_VARCHAR) or
SQL_ALL_TYPES. SQLGetTypeInfo returns information about a data type
supported by the data source.

e.g.

  use DBI qw(:sql_types);

  $sth = $dbh->func(SQL_ALL_TYPES, GetTypeInfo);
  DBI::dump_results($sth);

This function returns a DBI statement handle for the SQLGetTypeInfo
result-set containing many columns of type attributes (see ODBC
specification).

NOTE: It is VERY important that the use DBI includes the
qw(:sql_types) so that values like SQL_VARCHAR are correctly
interpreted.  This "imports" the sql type names into the program's
name space.  A very common mistake is to forget the qw(:sql_types) and
obtain strange results.

=head3 GetFunctions

This function maps to the ODBC SQLGetFunctions API which returns
information on whether a function is supported by the ODBC driver.

The argument should be SQL_API_ALL_FUNCTIONS (0) for all functions or
a valid ODBC function number (e.g. SQL_API_SQLDESCRIBEPARAM which is
58). See ODBC specification or examine your sqlext.h and sql.h header
files for all the SQL_API_XXX macros.

If called with SQL_API_ALL_FUNCTIONS (0), then a 100 element array is
returned where each element will contain a '1' if the ODBC function with
that SQL_API_XXX index is supported or '' if it is not.

If called with a specific SQL_API_XXX value for a single function it will
return true if the ODBC driver supports that function, otherwise false.

e.g.


    my @x = $dbh->func(0,"GetFunctions");
    print "SQLDescribeParam is supported\n" if ($x[58]);

or

    print "SQLDescribeParam is supported\n"
        if $dbh->func(58, "GetFunctions");

=head3 GetStatistics

See the ODBC specification for the SQLStatistics API.
You call SQLStatistics like this:

  $dbh->func($catalog, $schema, $table, $unique, 'GetStatistics');

=head3 GetForeignKeys

B<This private function is now superceded by DBI's foreign_key_info
method.>

See the ODBC specification for the SQLForeignKeys API.
You call SQLForeignKeys like this:

  $dbh->func($pcatalog, $pschema, $ptable,
             $fcatalog, $fschema, $ftable,
             "GetForeignKeys");
 
=head3 GetPrimaryKeys

B<This private function is now superceded by DBI's primary_key_info
method.>

See the ODBC specification for the SQLPrimaryKeys API.
You call SQLPrimaryKeys like this:

  $dbh->func($vatalog, $schema, $table, "GetPrimaryKeys");

=head3 data_sources

B<This private function is now superceded by DBI's data_sources
method.>

You call data_sources like this:

  @dsns = $dbh->func("data_sources);

Handled since 0.21.

=head3 GetSpecialColumns

See the ODBC specification for the SQLSpecialColumns API.
You call SQLSpecialColumns like this:

  $dbh->func($identifier, $catalog, $schema, $table, $scope,
             $nullable, 'GetSpecialColumns');

Handled as of version 0.28
 
head3 ColAttributes

B<This private function is now superceded by DBI's statement attributes
NAME, TYPE, PRECISION, SCLARE, NULLABLE etc).>

See the ODBC specification for the SQLColAttributes API.
You call SQLColAttributes like this:

  $dbh->func($column, $ftype, "ColAttributes");

head3 DescribeCol

B<This private function is now superceded by DBI's statement attributes
NAME, TYPE, PRECISION, SCLARE, NULLABLE etc).>

See the ODBC specification for the SQLDescribeCol API.
You call SQLDescribeCol like this:

  @info = $dbh->func($column, "DescribeCol");

The returned array contains the column attributes in the order described
in the ODBC specification for SQLDescribeCol.

=head2 Others/todo?

Level 1

    SQLTables (use tables()) call

Level 2

    SQLColumnPrivileges
    SQLProcedureColumns
    SQLProcedures
    SQLTablePrivileges
    SQLDrivers
    SQLNativeSql

=head2 Connect without DSN

The ability to connect without a full DSN is introduced in version
0.21.

Example (using MS Access):
	my $DSN = 'driver=Microsoft Access Driver (*.mdb);dbq=\\\\cheese\\g$\\perltest.mdb';
	my $dbh = DBI->connect("dbi:ODBC:$DSN", '','') 
		or die "$DBI::errstr\n";

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

=head2 Connect without DSN

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

=back

=head2 Random Links

These are in need of sorting and annotating. Some are relevant only
to ODBC developers.

L<http://www.syware.com>

L<http://www.microsoft.com/odbc>

For Linux/Unix folks, compatible ODBC driver managers can be found at:
   
L<http://www.unixodbc.org> (unixODBC source and rpms)

L<http://www.iodbc.org> (iODBC driver manager source)

For Linux/Unix folks, you can checkout the following for ODBC Drivers and
Bridges:

L<http://www.easysoft.com>

L<http://www.openlinksw.com>

L<http://www.datadirect.com>

L<http://www.atinet.com/support/openrda_samples.asp>
 
Some useful tutorials:

Debugging Perl DBI:

L<http://www.easysoft.com/developer/languages/perl/dbi-debugging.html>

Enabling ODBC support in Perl with Perl DBI and DBD::ODBC:


L<http://www.easysoft.com/developer/languages/perl/dbi_dbd_odbc.html>

Perl DBI/DBD::ODBC Tutorial Part 1 - Drivers, Data Sources and Connection:

L<http://www.easysoft.com/developer/languages/perl/dbd_odbc_tutorial_part_1.html>

Perl DBI/DBD::ODBC Tutorial Part 2 - Introduction to retrieving data from your database:

L<http://www.easysoft.com/developer/languages/perl/index.html>

Perl DBI/DBD::ODBC Tutorial Part 3 - Connecting Perl on UNIX or Linux to Microsoft SQL Server:

L<http://www.easysoft.com/developer/languages/perl/sql_server_unix_tutorial.html>

Perl DBI - Put Your Data On The Web:

L<http://www.easysoft.com/developer/languages/perl/tutorial_data_web.html>

=head2 Frequently Asked Questions

Answers to common DBI and DBD::ODBC questions:

=over

=item How do I read more than N characters from a Memo | BLOB | LONG field?

See LongReadLen in the DBI docs.

Example:

 $dbh->{LongReadLen} = 20000;
 $sth = $dbh->prepare("select long_col from big_table");
 $sth->execute;
 etc

=item What is DBD::ODBC?

=item Why can't I connect?

=item Do I need an ODBC driver?

=item What is the ODBC driver manager?

These, general questions lead to needing definitions.

=over 4

=item ODBC Driver

The ODBC Driver is the driver that the ODBC manager uses to connect
and interact with the RDBMS.  You B<DEFINITELY> need this to connect to
any database.  For Win32, they are plentiful and installed with many
applications.  For Linux/Unix, you can find a fairly comprehensive list
at L<http://www.unixodbc.org/drivers.html>.

=item ODBC Driver Manager

The ODBC driver manager is the interface between an ODBC application
(DBD::ODBC in this case) and the ODBC driver. The driver manager
principally provides the ODBC API so ODBC applications may link with a
single shared object (or dll) and be able to talk to a range of ODBC
drivers. At run time the application provides a connection string
which defines the ODBC data source it wants to connect to and this in
turn defines the ODBC driver which will handle this data source. The
driver manager loads the requested ODBC driver and passes all ODBC API
calls on to the driver. In this way, an ODBC application can be built
and distributed without knowing which ODBC driver it will be using.

However, this is a rather simplistic description of what the driver
manager does. The ODBC driver manager also:

* Controls a repository of installed ODBC drivers (on UNIX this is the
file odbcinst.ini).

* Controls a repository of defined ODBC data sources (on UNIX these are
the files odbc.ini and .odbc.ini).

* Provides the ODBC driver APIs (SQLGetPrivateProfileString and
SQLWritePrivateProfileString) to read and write ODBC data source
attributes.

* Handles ConfigDSN which the driver exports to configure data
sources.

* Provides APIs to install and uninstall drivers (SQLInstallDriver).

* Maps ODBC versions e.g. so an ODBC 2.0 application can work with an
ODBC 3.0 driver and vice versa.

* Maps ODBC states between different versions of ODBC.

* Provides a cursor library for drivers which only support
forward-only cursors.

* Provides SQLDataSources and SQLDrivers so an application can find
out what ODBC drivers are installed and what ODBC data sources are
defined.

* Provides an ODBC administrator which driver writers can use to
install ODBC drivers and users can use to define ODBC data sources.

The ODBC Driver Manager is the piece of software which interacts with
the drivers for the application.  It "hides" some of the differences
between the drivers (i.e. if a function call is not supported by a
driver, it 'hides' that and informs the application that the call is
not supported.  DBD::ODBC needs this to talk to drivers.

Under Win32, you usually get the ODBC Driver Manager as part of the
OS.  Under Unix/Linux you may have to find and build the driver
manager yourself. The two main driver managers for Unix are unixODBC
(L<http://www.unixodbc.org>) and iODBC (L<http://www.iodbc.org>).

B<It is strongly advised you get an ODBC Driver Manager before trying to
build DBD::ODBC unless you intend linking DBD::ODBC directly with your
driver.>

For a reasonable description of ODBC on Unix/Linux see
L<http://www.easysoft.com/developer/interfaces/odbc/linux.html>

=item DBD::ODBC

DBD::ODBC uses the driver manager to talk to the ODBC driver(s) on
your system.  You need both a driver manager and driver installed and
tested before working with DBD::ODBC.  You need to have a DSN (see
below) configured and B<TESTED> before being able to test DBD::ODBC.

=item DSN (Data Source Name)


The DSN is a way of referring to a particular driver and database by
any name you wish.  The DSN is usually a key to a list of attributes
the ODBC driver needs to connect to the database (e.g. ip address and
port) but there is always a key which names the driver so the driver
manager knows which driver to use with which data source. Do no
confuse DSNs with ODBC connection strings or DBI's "$data_source" (the
first argument to L<DBI/connect>.

The $data_source argument to DBI is composed of 'dbi:DRIVER:something_else'
where DRIVER is the name of the DBD driver you want to use (ODBC of
course for DBD::ODBC). The "something_else" for DBD::ODBC can be a DSN
name or it can be a normal ODBC connection string.

An ODBC connection string consists of attribute/value pairs separated
with semicolons (;). You can replace "something_else" above with a
normal ODBC connection string but as a special case for DBD::ODBC you can
just use the DSN name without the usual ODBC connection string prefix
of "DSN=dsn_name".

e.g.

=over

=item dbi:ODBC:DSN=fred

ODBC connection string using fred DSN

=item dbi:ODBC:fred

Same as above (a special case).

=item dbi:ODBC:Driver={blah blah driver};Host=1.2.3.4;Port=1000;

This is known as a DSN-less connection string for obvious reasons.

=back

=back

=item Where do I get an ODBC driver manager for Unix/Linux?

DBD::ODBC used to come bundled with a driver manager but this became
inconvenient when the driver manager was updated.

The two main ODBC Driver Managers for Unix are unixODBC (L<http://www.unixodbc/org>) and iODBC (L<http://www.iodbc.org>).

If you are running a packaged Linux like RedHat, Ubuntu, Fedora, Suse
etc etc you'll usually find it packaged with unixODBC and using the
package manager to install it is fairly straight forward. However,
make sure that if the driver manager is split into multiple packages
you install the development package as well as that contains the C
header files required by DBD::ODBC.

If you cannot find an ODBC Driver Manager package for your OS you can
download the source tar files for either of the driver managers above
and build it yourself.

=item How do I access a MS SQL Server database from Linux?

You have loads of choices (in no particular order):

* using DBI::ProxyServer

* using a commercial ODBC Driver or bridge like the ones from Easysoft
or Openlink.

* using FreeTDS an open source TDS library which includes an ODBC Driver.

* using DBD::Sybase and Sybase libraries.

=item How do I access a MS-Access database from Linux?

There are basically two choices:

* a commercial ODBC Bridge like the ones from Easysoft or OpenLink.

* using mdbtools although as of writing it has not been updated since
June 2004, only provides read access and seems to be a little buggy.

=item Almost all of my tests for DBD::ODBC fail. They complain about
not being able to connect or the DSN is not found.

Please, please test your configuration of ODBC and driver before
trying to test DBD::ODBC. Most of the time, this stems from the fact
that the DSN (or ODBC) is not configured properly. unixODBC comes with
a small program isql and iODBC comes with odbctest and you should use
these to test your ODBC configuration is working properly first.

=item I'm attempting to bind a Long Var char (or other specific type)
and the binding is not working.

The code I'm using is below:

	$sth->bind_param(1, $str, $DBI::SQL_LONGVARCHAR);
                                 ^^^

The problem is that DBI::SQL_LONGVARCHAR is not the same as
$DBI::SQL_LONGVARCHAR and that $DBI::SQL_LONGVARCHAR is an error!

It should be:

	$sth->bind_param(1, $str, DBI::SQL_LONGVARCHAR);

=item Does DBD::ODBC support Multiple Active Statements?

Multiple Active Statements (MAS) are concurrent statements created
from the same database handle which both have pending actions on them
(e.g. they both have executed a select statement but not retrieved all
the available rows yet).

DBD::ODBC does support MAS but whether you can actually use MAS is
down to the ODBC Driver.

By default MS SQL Server did not used to support multiple active
statements if any of them were select statements. You could get around
this (with caution) by changing to a dynamic cursor. There is a "hack"
in DBD::ODBC which can be used to enable MAS but you have to fully
understand the implications of doing so(see
L<DBD/ODBC/odbc_SQL_ROWSET_SIZE> and L<DBD/ODBC/odbc_cursortype>).

In MS SQL Server 2005, there is a new thing called MARS (Multiple
Active Result Sets) which allows multiple active select statements but
it has some nasty implications it you are also doing transactions.

For other drivers it depends. I believe various Oracle ODBC drivers
doe support multiple active statements as myodbc does.

Think carefully before using multiple active statements. It is
probably not portable and there is nearly always a better way of doing
it.

If anyone wants to report success with a particular driver and
multiple active statements I will collect them here.

=item Why do I get "Datetime field overflow" when attempting to insert a
date into Oracle?

If you are using the Oracle or Microsoft ODBC drivers then you may get
the following error when inserting dates into an Oracle database:

  [Oracle][ODBC]Datetime field overflow. (SQL-22008)

If you do then check v$nls_parameters and v$parameter to see if you are
using a date format containing the RR format. e.g.,

  select * from v$nls_parameters where parameter = 'NLS_DATE_FORMAT'
  select * from v$parameter where name = 'nls_date_format'

If you see a date format like 'DD-MON-RR' (e.g., contains an RR) then
all I can suggest is you change the date format for your session as I
have never been able to bind a date using this format. You can do this
with:

  alter session set nls_date_format='YYYY/MM/DD'

and use any format you like but keep away from 'RR'.

You can find some test code in the file mytest/rtcpan_28821.pl which
demonstrates this problem. This was originally a rt.cpan issue which
can be found at L<http://rt.cpan.org/Ticket/Display.html?id=28821>.

As an aside, if anyone is reading this and can shed some light on the problem
I'd love to hear from you. The technical details are:

  create table rtcpan28821 (a date)
  insert into rtcpan28821 values('23-MAR-62') fails

Looking at the ODBC trace, SQLDescribeParam returns:

  data type: 93, SQL_TYPE_TIMESTAMP
  size: 19
  decimal digits: 0
  nullable: 1

and DBD::ODBC calls SQLBindParameter with:

  ValueType: SQL_C_CHAR
  ParameterType: SQL_TYPE_TIMESTAMP
  ColumnSize: 9
  DecimalDigits: 0
  Data: 23-MAR-62
  BufferLength: 9

=item Why do my SQL Server temporary objects disappear?

If you are creating temporary objects (e.g., temporary tables) in
SQL Server you find they have disappeared when you attempt to use
them. Temporary objects only have a lifetime of the session they
are created in but in addition, they cannot be created using
prepare/execute. e.g., the following fails:

  $s = $h->prepare('select * into #tmp from mytable');
  $s->execute;
  $s = $h->selectall_arrayref('select * from #tmp');

with "Invalid object name '#tmp'". Your should read
L<http://technet.microsoft.com/en-US/library/ms131667.aspx> which
basically says I<Prepared statements cannot be used to create
temporary objects on SQL Server 2000 or later...>. The proper way to
avoid this is to use the C<do> method but if you cannot do that then
you need to add the L</odbc_exec_direct> attribute to your prepare as
follows:

  my $s = $h->prepare('select * into #tmp from mytable',
                      { odbc_exec_direct => 1});

See L</odbc_exec_direct>.

=back

=cut
