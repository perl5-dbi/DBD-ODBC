/* $Id: dbdimp.c,v 1.11 1998/08/14 18:28:20 timbo Exp $
 * 
 * portions Copyright (c) 1994,1995,1996,1997  Tim Bunce
 * portions Copyright (c) 1997 Thomas K. Wenrich
 * portions Copyright (c) 1997 Jeff Urlwin
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Artistic License, as specified in the Perl README file.
 *
 */

#include "ODBC.h"

static const char *S_SqlTypeToString (SWORD sqltype);
static const char *S_SqlCTypeToString (SWORD sqltype);
static const char *cSqlTables = "SQLTables(%s)";
static const char *cSqlColumns = "SQLColumns(%s,%s,%s,%s)";
static const char *cSqlGetTypeInfo = "SQLGetTypeInfo(%d)";

/* for sanity/ease of use with potentially null strings */
#define XXSAFECHAR(p) ((p) ? (p) : "(null)")

void dbd_error _((SV *h, RETCODE err_rc, char *what));

DBISTATE_DECLARE;

void
   dbd_init(dbistate)
   dbistate_t *dbistate;
{
    DBIS = dbistate;
}


int
   build_results(sth)
   SV *	 sth;
{
    RETCODE rc;
    D_imp_sth(sth);
    dTHR;
    
    if (DBIS->debug >= 2)
	fprintf(DBILOGFP, "    build_results sql f%d\n\t%s\n",
		imp_sth->hstmt, imp_sth->statement);

    /* init sth pointers */
    imp_sth->fbh = NULL;
    imp_sth->ColNames = NULL;
    imp_sth->RowBuffer = NULL;
    imp_sth->RowCount = -1;
    imp_sth->eod = -1;

    if (!dbd_describe(sth, imp_sth)) {
	SQLFreeStmt(imp_sth->hstmt, SQL_DROP);
	imp_sth->hstmt = SQL_NULL_HSTMT;
	return 0; /* dbd_describe already called dbd_error()	*/
    }

    if (dbd_describe(sth, imp_sth) <= 0)
	return 0;

    DBIc_IMPSET_on(imp_sth);

    imp_sth->RowCount = -1;
    rc = SQLRowCount(imp_sth->hstmt, &imp_sth->RowCount);
    dbd_error(sth, rc, "dbd_st_tables/SQLRowCount");
    if (rc != SQL_SUCCESS) {
	return -1;
    }

    DBIc_ACTIVE_on(imp_sth); /* XXX should only set for select ?	*/
    imp_sth->eod = SQL_SUCCESS;
    return 1;
}

int
   dbd_discon_all(drh, imp_drh)
   SV *drh;
imp_drh_t *imp_drh;
{
    dTHR;
    /* The disconnect_all concept is flawed and needs more work */
    if (!dirty && !SvTRUE(perl_get_sv("DBI::PERL_ENDING",0))) {
	sv_setiv(DBIc_ERR(imp_drh), (IV)1);
	sv_setpv(DBIc_ERRSTR(imp_drh),
		 (char*)"disconnect_all not implemented");
	DBIh_EVENT2(drh, ERROR_event,
		    DBIc_ERR(imp_drh), DBIc_ERRSTR(imp_drh));
	return FALSE;
    }  
    return FALSE;
}


void
   dbd_db_destroy(dbh, imp_dbh)
   SV *dbh;
imp_dbh_t *imp_dbh;
{
    if (DBIc_ACTIVE(imp_dbh))
	dbd_db_disconnect(dbh, imp_dbh);
    /* Nothing in imp_dbh to be freed	*/

    DBIc_IMPSET_off(imp_dbh);
}


/*------------------------------------------------------------
connecting to a data source.
Allocates henv and hdbc.
------------------------------------------------------------*/
int
   dbd_db_login(dbh, imp_dbh, dbname, uid, pwd)
   SV *dbh;
imp_dbh_t *imp_dbh;
char *dbname;
char *uid;
char *pwd;
{
    D_imp_drh_from_dbh;
    int ret;
    dTHR;

    RETCODE rc;

    /*
     * for SQLDriverConnect
     */
    char szConnStrOut[2048];
#ifdef DBD_SOLID
	SWORD cbConnStrOut;
#else
    SQLSMALLINT cbConnStrOut;
#endif

    if (!imp_drh->connects) {
	rc = SQLAllocEnv(&imp_drh->henv);
	dbd_error(dbh, rc, "db_login/SQLAllocEnv");
	if (!SQL_ok(rc))
	    return 0;
    }
    imp_dbh->henv = imp_drh->henv;	/* needed for dbd_error */

    rc = SQLAllocConnect(imp_drh->henv, &imp_dbh->hdbc);
    if (!SQL_ok(rc)) {
	dbd_error(dbh, rc, "db_login/SQLAllocConnect");
	if (imp_drh->connects == 0) {
	    SQLFreeEnv(imp_drh->henv);
	    imp_drh->henv = SQL_NULL_HENV;
	}
	return 0;
    }

#ifndef DBD_ODBC_NO_SQLDRIVERCONNECT
    if (DBIS->debug >= 2)
		fprintf(DBILOGFP, "Driver connect '%s', '%s', '%s'\n", dbname, uid, pwd);

    /*
     * SQLDriverConnect handles/maps/fixes db connections and can optionally
     * add a dialog box to the application.  
     */
    rc = SQLDriverConnect(imp_dbh->hdbc,
			  0, /* no hwnd */
			  dbname,
			  strlen(dbname),
			  szConnStrOut,
			  sizeof(szConnStrOut),
			  &cbConnStrOut,
			  SQL_DRIVER_NOPROMPT /* no dialog box (for now) */
			 );

#else
	/* if we are using something that can not handle SQLDriverconnect,
	 * then set rc to a not OK state
	 */
	rc = SQL_ERROR;
#endif
    /*
     * if SQLDriverConnect fails, then call SQLConnect, just in case
     * perform some tracing at level 4+ (detail of why SQLDriverConnect failed)
     * and level 2+ just to indicate that we are trying SQLConnect.
     */
    if (!SQL_ok(rc)) {
#ifdef DBD_ODBC_NO_SQLDRIVERCONNECT
		fprintf(DBILOGFP, "SQLDriverConnect unsupported.\n");
#else		
	if (DBIS->debug > 3) {
	    UCHAR sqlstate[SQL_SQLSTATE_SIZE+1];
	    /* ErrorMsg must not be greater than SQL_MAX_MESSAGE_LENGTH (says spec) */
	    UCHAR ErrorMsg[SQL_MAX_MESSAGE_LENGTH];
	    SWORD ErrorMsgLen;
	    SDWORD NativeError;

	    rc=SQLError(imp_dbh->henv, imp_dbh->hdbc, 0,
			sqlstate, &NativeError,
			ErrorMsg, sizeof(ErrorMsg)-1, &ErrorMsgLen);

	    if (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO)
		fprintf(DBILOGFP, "SQLDriverConnect failed: %s %s\n", sqlstate, ErrorMsgLen);
	}
#endif /* DriverConnect supported */
	if (DBIS->debug >= 2)
	    fprintf(DBILOGFP, "SQLConnect '%s', '%s', '%s'\n", dbname, uid, pwd);

	rc = SQLConnect(imp_dbh->hdbc,
			dbname, strlen(dbname),
			uid, strlen(uid),
			pwd, strlen(pwd));
    }

    if (!SQL_ok(rc)) {
	dbd_error(dbh, rc, "db_login/SQLConnect");
	SQLFreeConnect(imp_dbh->hdbc);
	if (imp_drh->connects == 0) {
	    SQLFreeEnv(imp_drh->henv);
	    imp_drh->henv = SQL_NULL_HENV;
	}
	return 0;
    }

    /* DBI spec requires AutoCommit on */
    rc = SQLSetConnectOption(imp_dbh->hdbc,
			     SQL_AUTOCOMMIT, SQL_AUTOCOMMIT_ON);
    if (!SQL_ok(rc)) {
		dbd_error(dbh, rc, "dbd_db_login/SQLSetConnectOption");
		SQLFreeConnect(imp_dbh->hdbc);
		if (imp_drh->connects == 0) {
			SQLFreeEnv(imp_drh->henv);
			imp_drh->henv = SQL_NULL_HENV;
		}
		return 0;
	}
    DBIc_set(imp_dbh,DBIcf_AutoCommit, 1);

    imp_drh->connects++;
    DBIc_IMPSET_on(imp_dbh);	/* imp_dbh set up now			*/
    DBIc_ACTIVE_on(imp_dbh);	/* call disconnect before freeing	*/
    return 1;
}


int
   dbd_db_disconnect(dbh, imp_dbh)
   SV *dbh;
imp_dbh_t *imp_dbh;
{
    RETCODE rc;
    D_imp_drh_from_dbh;
    dTHR;

    /* We assume that disconnect will always work	*/
    /* since most errors imply already disconnected.	*/
    DBIc_ACTIVE_off(imp_dbh);

    // If not autocommit, should we rollback?  I don't think that's appropriate.
    rc = SQLDisconnect(imp_dbh->hdbc);
    if (!SQL_ok(rc)) {
		dbd_error(dbh, rc, "db_disconnect/SQLDisconnect");
		// return 0;	/* XXX if disconnect fails, fall through... */
    }

    SQLFreeConnect(imp_dbh->hdbc);
    imp_dbh->hdbc = SQL_NULL_HDBC;
    imp_drh->connects--;
    if (imp_drh->connects == 0) {
	SQLFreeEnv(imp_drh->henv);
    }
    /* We don't free imp_dbh since a reference still exists	*/
    /* The DESTROY method is the only one to 'free' memory.	*/
    /* Note that statement objects may still exists for this dbh!	*/

    return 1;
}


int
   dbd_db_commit(dbh, imp_dbh)
   SV *dbh;
imp_dbh_t *imp_dbh;
{
    RETCODE rc;
    dTHR;

    rc = SQLTransact(imp_dbh->henv, imp_dbh->hdbc, SQL_COMMIT);
    if (!SQL_ok(rc)) {
	dbd_error(dbh, rc, "db_commit/SQLTransact");
	return 0;
    }
    return 1;
}

int
   dbd_db_rollback(dbh, imp_dbh)
   SV *dbh;
imp_dbh_t *imp_dbh;
{
    RETCODE rc;
    dTHR;

    rc = SQLTransact(imp_dbh->henv, imp_dbh->hdbc, SQL_ROLLBACK);
    if (!SQL_ok(rc)) {
	dbd_error(dbh, rc, "db_rollback/SQLTransact");
	return 0;
    }
    return 1;
}


/*------------------------------------------------------------
replacement for odbc_error.
empties entire ODBC error queue.
------------------------------------------------------------*/
void
   dbd_error(h, err_rc, what)
   SV *h;
RETCODE err_rc;
char *what;
{
    D_imp_xxh(h);
    dTHR;

    struct imp_dbh_st *imp_dbh = NULL;
    struct imp_sth_st *imp_sth = NULL;
    HENV henv = SQL_NULL_HENV;
    HDBC hdbc = SQL_NULL_HDBC;
    HSTMT hstmt = SQL_NULL_HSTMT;
    SV *errstr;

    if (err_rc == SQL_SUCCESS && DBIS->debug<3)	/* nothing to do */
	return;

    switch(DBIc_TYPE(imp_xxh)) {
	case DBIt_ST:
	    imp_sth = (struct imp_sth_st *)(imp_xxh);
	    imp_dbh = (struct imp_dbh_st *)(DBIc_PARENT_COM(imp_sth));
	    hstmt = imp_sth->hstmt;
	    break;
	case DBIt_DB:
	    imp_dbh = (struct imp_dbh_st *)(imp_xxh);
	    break;
	default:
	    croak("panic: dbd_error on bad handle type");
    }
    hdbc = imp_dbh->hdbc;
    henv = imp_dbh->henv;

    errstr = DBIc_ERRSTR(imp_xxh);
    sv_setpvn(errstr, "", 0);
    sv_setiv(DBIc_ERR(imp_xxh), (IV)err_rc);
    /* sqlstate isn't set for SQL_NO_DATA returns  */
    sv_setpvn(DBIc_STATE(imp_xxh), "00000", 5);

    while(henv != SQL_NULL_HENV) {
	UCHAR sqlstate[SQL_SQLSTATE_SIZE+1];
	/* ErrorMsg must not be greater than SQL_MAX_MESSAGE_LENGTH (says spec) */
	UCHAR ErrorMsg[SQL_MAX_MESSAGE_LENGTH];
	SWORD ErrorMsgLen;
	SDWORD NativeError;
	RETCODE rc = 0;

	if (DBIS->debug >= 3)
	    fprintf(DBILOGFP, "dbd_error: err_rc=%d rc=%d s/d/e: %d/%d/%d\n", 
		    err_rc, rc, hstmt,hdbc,henv);

	while( (rc=SQLError(henv, hdbc, hstmt,
			    sqlstate, &NativeError,
			    ErrorMsg, sizeof(ErrorMsg)-1, &ErrorMsgLen
			   )) == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) {
	    sv_setpvn(DBIc_STATE(imp_xxh), sqlstate, 5);
	    if (SvCUR(errstr) > 0) {
		sv_catpv(errstr, "\n");
		/* JLU: attempt to get a reasonable error	*/
		/* from first SQLError result on lowest handle	*/
		sv_setpv(DBIc_ERR(imp_xxh), sqlstate);
	    }
	    sv_catpvn(errstr, ErrorMsg, ErrorMsgLen);
	    sv_catpv(errstr, " (SQL-");
	    sv_catpv(errstr, sqlstate);
	    sv_catpv(errstr, ")");

	    /* maybe bad way to add hint about invalid transaction
	     * state upon disconnect...
	     */
	    if (what && !strcmp(sqlstate, "25000") && !strcmp(what, "db_disconnect/SQLDisconnect")) {
		sv_catpv(errstr, " You need to commit before disconnecting! ");
	    }
	    if (DBIS->debug >= 3)
		fprintf(DBILOGFP, 
			"dbd_error: SQL-%s (native %d): %s\n",
			sqlstate, NativeError, SvPVX(errstr));
	}
	if (rc != SQL_NO_DATA_FOUND) {	/* should never happen */
	    if (DBIS->debug)
		fprintf(DBILOGFP, 
			"dbd_error: SQLError returned %d unexpectedly.\n", rc);
	    if (!SvTRUE(errstr)) { /* set some values to indicate the problem */
		sv_setpvn(DBIc_STATE(imp_xxh), "IM008", 5); /* "dialog failed" */
		sv_catpv(errstr, "(Unable to fetch information about the error)");
	    }
	}

	/* climb up the tree each time round the loop		*/
	if      (hstmt != SQL_NULL_HSTMT) hstmt = SQL_NULL_HSTMT;
	else if (hdbc  != SQL_NULL_HDBC)  hdbc  = SQL_NULL_HDBC;
	else henv = SQL_NULL_HENV;	/* done the top		*/
    }

    if (err_rc != SQL_SUCCESS) {
	if (what) {
	    char buf[10];
	    sprintf(buf, " err=%d", err_rc);
	    sv_catpv(errstr, "(DBD: ");
	    sv_catpv(errstr, what);
	    sv_catpv(errstr, buf);
	    sv_catpv(errstr, ")");
	}

	DBIh_EVENT2(h, ERROR_event, DBIc_ERR(imp_xxh), errstr);

	if (DBIS->debug >= 2)
	    fprintf(DBILOGFP, "%s error %d recorded: %s\n",
		    what, err_rc, SvPV(errstr,na));
    }
}


/*-------------------------------------------------------------------------
dbd_preparse: 
- scan for placeholders (? and :xx style) and convert them to ?.
- builds translation table to convert positional parameters of the 
execute() call to :nn type placeholders.
We need two data structures to translate this stuff:
- a hash to convert positional parameters to placeholders
- an array, representing the actual '?' query parameters.
%param = (name1=>plh1, name2=>plh2, ..., name_n=>plh_n)   #
@qm_param = (\$param{'name1'}, \$param{'name2'}, ...) 
-------------------------------------------------------------------------*/
void
   dbd_preparse(imp_sth, statement)
   imp_sth_t *imp_sth;
char *statement;
{
    dTHR;
    bool in_literal = FALSE;
    char *src, *start, *dest;
    phs_t phs_tpl, *phs;
    SV *phs_sv;
    int idx=0, style=0, laststyle=0;
    int param = 0;
    STRLEN namelen;
    char name[256];
    SV **svpp;
    char ch;

    /* allocate room for copy of statement with spare capacity	*/
    imp_sth->statement = (char*)safemalloc(strlen(statement)+1);

    /* initialize phs ready to be cloned per placeholder	*/
    memset(&phs_tpl, 0, sizeof(phs_tpl));
    phs_tpl.ftype = 1;	/* VARCHAR2 */
    phs_tpl.sv = &sv_undef;

    src  = statement;
    dest = imp_sth->statement;
    while(*src) {
	if (*src == '\'')
	    in_literal = ~in_literal;
	if ((*src != ':' && *src != '?') || in_literal) {
	    *dest++ = *src++;
	    continue;
	}
	start = dest;			/* save name inc colon	*/ 
	ch = *src++;
	if (ch == '?') {                /* X/Open standard	*/ 
	    idx++;
	    sprintf(name, "%d", idx);
	    *dest++ = ch;
	    style = 3;
	}
	else if (isDIGIT(*src)) {       /* ':1'		*/
	    char *p = name;
	    *dest++ = '?';
	    idx = atoi(src);
	    while(isDIGIT(*src))
		*p++ = *src++;
	    *p = 0;
	    style = 1;
	} 
	else if (isALNUM(*src)) {       /* ':foo'	*/
	    char *p = name;
	    *dest++ = '?';

	    while(isALNUM(*src))	/* includes '_'	*/
		*p++ = *src++;
	    *p = 0;
	    style = 2;
	} 
	else {			/* perhaps ':=' PL/SQL construct */
	    *dest++ = ch;
	    continue;
	}
	*dest = '\0';			/* handy for debugging	*/
	if (laststyle && style != laststyle)
	    croak("Can't mix placeholder styles (%d/%d)",style,laststyle);
	laststyle = style;

	if (imp_sth->all_params_hv == NULL)
	    imp_sth->all_params_hv = newHV();
	namelen = strlen(name);

	svpp = hv_fetch(imp_sth->all_params_hv, name, namelen, 0);
	if (svpp == NULL) {
	    /* create SV holding the placeholder */
	    phs_sv = newSVpv((char*)&phs_tpl, sizeof(phs_tpl)+namelen+1);
	    phs = (phs_t*)SvPVX(phs_sv);
	    strcpy(phs->name, name);
	    phs->idx = idx;

	    /* store placeholder to all_params_hv */
	    svpp = hv_store(imp_sth->all_params_hv, name, namelen, phs_sv, 0);
	}
    }
    *dest = '\0';
    if (imp_sth->all_params_hv) {
	DBIc_NUM_PARAMS(imp_sth) = (int)HvKEYS(imp_sth->all_params_hv);
	if (DBIS->debug >= 2)
	    fprintf(DBILOGFP, "    dbd_preparse scanned %d distinct placeholders\n",
		    (int)DBIc_NUM_PARAMS(imp_sth));
    }
}


int
   dbd_st_tables(dbh, sth, qualifier, table_type)
   SV *dbh;
SV *sth;
char *qualifier;
char *table_type;
{
    D_imp_dbh(dbh);
    D_imp_sth(sth);
    RETCODE rc;
    SV **svp;
    char cname[128];					/* cursorname */
    dTHR;

    imp_sth->henv = imp_dbh->henv;	/* needed for dbd_error */
    imp_sth->hdbc = imp_dbh->hdbc;

    imp_sth->done_desc = 0;
    rc = SQLAllocStmt(imp_dbh->hdbc, &imp_sth->hstmt);
    if (rc != SQL_SUCCESS) {
	dbd_error(sth, rc, "st_tables/SQLAllocStmt");
	return 0;
    }

    /* just for sanity, later.  Any internals that may rely on this (including */
    /* debugging) will have valid data */
    imp_sth->statement = (char *)safemalloc(strlen(cSqlTables)+strlen(qualifier)+1);
    sprintf(imp_sth->statement, cSqlTables, qualifier);

    rc = SQLTables(imp_sth->hstmt,
		   0, SQL_NTS,			/* qualifier */
		   0, SQL_NTS,			/* schema/user */
		   0, SQL_NTS,			/* table name */
		   table_type && *table_type ? table_type : 0, SQL_NTS		/* type (view, table, etc) */
		  );

    if (DBIS->debug >= 2)
	fprintf(DBILOGFP, "   Tables result %d (%s)\n",
		rc, table_type ? table_type : "(null)");

    dbd_error(sth, rc, "st_tables/SQLTables");
    if (!SQL_ok(rc)) {
	SQLFreeStmt(imp_sth->hstmt, SQL_DROP);
	imp_sth->hstmt = SQL_NULL_HSTMT;
	return 0;
    }

    return build_results(sth);
}


int
   dbd_st_prepare(sth, imp_sth, statement, attribs)
   SV *sth;
imp_sth_t *imp_sth;
char *statement;
SV *attribs;
{
    dTHR;
    D_imp_dbh_from_sth;
    RETCODE rc;
    SV **svp;
    char cname[128];		/* cursorname */

    imp_sth->done_desc = 0;
    imp_sth->henv = imp_dbh->henv;	/* needed for dbd_error */
    imp_sth->hdbc = imp_dbh->hdbc;

    rc = SQLAllocStmt(imp_dbh->hdbc, &imp_sth->hstmt);
    if (!SQL_ok(rc)) {
	dbd_error(sth, rc, "st_prepare/SQLAllocStmt");
	return 0;
    }

    /* scan statement for '?', ':1' and/or ':foo' style placeholders	*/
    dbd_preparse(imp_sth, statement);

    /* parse the (possibly edited) SQL statement */
    rc = SQLPrepare(imp_sth->hstmt, 
		    imp_sth->statement, strlen(imp_sth->statement));
    if (!SQL_ok(rc)) {
	dbd_error(sth, rc, "st_prepare/SQLPrepare");
	SQLFreeStmt(imp_sth->hstmt, SQL_DROP);
	imp_sth->hstmt = SQL_NULL_HSTMT;
	return 0;
    }

    if (DBIS->debug >= 2)
	fprintf(DBILOGFP, "    dbd_st_prepare'd sql f%d\n\t%s\n",
		imp_sth->hstmt, imp_sth->statement);

    /* init sth pointers */
    imp_sth->henv = imp_dbh->henv;
    imp_sth->hdbc = imp_dbh->hdbc;
    imp_sth->fbh = NULL;
    imp_sth->ColNames = NULL;
    imp_sth->RowBuffer = NULL;
    imp_sth->RowCount = -1;
    imp_sth->eod = -1;

    DBIc_IMPSET_on(imp_sth);
    return 1;
}


int 
   dbtype_is_string(int bind_type)
{
    switch(bind_type) {
	case SQL_C_CHAR:
	case SQL_C_BINARY:
	    return 1;
    }
    return 0;
}    


static const char *
   S_SqlTypeToString (SWORD sqltype)
{
    switch(sqltype) {
	case SQL_CHAR:	return "CHAR";
	case SQL_NUMERIC:	return "NUMERIC";
	case SQL_DECIMAL:	return "DECIMAL";
	case SQL_INTEGER:	return "INTEGER";
	case SQL_SMALLINT:	return "SMALLINT";
	case SQL_FLOAT:	return "FLOAT";
	case SQL_REAL:	return "REAL";
	case SQL_DOUBLE:	return "DOUBLE";
	case SQL_VARCHAR:	return "VARCHAR";
#ifdef SQL_WVARCHAR
	case SQL_WVARCHAR:	return "UNICODE VARCHAR"; /* added for SQLServer 7 ntext type 2/24/2000 */
#endif
#ifdef SQL_WLONGVARCHAR
	case SQL_WLONGVARCHAR: return "UNICODE LONG VARCHAR";
#endif
	case SQL_DATE:	return "DATE";
	case SQL_TIME:	return "TIME";
	case SQL_TIMESTAMP:	return "TIMESTAMP";
	case SQL_LONGVARCHAR: return "LONG VARCHAR";
	case SQL_BINARY:	return "BINARY";
	case SQL_VARBINARY: return "VARBINARY";
	case SQL_LONGVARBINARY: return "LONG VARBINARY";
	case SQL_BIGINT:	return "BIGINT";
	case SQL_TINYINT:	return "TINYINT";
	case SQL_BIT:	return "BIT";
    }
    return "unknown";
}


static const char *
   S_SqlCTypeToString (SWORD sqltype)
{
    static char s_buf[100];
#define s_c(x) case x: return #x
    switch(sqltype) {
	s_c(SQL_C_CHAR);
	s_c(SQL_C_BIT);
	s_c(SQL_C_STINYINT);
	s_c(SQL_C_UTINYINT);
	s_c(SQL_C_SSHORT);
	s_c(SQL_C_USHORT);
	s_c(SQL_C_FLOAT);
	s_c(SQL_C_DOUBLE);
	s_c(SQL_C_BINARY);
	s_c(SQL_C_DATE);
	s_c(SQL_C_TIME);
	s_c(SQL_C_TIMESTAMP);
    }
#undef s_c
    sprintf(s_buf, "(unknown CType %d)", sqltype);
    return s_buf;
}


/*
 * describes the output variables of a query,
 * allocates buffers for result rows,
 * and binds this buffers to the statement.
 */
int
   dbd_describe(h, imp_sth)
   SV *h;
imp_sth_t *imp_sth;
{
    dTHR;
    RETCODE rc;

    UCHAR *cbuf_ptr;		
    UCHAR *rbuf_ptr;		

    int t_cbufl=0;		/* length of all column names */
    int i;
    imp_fbh_t *fbh;
    int t_dbsize = 0;		/* size of native type */
    int t_dsize = 0;		/* display size */
    SWORD num_fields;

    if (imp_sth->done_desc)
	return 1;	/* success, already done it */

    rc = SQLNumResultCols(imp_sth->hstmt, &num_fields);
    if (!SQL_ok(rc)) {
	dbd_error(h, rc, "dbd_describe/SQLNumResultCols");
	return 0;
    }
    imp_sth->done_desc = 1;	/* assume ok from here on */

    DBIc_NUM_FIELDS(imp_sth) = num_fields;

    if (DBIS->debug >= 2)
	fprintf(DBILOGFP, "    dbd_describe sql %d: num_fields=%d\n",
		imp_sth->hstmt, DBIc_NUM_FIELDS(imp_sth));

    if (num_fields == 0) {
	if (DBIS->debug >= 2)
	    fprintf(DBILOGFP,
		    "    dbd_describe skipped (no result cols) (sql f%d)\n",
		    imp_sth->hstmt);
	return 1;
    }

    /* allocate field buffers				*/
    Newz(42, imp_sth->fbh, num_fields, imp_fbh_t);

    /* Pass 1: Get space needed for field names, display buffer and dbuf */
    for (fbh=imp_sth->fbh, i=0; i < num_fields; i++, fbh++) {
	UCHAR ColName[256];

	rc = SQLDescribeCol(imp_sth->hstmt, 
			    i+1, 
			    ColName, sizeof(ColName)-1,
			    &fbh->ColNameLen,
			    &fbh->ColSqlType,
			    &fbh->ColDef,
			    &fbh->ColScale,
			    &fbh->ColNullable);
	if (!SQL_ok(rc)) {	/* should never fail */
	    dbd_error(h, rc, "describe/SQLDescribeCol");
	    break;
	}

	ColName[fbh->ColNameLen] = 0;

	t_cbufl += fbh->ColNameLen;

#ifdef SQL_COLUMN_DISPLAY_SIZE
	rc = SQLColAttributes(imp_sth->hstmt,i+1,SQL_COLUMN_DISPLAY_SIZE,
			      NULL, 0, NULL ,&fbh->ColDisplaySize);
	if (!SQL_ok(rc)) {
	    dbd_error(h, rc, "describe/SQLColAttributes/SQL_COLUMN_DISPLAY_SIZE");
	    break;
	}
	fbh->ColDisplaySize += 1; /* add terminator */
#else
	/* XXX we should at least allow an attribute to set this */
	fbh->ColDisplaySize = 2001; /* XXX! */
#endif

#ifdef SQL_COLUMN_LENGTH
	rc = SQLColAttributes(imp_sth->hstmt,i+1,SQL_COLUMN_LENGTH,
			      NULL, 0, NULL ,&fbh->ColLength);
	if (!SQL_ok(rc)) {
	    dbd_error(h, rc, "describe/SQLColAttributes/SQL_COLUMN_LENGTH");
	    break;
	}
#else
	/* XXX we should at least allow an attribute to set this */
	fbh->ColLength = 2001;	/* XXX! */
#endif

	/* change fetched size for some types
	 */
	fbh->ftype = SQL_C_CHAR;
	switch(fbh->ColSqlType)
	{
	    // patch to allow binary types 3/24/99 courtesy of Jon Smirl
	    case SQL_VARBINARY:
	    case SQL_BINARY:
		fbh->ftype = SQL_C_BINARY;
		break;
	    case SQL_LONGVARBINARY:
		fbh->ftype = SQL_C_BINARY;
		fbh->ColDisplaySize = DBIc_LongReadLen(imp_sth);
		break;
#ifdef SQL_WLONGVARCHAR
	    case SQL_WLONGVARCHAR:	/* added for SQLServer 7 ntext type */
#endif
	    case SQL_LONGVARCHAR:
		fbh->ColDisplaySize = DBIc_LongReadLen(imp_sth)+1;
		break;
#ifdef TIMESTAMP_STRUCT	/* XXX! */
	    case SQL_TIMESTAMP:
		fbh->ftype = SQL_C_TIMESTAMP;
		fbh->ColDisplaySize = sizeof(TIMESTAMP_STRUCT);
		break;
#endif
	}
	if (fbh->ftype != SQL_C_CHAR) {
	    t_dbsize += t_dbsize % sizeof(int);     /* alignment */
	}
	t_dbsize += fbh->ColDisplaySize;

	if (DBIS->debug >= 2)
	    fprintf(DBILOGFP, 
		    "      col %2d: %-8s len=%3d disp=%3d, prec=%3d scale=%d\n", 
		    i+1, S_SqlTypeToString(fbh->ColSqlType),
		    fbh->ColLength, fbh->ColDisplaySize,
		    fbh->ColDef, fbh->ColScale);
    }
    if (!SQL_ok(rc)) {
	/* dbd_error called above */
	Safefree(imp_sth->fbh);
	return 0;
    }

    /* allocate a buffer to hold all the column names	*/
    Newz(42, imp_sth->ColNames, t_cbufl + num_fields, UCHAR);
    /* allocate Row memory */
    Newz(42, imp_sth->RowBuffer, t_dbsize + num_fields, UCHAR);

    /* Second pass:
    - get column names
    - bind column output
*/

    cbuf_ptr = imp_sth->ColNames;
    rbuf_ptr = imp_sth->RowBuffer;

    for(i=0, fbh = imp_sth->fbh; i < num_fields && SQL_ok(rc); i++, fbh++)
    {
	switch(fbh->ftype)
	{
	    case SQL_C_BINARY:
	    case SQL_C_TIMESTAMP:
		rbuf_ptr += (rbuf_ptr - imp_sth->RowBuffer) % sizeof(int);
		break;
	}

	rc = SQLDescribeCol(imp_sth->hstmt, 
			    i+1, 
			    cbuf_ptr, 255,
			    &fbh->ColNameLen, &fbh->ColSqlType,
			    &fbh->ColDef, &fbh->ColScale, &fbh->ColNullable
			   );
	if (!SQL_ok(rc)) {	/* should never fail */
	    dbd_error(h, rc, "describe/SQLDescribeCol");
	    break;
	}

	fbh->ColName = cbuf_ptr;
	cbuf_ptr[fbh->ColNameLen] = 0;
	cbuf_ptr += fbh->ColNameLen+1;

	fbh->data = rbuf_ptr;
	rbuf_ptr += fbh->ColDisplaySize;

	/* Bind output column variables */
	rc = SQLBindCol(imp_sth->hstmt,
			i+1,
			fbh->ftype, fbh->data,
			fbh->ColDisplaySize, &fbh->datalen);
	if (DBIS->debug >= 2)
	    fprintf(DBILOGFP, 
		    "      col %2d: '%s' sqltype=%s, ctype=%s, maxlen=%d\n",
		    i+1, fbh->ColName,
		    S_SqlTypeToString(fbh->ColSqlType),
		    S_SqlCTypeToString(fbh->ftype),
		    fbh->ColDisplaySize);
	if (!SQL_ok(rc)) {
	    dbd_error(h, rc, "describe/SQLBindCol");
	    break;
	}
    } /* end pass 2 */

    if (!SQL_ok(rc)) {
	/* dbd_error called above */
	Safefree(imp_sth->fbh);
	return 0;
    }

    return 1;
}


int
   dbd_st_execute(sth, imp_sth)	/* <= -2:error, >=0:ok row count, (-1=unknown count) */
   SV *sth;
imp_sth_t *imp_sth;
{
    dTHR;
    RETCODE rc;
    int debug = DBIS->debug;

    /* bind input parameters */

    if (debug >= 2)
	fprintf(DBILOGFP,
		"    dbd_st_execute (for sql f%d after)...\n",
		imp_sth->hstmt);

    rc = SQLExecute(imp_sth->hstmt);
    /* patches to handle blobs better, via Jochen Wiedmann */
    while (rc == SQL_NEED_DATA) {
	phs_t* phs;
	STRLEN len;
	UCHAR* ptr;

	if ((rc = SQLParamData(imp_sth->hstmt, (PTR*) &phs))
	    !=  SQL_NEED_DATA) {
	    break;
	}

	/* phs->sv is already upgraded to a PV in _dbd_rebind_ph.
	 * It is not NULL, because we otherwise won't be called here
	 * (value_len = 0).
	 */
	ptr = SvPV(phs->sv, len);
	rc = SQLPutData(imp_sth->hstmt, ptr, len);
	if (!SQL_ok(rc)) {
	    break;
	}
	rc = SQL_NEED_DATA;  /*  So the loop continues ...  */
    }
     
    if (!SQL_ok(rc)) {
	dbd_error(sth, rc, "st_execute/SQLExecute");
	return -2;
    }

    rc = SQLRowCount(imp_sth->hstmt, &imp_sth->RowCount);
    if (!SQL_ok(rc)) {
	dbd_error(sth, rc, "st_execute/SQLRowCount");	/* XXX ? */
	imp_sth->RowCount = -1;
    }

    if (!imp_sth->done_desc) {
	/* This needs to be done after SQLExecute for some drivers!	*/
	/* Especially for order by and join queries.			*/
	/* See Microsoft Knowledge Base article (#Q124899)		*/
	/* describe and allocate storage for results (if any needed)	*/
	if (!dbd_describe(sth, imp_sth))
	    return -2; /* dbd_describe already called dbd_error()	*/
    }

    if (DBIc_NUM_FIELDS(imp_sth) > 0) {
	DBIc_ACTIVE_on(imp_sth);	/* only set for select (?)	*/
    } else {
	if (debug >= 2) {
	    fprintf(DBILOGFP,
		    "    dbd_st_execute got no rows: resetting ACTIVE, moreResults\n");
	    imp_sth->moreResults = 0;
	    DBIc_ACTIVE_off(imp_sth);
	}
    }
    imp_sth->eod = SQL_SUCCESS;

    // JLU: Jon Smirl had:
    //      return (imp_sth->RowCount == -1 ? -1 : abs(imp_sth->RowCount));
    // why?  Why do you need the abs() of the rowcount?  Special reason?
    // The e-mail that accompanied the change indicated that Sybase would return
    // a negative value for an estimate.  Wouldn't you WANT that to stay negative?
    //
    // dgood: JLU had:
    //      return imp_sth->RowCount;
    // Because you return -2 on errors so if you don't abs() it, a perfectly 
    // valid return value will get flagged as an error...
    //
    return (imp_sth->RowCount == -1 ? -1 : abs(imp_sth->RowCount));
    // return imp_sth->RowCount;
}


/*----------------------------------------
 * running $sth->fetch()
 *----------------------------------------
 */
AV *
   dbd_st_fetch(sth, imp_sth)
   SV *	sth;
imp_sth_t *imp_sth;
{
    dTHR;
    D_imp_dbh_from_sth;
    UWORD supported;
    int debug = DBIS->debug;
    int i;
    AV *av;
    RETCODE rc;
    int num_fields;
    char cvbuf[512];
    int ChopBlanks;

    /* Check that execute() was executed sucessfully. This also implies	*/
    /* that dbd_describe() executed sucessfuly so the memory buffers	*/
    /* are allocated and bound.						*/
    if ( !DBIc_ACTIVE(imp_sth) ) {
		dbd_error(sth, SQL_ERROR, "no select statement currently executing");
		return Nullav;
    }

    rc = SQLFetch(imp_sth->hstmt);
    if (DBIS->debug >= 3)
	fprintf(DBILOGFP, "       SQLFetch rc %d\n", rc);
    imp_sth->eod = rc;
    if (!SQL_ok(rc)) {
	if (SQL_NO_DATA_FOUND == rc) {

	    /* See if we can check for multiple results */
	    rc = SQLGetFunctions(imp_dbh->hdbc, SQL_API_SQLMORERESULTS, 
				 &supported);
	    if (DBIS->debug >= 3)
		fprintf(DBILOGFP, "       SQLGetFunctions - supported: %d\n", 
			supported);
	    if (SQL_ok(rc)) {
		if (supported) {
		    /* Check for multiple results */
		    rc = SQLMoreResults(imp_sth->hstmt);
		    if (SQL_ok(rc)){
			/* More results detected.  Clear out the old result */
			/* stuff and re-describe the fields.                */
			Safefree(imp_sth->fbh);
			Safefree(imp_sth->ColNames);
			Safefree(imp_sth->RowBuffer);

			/* dgood - Yikes!  I don't want to go down to this level, */
			/*         but if I don't, it won't figure out that the   */
			/*         number of columns have changed...              */
			if (DBIc_FIELDS_AV(imp_sth)) {
			    sv_free((SV*)DBIc_FIELDS_AV(imp_sth));
			    DBIc_FIELDS_AV(imp_sth) = Nullav;
			}

			imp_sth->fbh       = NULL;
			imp_sth->ColNames  = NULL;
			imp_sth->RowBuffer = NULL;
			imp_sth->done_desc = 0;
			if (!dbd_describe(sth, imp_sth))
			    return Nullav; /* dbd_describe already called dbd_error() */

			/* set moreResults so we'll know we can keep fetching */
			imp_sth->moreResults = 1;
			return Nullav;
		    }
		    else if (rc == SQL_NO_DATA_FOUND){
			/* No more results */
			imp_sth->moreResults = 0;

			/* XXX need to 'finish' here */
			dbd_st_finish(sth, imp_sth);
			return Nullav;
		    }
		    else {
			dbd_error(sth, rc, "st_fetch/SQLMoreResults");
		    }
		}
		else {
		    // SQLMoreResults not supported, just finish.
		    // per bug found by Jarkko Hyöty [hyoty@medialab.sonera.fi]
		    /* No more results */
		    imp_sth->moreResults = 0;
		    /* XXX need to 'finish' here */
		    dbd_st_finish(sth, imp_sth);
		    return Nullav;
		}
	    }
	    else {
		// sql not OK for calling SQLGetFunctions ... falls
		// here.
		dbd_error(sth, rc, "st_fetch/SQLGetFunctions");
	    }
	} else {
	    dbd_error(sth, rc, "st_fetch/SQLFetch");
	    /* XXX need to 'finish' here */
	    dbd_st_finish(sth, imp_sth);
	    return Nullav;
	}
    }

    if (imp_sth->RowCount == -1)
	imp_sth->RowCount = 0;
    imp_sth->RowCount++;

    av = DBIS->get_fbav(imp_sth);
    num_fields = AvFILL(av)+1;

    if (DBIS->debug >= 3)
	fprintf(DBILOGFP, "fetch num_fields=%d\n", num_fields);

    ChopBlanks = DBIc_has(imp_sth, DBIcf_ChopBlanks);

    for(i=0; i < num_fields; ++i) {
	imp_fbh_t *fbh = &imp_sth->fbh[i];
	SV *sv = AvARRAY(av)[i]; /* Note: we (re)use the SV in the AV	*/

	if (DBIS->debug >= 4)
	    fprintf(DBILOGFP, "fetch col#%d %s datalen=%d displ=%d\n",
		    i, fbh->ColName, fbh->datalen, fbh->ColDisplaySize);

	if (fbh->datalen == SQL_NULL_DATA) {	/* NULL value		*/
	    SvOK_off(sv);
	    continue;
	}

	if (fbh->datalen > fbh->ColDisplaySize || fbh->datalen < 0) { 
	    /* truncated LONG ??? DBIcf_LongTruncOk() */
	    /* DBIcf_LongTruncOk this should only apply to LONG type fields	*/
	    /* truncation of other fields should always be an error since it's	*/
	    /* a sign of an internal error */
	    if (!DBIc_has(imp_sth, DBIcf_LongTruncOk)
		  /*  && rc == SQL_SUCCESS_WITH_INFO */) {

		/* fix for OpenLink drivers which return success, but we've detected */
		/* the problem locally, via the datalen */
		if (!rc)
		    rc = SQL_SUCCESS_WITH_INFO;

		dbd_error(sth, rc, "st_fetch/SQLFetch (long truncated)");
		return Nullav;
	    }
	    sv_setpvn(sv, (char*)fbh->data, fbh->ColDisplaySize);
	}
	else switch(fbh->ftype) {
#ifdef TIMESTAMP_STRUCT /* iODBC doesn't define this */
	    TIMESTAMP_STRUCT *ts;
	    case SQL_C_TIMESTAMP:
		ts = (TIMESTAMP_STRUCT *)fbh->data;
		sprintf(cvbuf, "%04d-%02d-%02d %02d:%02d:%02d",
			ts->year, ts->month, ts->day, 
			ts->hour, ts->minute, ts->second, ts->fraction);
		sv_setpv(sv, cvbuf);
		break;
#endif
	    default:
		if (ChopBlanks && fbh->ColSqlType == SQL_CHAR && fbh->datalen > 0) {
		    char *p = (char*)fbh->data;
		    while(fbh->datalen && p[fbh->datalen - 1]==' ')
			--fbh->datalen;
		}
		sv_setpvn(sv, (char*)fbh->data, fbh->datalen);
	}
    }
    return av;
}


int
   dbd_st_rows(sth, imp_sth)
   SV *sth;
imp_sth_t *imp_sth;
{
    return imp_sth->RowCount;
}


int
   dbd_st_finish(sth, imp_sth)
   SV *sth;
imp_sth_t *imp_sth;
{
    dTHR;
    D_imp_dbh_from_sth;
    RETCODE rc;
    int ret = 0;

    /* Cancel further fetches from this cursor.                 */
    /* We don't close the cursor till DESTROY (dbd_st_destroy). */
    /* The application may re execute(...) it.                  */

    /* XXX semantics of finish (eg oracle vs odbc) need lots more thought */
    /* re-read latest DBI specs and ODBC manuals */
    if (DBIc_ACTIVE(imp_sth) && imp_dbh->hdbc != SQL_NULL_HDBC) {
	rc = SQLFreeStmt(imp_sth->hstmt, SQL_CLOSE);
	if (!SQL_ok(rc)) {
	    dbd_error(sth, rc, "finish/SQLFreeStmt(SQL_CLOSE)");
	    return 0;
	}
    }
    DBIc_ACTIVE_off(imp_sth);
    return 1;
}


void
   dbd_st_destroy(sth, imp_sth)
   SV *sth;
imp_sth_t *imp_sth;
{
    dTHR;
    D_imp_dbh_from_sth;
    RETCODE rc;

    /* SQLxxx functions dump core when no connection exists. This happens
     * when the db was disconnected before perl ending.
     */
    if (imp_dbh->hdbc != SQL_NULL_HDBC) {
	rc = SQLFreeStmt(imp_sth->hstmt, SQL_DROP);
	if (!SQL_ok(rc)) {
	    dbd_error(sth, rc, "st_destroy/SQLFreeStmt(SQL_DROP)");
	    /* return 0; */
	}
    }

    /* Free contents of imp_sth	*/

    Safefree(imp_sth->fbh);
    Safefree(imp_sth->ColNames);
    Safefree(imp_sth->RowBuffer);
    Safefree(imp_sth->statement);

    if (imp_sth->out_params_av)
	sv_free((SV*)imp_sth->out_params_av);

    if (imp_sth->all_params_hv) {
	HV *hv = imp_sth->all_params_hv;
	SV *sv;
	char *key;
	I32 retlen;
	hv_iterinit(hv);
	while( (sv = hv_iternextsv(hv, &key, &retlen)) != NULL ) {
	    if (sv != &sv_undef) {
		phs_t *phs_tpl = (phs_t*)(void*)SvPVX(sv);
		sv_free(phs_tpl->sv);
	    }
	}
	sv_free((SV*)imp_sth->all_params_hv);
    }

    DBIc_IMPSET_off(imp_sth);		/* let DBI know we've done it	*/
}



/* ====================================================================	*/


static int 
   _dbd_rebind_ph(sth, imp_sth, phs, maxlen) 
   SV *sth;
imp_sth_t *imp_sth;
phs_t *phs;
int maxlen;
{
	dTHR;
	RETCODE rc;
	/* args of SQLBindParameter() call */
	SWORD fParamType;
	SWORD fCType;
	SWORD fSqlType;
	UCHAR *rgbValue;
	UDWORD cbColDef;
	SWORD ibScale;
	SDWORD cbValueMax;

	STRLEN value_len;

	if (DBIS->debug >= 2) {
		char *text = neatsvpv(phs->sv,0);
		fprintf(DBILOGFP,
				"bind %s <== %s (size %d/%d/%ld, ptype %ld, otype %d)\n",
				phs->name, text, SvCUR(phs->sv),SvLEN(phs->sv),phs->maxlen,
				SvTYPE(phs->sv), phs->ftype);
	}

	/* At the moment we always do sv_setsv() and rebind.        */
	/* Later we may optimise this so that more often we can     */
	/* just copy the value & length over and not rebind.        */

	if (phs->is_inout) {        /* XXX */
		if (SvREADONLY(phs->sv))
			croak(no_modify);
		/* phs->sv _is_ the real live variable, it may 'mutate' later   */
		/* pre-upgrade high to reduce risk of SvPVX realloc/move        */
		(void)SvUPGRADE(phs->sv, SVt_PVNV);
		/* ensure room for result, 28 is magic number (see sv_2pv)      */
		SvGROW(phs->sv, (phs->maxlen < 28) ? 28 : phs->maxlen+1);
	}
	else {
		/* phs->sv is copy of real variable, upgrade to at least string */
		(void)SvUPGRADE(phs->sv, SVt_PV);
	}

	/* At this point phs->sv must be at least a PV with a valid buffer, */
	/* even if it's undef (null)                                        */
	/* Here we set phs->sv_buf, and value_len.                */
	if (SvOK(phs->sv)) {
		phs->sv_buf = SvPV(phs->sv, value_len);
	}
	else {      /* it's null but point to buffer incase it's an out var */
		phs->sv_buf = SvPVX(phs->sv);
		value_len   = 0;
	}
	/* value_len has current value length now */
	phs->sv_type = SvTYPE(phs->sv);     /* part of mutation check       */
	phs->maxlen  = SvLEN(phs->sv)-1;    /* avail buffer space  		*/

	if (DBIS->debug >= 3) {
		fprintf(DBILOGFP, "bind %s <== '%.100s' (len %ld/%ld, null %d)\n",
				phs->name, phs->sv_buf,
				(long)value_len,(long)phs->maxlen, SvOK(phs->sv)?0:1);
	}

	/* ----------------------------------------------------------------	*/

	/* XXX
	This will fail (IM001) on drivers which don't support it.
	We need to check for this and bind the param as varchars.
	This will work on many drivers and databases.
	If the database won't convert a varchar to an int (for example)
	the user will get an error at execute time
	but can add an explicit conversion to the SQL:
	"... where num_field > int(?) ..."
*/

	if (phs->sql_type == 0) {
		SWORD fNullable;
		SWORD ibScale;
		UDWORD dp_cbColDef;
		rc = SQLDescribeParam(imp_sth->hstmt,
							  phs->idx, &fSqlType, &dp_cbColDef, &ibScale, &fNullable
							 );
		if (!SQL_ok(rc)) {
			dbd_error(sth, rc, "_rebind_ph/SQLDescribeParam");
			return 0;
		}
		if (DBIS->debug >=2)
			fprintf(DBILOGFP,
					"    SQLDescribeParam %s: SqlType=%s, ColDef=%d\n",
					phs->name, S_SqlTypeToString(fSqlType), dp_cbColDef);

		phs->sql_type = fSqlType;
	}

	fParamType = SQL_PARAM_INPUT;
	fCType = phs->ftype;
	ibScale = value_len;
	cbColDef = value_len;
	cbValueMax = value_len;

	/* When we fill a LONGVARBINARY, the CTYPE must be set 
	 * to SQL_C_BINARY.
	 */
	if (fCType == SQL_C_CHAR) {	/* could be changed by bind_plh */
		switch(phs->sql_type) {
			case SQL_LONGVARBINARY:
			case SQL_BINARY:
			case SQL_VARBINARY:
				fCType = SQL_C_BINARY;
				break;
#ifdef SQL_WLONGVARCHAR
			case SQL_WLONGVARCHAR:	/* added for SQLServer 7 ntext type */
#endif
			case SQL_LONGVARCHAR:
				break;
			case SQL_TIMESTAMP:
			case SQL_DATE:
			case SQL_TIME:
				fSqlType = SQL_VARCHAR;
				break;
			default:
				break;
		}
	}
	/* per patch from Paul G. Weiss, who was experiencing re-preparing
	 * of queries when the size of the bound string's were increasing
	 * for example select * from tabtest where name = ?
	 * then executing with 'paul' and then 'thomas' would cause
	 * SQLServer to prepare the query twice, but if we ran 'thomas'
	 * then 'paul', it would not re-prepare the query.  The key seems
	 * to be allocating enough space for the largest parameter.
	 * TBD: the default for this should be a DBD::ODBC specific option
	 * or attribute.
	 */
	if (phs->sql_type == SQL_VARCHAR) {
		ibScale = 0;
		/* default to at least 80 if this is the first time through */
		if (phs->biggestparam == 0) {
			phs->biggestparam = (value_len > 80) ? value_len : 80;
		} else {
			/* bump up max, if needed */
			if (value_len > phs->biggestparam) {
				phs->biggestparam = value_len;
			}
		}
		cbColDef = phs->biggestparam;
	}

	if (!SvOK(phs->sv)) {
		rgbValue = NULL;
		phs->cbValue = SQL_NULL_DATA;
	}
	else {
		rgbValue = phs->sv_buf;
		phs->cbValue = (UDWORD) value_len;
	}
	if (DBIS->debug >=2)
		fprintf(DBILOGFP,
				"    bind %s: CTy=%d, STy=%s, CD=%d, Sc=%d, VM=%d.\n",
				phs->name, fCType, S_SqlTypeToString(phs->sql_type),
				cbColDef, ibScale, cbValueMax);

	if (value_len < 32768) {
		ibScale = value_len;
	} else {
		/* This exceeds the positive size of an SWORD, so we have to use
		 * SQLPutData.
		 */
		ibScale = 32767;
		phs->cbValue = SQL_LEN_DATA_AT_EXEC(value_len);
		/* This is lazyness!
		 *
		 * The ODBC docs declare rgbValue as a 32 bit value. May be this
		 * breaks on 64 bit machines?
		 */
		rgbValue = (UCHAR*) phs;
	}
	rc = SQLBindParameter(imp_sth->hstmt,
						  phs->idx, fParamType, fCType, phs->sql_type,
						  cbColDef, ibScale,
						  rgbValue, cbValueMax, &phs->cbValue);

	if (!SQL_ok(rc)) {
		dbd_error(sth, rc, "_rebind_ph/SQLBindParameter");
		return 0;
	}

	return 1;
}


/*------------------------------------------------------------
 * bind placeholder.
 *  Is called from ODBC.xs execute()
 *  AND from ODBC.xs bind_param()
 */
int
   dbd_bind_ph(sth, imp_sth, ph_namesv, newvalue, sql_type, attribs, is_inout, maxlen)
   SV *sth;
imp_sth_t *imp_sth;
SV *ph_namesv;		/* index of execute() parameter 1..n */
SV *newvalue;
IV sql_type;
SV *attribs;		/* may be set by Solid.xs bind_param call */
int is_inout;		/* inout for procedure calls only */
IV maxlen;			/* ??? */
{
    dTHR;
    SV **phs_svp;
    STRLEN name_len;
    char *name;
    char namebuf[30];
    phs_t *phs;

    if (SvNIOK(ph_namesv) ) {	/* passed as a number	*/
		name = namebuf;
		sprintf(name, "%d", (int)SvIV(ph_namesv));
		name_len = strlen(name);
    } 
    else {
		name = SvPV(ph_namesv, name_len);
    }

    if (SvTYPE(newvalue) > SVt_PVMG)    /* hook for later array logic   */
		croak("Can't bind non-scalar value (currently)");

    if (DBIS->debug >= 2)
		fprintf(DBILOGFP, "bind %s <== '%.200s' (attribs: %s)\n",
				name, SvPV(newvalue,na), attribs ? SvPV(attribs,na) : "" );

	phs_svp = hv_fetch(imp_sth->all_params_hv, name, name_len, 0);
	if (phs_svp == NULL)
		croak("Can't bind unknown placeholder '%s'", name);
	phs = (phs_t*)SvPVX(*phs_svp);	/* placeholder struct	*/

    if (phs->sv == &sv_undef) { /* first bind for this placeholder      */
		phs->ftype    = SQL_C_CHAR;     /* our default type VARCHAR2    */
		phs->sql_type = (sql_type) ? sql_type : SQL_VARCHAR;
		phs->maxlen   = maxlen;         /* 0 if not inout               */
		phs->is_inout = is_inout;
		if (is_inout) {
			phs->sv = SvREFCNT_inc(newvalue);   /* point to live var    */
			++imp_sth->has_inout_params;
			/* build array of phs's so we can deal with out vars fast   */
			if (!imp_sth->out_params_av)
				imp_sth->out_params_av = newAV();
			av_push(imp_sth->out_params_av, SvREFCNT_inc(*phs_svp));
			croak("Can't bind output values (currently)");	/* XXX */
		}
		
		/* some types require the trailing null included in the length. */
		phs->alen_incnull = 0; /*Oracle:(phs->ftype==SQLT_STR || phs->ftype==SQLT_AVC);*/
		
    }
	/* check later rebinds for any changes */
	else if (is_inout || phs->is_inout) {
		croak("Can't rebind or change param %s in/out mode after first bind", phs->name);
    }
    else if (maxlen && maxlen != phs->maxlen) {
		croak("Can't change param %s maxlen (%ld->%ld) after first bind",
			  phs->name, phs->maxlen, maxlen);
    }

    if (!is_inout) {    /* normal bind to take a (new) copy of current value    */
		if (phs->sv == &sv_undef)       /* (first time bind) */
			phs->sv = newSV(0);
		sv_setsv(phs->sv, newvalue);
    }

    return _dbd_rebind_ph(sth, imp_sth, phs);
}


/*------------------------------------------------------------
 * blob_read:
 * read part of a BLOB from a table.
 * XXX needs more thought
 */
dbd_st_blob_read(sth, imp_sth, field, offset, len, destrv, destoffset)
SV *sth;
imp_sth_t *imp_sth;
int field;
long offset;
long len;
SV *destrv;
long destoffset;
{
    dTHR;
    SDWORD retl;
    SV *bufsv;
    RETCODE rc;

    bufsv = SvRV(destrv);
    sv_setpvn(bufsv,"",0);      /* ensure it's writable string  */
    SvGROW(bufsv, len+destoffset+1);    /* SvGROW doesn't do +1 */

    /* XXX for this to work be probably need to avoid calling SQLGetData in	*/
    /* fetch. The definition of SQLGetData doesn't work well with the DBI's	*/
    /* notion of how LongReadLen would work. Needs more thought.	*/

    rc = SQLGetData(imp_sth->hstmt, (UWORD)field+1,
		    SQL_C_BINARY,
		    ((UCHAR *)SvPVX(bufsv)) + destoffset, (SDWORD)len, &retl
		   );
    if (DBIS->debug >= 2)
	fprintf(DBILOGFP,
		"SQLGetData(...,off=%d, len=%d)->rc=%d,len=%d SvCUR=%d\n",
		destoffset, len, rc, retl, SvCUR(bufsv));

    if (!SQL_ok(rc)) {
	dbd_error(sth, rc, "dbd_st_blob_read/SQLGetData");
	return 0;
    }
    if (rc == SQL_SUCCESS_WITH_INFO) {	/* XXX should check for 01004 */
	retl = len;
    }

    if (retl == SQL_NULL_DATA) {	/* field is null	*/
	(void)SvOK_off(bufsv);
	return 1;
    }
#ifdef SQL_NO_TOTAL
    if (retl == SQL_NO_TOTAL) {		/* unknown length!	*/
	(void)SvOK_off(bufsv);
	return 0;
    }
#endif

    SvCUR_set(bufsv, destoffset+retl);
    *SvEND(bufsv) = '\0'; /* consistent with perl sv_setpvn etc */

    if (DBIS->debug >= 2)
	fprintf(DBILOGFP, "blob_read: SvCUR=%d\n",
		SvCUR(bufsv));

    return 1;
}


/*----------------------------------------
 * db level interface
 * set connection attributes.
 *----------------------------------------
 */

typedef struct {
    const char *str;
    UWORD fOption;
    UDWORD true;
    UDWORD false;
} db_params;

static db_params S_db_storeOptions[] =  {
    { "AutoCommit", SQL_AUTOCOMMIT, SQL_AUTOCOMMIT_ON, SQL_AUTOCOMMIT_OFF },
#if 0 /* not defined by DBI/DBD specification */
    { "TRANSACTION", 
    SQL_ACCESS_MODE, SQL_MODE_READ_ONLY, SQL_MODE_READ_WRITE },
    { "solid_trace", SQL_OPT_TRACE, SQL_OPT_TRACE_ON, SQL_OPT_TRACE_OFF },
    { "solid_timeout", SQL_LOGIN_TIMEOUT },
    { "ISOLATION", SQL_TXN_ISOLATION },
    { "solid_tracefile", SQL_OPT_TRACEFILE },
#endif
    { NULL },
};

static const db_params *
   S_dbOption(const db_params *pars, char *key, STRLEN len)
{
    /* search option to set */
    while (pars->str != NULL) {
	if (strncmp(pars->str, key, len) == 0
	    && len == strlen(pars->str))
	    break;
	pars++;
    }
    if (pars->str == NULL)
	return NULL;
    return pars;
}

int
   dbd_db_STORE_attrib(dbh, imp_dbh, keysv, valuesv)
   SV *dbh;
imp_dbh_t *imp_dbh;
SV *keysv;
SV *valuesv;
{
    dTHR;
    D_imp_drh_from_dbh;
    RETCODE rc;
    STRLEN kl;
    STRLEN plen;
    char *key = SvPV(keysv,kl);
    SV *cachesv = NULL;
    int on;
    UDWORD vParam;
    const db_params *pars;

    if ((pars = S_dbOption(S_db_storeOptions, key, kl)) == NULL)
	return FALSE;

    switch(pars->fOption)
    {
	case SQL_LOGIN_TIMEOUT:
	case SQL_TXN_ISOLATION:
	    vParam = SvIV(valuesv);
	    break;
	case SQL_OPT_TRACEFILE:
	    vParam = (UDWORD) SvPV(valuesv, plen);
	    break;
	default:
	    on = SvTRUE(valuesv);
	    vParam = on ? pars->true : pars->false;
	    break;
    }

    rc = SQLSetConnectOption(imp_dbh->hdbc, pars->fOption, vParam);
    if (!SQL_ok(rc)) {
	dbd_error(dbh, rc, "db_STORE/SQLSetConnectOption");
	return FALSE;
    }
    /* keep our flags in sync */
    if (kl == 10 && strEQ(key, "AutoCommit"))
		DBIc_set(imp_dbh, DBIcf_AutoCommit, SvTRUE(valuesv));
    return TRUE;
}


static db_params S_db_fetchOptions[] =  {
    { "AutoCommit", SQL_AUTOCOMMIT, SQL_AUTOCOMMIT_ON, SQL_AUTOCOMMIT_OFF },
#if 0 /* seems not supported by SOLID */
    { "sol_readonly", 
    SQL_ACCESS_MODE, SQL_MODE_READ_ONLY, SQL_MODE_READ_WRITE },
    { "sol_trace", SQL_OPT_TRACE, SQL_OPT_TRACE_ON, SQL_OPT_TRACE_OFF },
    { "sol_timeout", SQL_LOGIN_TIMEOUT },
    { "sol_isolation", SQL_TXN_ISOLATION },
    { "sol_tracefile", SQL_OPT_TRACEFILE },
#endif
    { NULL }
};

SV *
   dbd_db_FETCH_attrib(dbh, imp_dbh, keysv)
   SV *dbh;
imp_dbh_t *imp_dbh;
SV *keysv;
{
    dTHR;
    D_imp_drh_from_dbh;
    RETCODE rc;
    STRLEN kl;
    STRLEN plen;
    char *key = SvPV(keysv,kl);
    int on;
    UDWORD vParam = 0;
    const db_params *pars;
    SV *retsv = NULL;

    /* checking pars we need FAST */

    if ((pars = S_dbOption(S_db_fetchOptions, key, kl)) == NULL)
	return Nullsv;

    /*
     * readonly, tracefile etc. isn't working yet. only AutoCommit supported.
     */

    rc = SQLGetConnectOption(imp_dbh->hdbc, pars->fOption, &vParam);
    dbd_error(dbh, rc, "db_FETCH/SQLGetConnectOption");
    if (!SQL_ok(rc)) {
	if (DBIS->debug >= 1)
	    fprintf(DBILOGFP,
		    "SQLGetConnectOption returned %d in dbd_db_FETCH\n", rc);
	return Nullsv;
    }
    switch(pars->fOption) {
	case SQL_LOGIN_TIMEOUT:
	case SQL_TXN_ISOLATION:
	    newSViv(vParam);
	    break;
	case SQL_OPT_TRACEFILE:
	    retsv = newSVpv((char *)vParam, 0);
	    break;
	default:
	    if (vParam == pars->true)
		retsv = newSViv(1);
	    else
		retsv = newSViv(0);
	    break;
    } /* switch */
    return sv_2mortal(retsv);
}

typedef struct {
    const char *str;
    unsigned len:8;
    unsigned array:1;
    unsigned filler:23;
} T_st_params;

#define s_A(str) { str, sizeof(str)-1 }
static T_st_params S_st_fetch_params[] = 
{
    s_A("NUM_OF_PARAMS"),	/* 0 */
    s_A("NUM_OF_FIELDS"),	/* 1 */
    s_A("NAME"),		/* 2 */
    s_A("NULLABLE"),		/* 3 */
    s_A("TYPE"),		/* 4 */
    s_A("PRECISION"),		/* 5 */
    s_A("SCALE"),		/* 6 */
    s_A("sol_type"),		/* 7 */
    s_A("sol_length"),		/* 8 */
    s_A("CursorName"),		/* 9 */
    s_A("odbc_more_results"),	/* 10 */
    s_A(""),			/* END */
};

static T_st_params S_st_store_params[] = 
{
    s_A(""),			/* END */
};
#undef s_A

/*----------------------------------------
 * dummy routines st_XXXX
 *----------------------------------------
 */
SV *
   dbd_st_FETCH_attrib(sth, imp_sth, keysv)
   SV *sth;
imp_sth_t *imp_sth;
SV *keysv;
{
    dTHR;
    STRLEN kl;
    char *key = SvPV(keysv,kl);
    int i;
    SV *retsv = NULL;
    T_st_params *par;
    int n_fields;
    imp_fbh_t *fbh;
    char cursor_name[256];
    SWORD cursor_name_len;
    RETCODE rc;

    for (par = S_st_fetch_params; par->len > 0; par++)
	if (par->len == kl && strEQ(key, par->str))
	    break;

    if (par->len <= 0)
	return Nullsv;

    if (!imp_sth->done_desc && !dbd_describe(sth, imp_sth)) 
    {
	/* dbd_describe has already called dbd_error()          */
	/* we can't return Nullsv here because the xs code will */
	/* then just pass the attribute name to DBI for FETCH.  */
	croak("Describe failed during %s->FETCH(%s)",
	      SvPV(sth,na), key);
    }

    i = DBIc_NUM_FIELDS(imp_sth);

    switch(par - S_st_fetch_params)
    {
	AV *av;

	case 0:			/* NUM_OF_PARAMS */
	    return Nullsv;	/* handled by DBI */
	case 1:			/* NUM_OF_FIELDS */
	    retsv = newSViv(i);
	    break;
	case 2: 			/* NAME */
	    av = newAV();
	    retsv = newRV(sv_2mortal((SV*)av));
	    while(--i >= 0)
		av_store(av, i, newSVpv(imp_sth->fbh[i].ColName, 0));
	    break;
	case 3:			/* NULLABLE */
	    av = newAV();
	    retsv = newRV(sv_2mortal((SV*)av));
	    while(--i >= 0)
		av_store(av, i,
			 (imp_sth->fbh[i].ColNullable == SQL_NO_NULLS)
			 ? &sv_no : &sv_yes);
	    break;
	case 4:			/* TYPE */
	    av = newAV();
	    retsv = newRV(sv_2mortal((SV*)av));
	    while(--i >= 0) 
		av_store(av, i, newSViv(imp_sth->fbh[i].ColSqlType));
	    break;
	case 5:			/* PRECISION */
	    av = newAV();
	    retsv = newRV(sv_2mortal((SV*)av));
	    while(--i >= 0) 
		av_store(av, i, newSViv(imp_sth->fbh[i].ColDef));
	    break;
	case 6:			/* SCALE */
	    av = newAV();
	    retsv = newRV(sv_2mortal((SV*)av));
	    while(--i >= 0) 
		av_store(av, i, newSViv(imp_sth->fbh[i].ColScale));
	    break;
	case 7:			/* sol_type */
	    av = newAV();
	    retsv = newRV(sv_2mortal((SV*)av));
	    while(--i >= 0) 
		av_store(av, i, newSViv(imp_sth->fbh[i].ColSqlType));
	    break;
	case 8:			/* sol_length */
	    av = newAV();
	    retsv = newRV(sv_2mortal((SV*)av));
	    while(--i >= 0) 
		av_store(av, i, newSViv(imp_sth->fbh[i].ColLength));
	    break;
	case 9:			/* CursorName */
	    rc = SQLGetCursorName(imp_sth->hstmt,
				  cursor_name, sizeof(cursor_name), &cursor_name_len);
	    if (!SQL_ok(rc)) {
		dbd_error(sth, rc, "st_FETCH/SQLGetCursorName");
		return Nullsv;
	    }
	    retsv = newSVpv(cursor_name, cursor_name_len);
	    break;
	case 10:                /* odbc_more_results */
	    retsv = newSViv(imp_sth->moreResults);
	    break;
	    /* Umm... This used to be #10.  I don't really see why it's here at  */
	    /* all since there was no corresponding entry in S_st_fetch_params.  */
	    /* I'll leave it here as #11 just in case...  [dgood 2/16/00]        */
	case 11:
	    retsv = newSViv(DBIc_LongReadLen(imp_sth));
	    break;
	default:
	    return Nullsv;
    }

    return sv_2mortal(retsv);
}


int
   dbd_st_STORE_attrib(sth, imp_sth, keysv, valuesv)
   SV *sth;
imp_sth_t *imp_sth;
SV *keysv;
SV *valuesv;
{
    dTHR;
    D_imp_dbh_from_sth;
    STRLEN kl;
    STRLEN vl;
    char *key = SvPV(keysv,kl);
    char *value = SvPV(valuesv, vl);
    T_st_params *par;
    RETCODE rc;

    for (par = S_st_store_params; par->len > 0; par++)
	if (par->len == kl && strEQ(key, par->str))
	    break;

    if (par->len <= 0)
	return FALSE;

    switch(par - S_st_store_params)
    {
	case 0:/*  */
	    return TRUE;
    }
    return FALSE;
}


SV *
   odbc_get_info(dbh, ftype)
   SV *dbh;
int ftype;
{
    dTHR;
    D_imp_dbh(dbh);
    RETCODE rc;
    SV *retsv = NULL;
    int i;
    char rgbInfoValue[256];
    SWORD cbInfoValue = -2;

    /* See fancy logic below */
    for (i = 0; i < 6; i++)
	rgbInfoValue[i] = 0xFF;

    rc = SQLGetInfo(imp_dbh->hdbc, ftype,
		    rgbInfoValue, sizeof(rgbInfoValue)-1, &cbInfoValue);
    if (!SQL_ok(rc)) {
	dbd_error(dbh, rc, "odbc_get_info/SQLGetInfo");
	return Nullsv;
    }

    /* Fancy logic here to determine if result is a string or int */
    if (cbInfoValue == -2)				/* is int */
	retsv = newSViv(*(int *)rgbInfoValue);	/* XXX cast */
    else if (cbInfoValue != 2 && cbInfoValue != 4)	/* must be string */
	retsv = newSVpv(rgbInfoValue, 0);
    else if (rgbInfoValue[cbInfoValue+1] == '\0')	/* must be string */
	retsv = newSVpv(rgbInfoValue, 0);
    else if (cbInfoValue == 2)			/* short */
	retsv = newSViv(*(short *)rgbInfoValue);	/* XXX cast */
    else if (cbInfoValue == 4)			/* int */
	retsv = newSViv(*(int *)rgbInfoValue);	/* XXX cast */
    else
	croak("panic: SQLGetInfo cbInfoValue == %d", cbInfoValue);

    if (DBIS->debug >= 2)
	fprintf(DBILOGFP, "SQLGetInfo: ftype %d, cbInfoValue %d: %s\n",
		ftype, cbInfoValue, neatsvpv(retsv,0));

    return sv_2mortal(retsv);
}

int
   odbc_get_statistics(dbh, sth, CatalogName, SchemaName, TableName, Unique)
   SV *	 dbh;
SV *	 sth;
char * CatalogName;
char * SchemaName;
char * TableName;
int		 Unique;
{
    dTHR;
    D_imp_dbh(dbh);
    D_imp_sth(sth);
    RETCODE rc;

    imp_sth->henv = imp_dbh->henv;	/* needed for dbd_error */
    imp_sth->hdbc = imp_dbh->hdbc;

    imp_sth->done_desc = 0;
    rc = SQLAllocStmt(imp_dbh->hdbc, &imp_sth->hstmt);
    if (rc != SQL_SUCCESS) {
	dbd_error(sth, rc, "odbc_get_statistics/SQLAllocStmt");
	return 0;
    }

    rc = SQLStatistics(imp_sth->hstmt, 
		       CatalogName, strlen(CatalogName), 
		       SchemaName, strlen(SchemaName), 
		       TableName, strlen(TableName), 
		       Unique, 0);
    if (!SQL_ok(rc)) {
	dbd_error(sth, rc, "odbc_get_statistics/SQLGetStatistics");
	return 0;
    }
    return build_results(sth);
}

int
   odbc_get_primary_keys(dbh, sth, CatalogName, SchemaName, TableName)
   SV *	 dbh;
SV *	 sth;
char * CatalogName;
char * SchemaName;
char * TableName;
{
    dTHR;
    D_imp_dbh(dbh);
    D_imp_sth(sth);
    RETCODE rc;

    imp_sth->henv = imp_dbh->henv;	/* needed for dbd_error */
    imp_sth->hdbc = imp_dbh->hdbc;

    imp_sth->done_desc = 0;
    rc = SQLAllocStmt(imp_dbh->hdbc, &imp_sth->hstmt);
    if (rc != SQL_SUCCESS) {
	dbd_error(sth, rc, "odbc_get_primary_keys/SQLAllocStmt");
	return 0;
    }

    rc = SQLPrimaryKeys(imp_sth->hstmt, 
			CatalogName, strlen(CatalogName), 
			SchemaName, strlen(SchemaName), 
			TableName, strlen(TableName));
    if (!SQL_ok(rc)) {
	dbd_error(sth, rc, "odbc_get_primary_keys/SQLPrimaryKeys");
	return 0;
    }
    return build_results(sth);
}

int
   odbc_get_special_columns(dbh, sth, Identifier, CatalogName, SchemaName, TableName, Scope, Nullable)
   SV *	 dbh;
SV *	 sth;
int    Identifier;
char * CatalogName;
char * SchemaName;
char * TableName;
int    Scope;
int    Nullable;
{
	dTHR;
	D_imp_dbh(dbh);
	D_imp_sth(sth);
    RETCODE rc;

    imp_sth->henv = imp_dbh->henv;	/* needed for dbd_error */
	imp_sth->hdbc = imp_dbh->hdbc;

    imp_sth->done_desc = 0;
    rc = SQLAllocStmt(imp_dbh->hdbc, &imp_sth->hstmt);
    if (rc != SQL_SUCCESS) {
		dbd_error(sth, rc, "odbc_get_special_columns/SQLAllocStmt");
		return 0;
	}
	
	rc = SQLSpecialColumns(imp_sth->hstmt, 
						   Identifier, CatalogName, strlen(CatalogName), 
						   SchemaName, strlen(SchemaName), 
						   TableName, strlen(TableName),
						   Scope, Nullable);
	if (!SQL_ok(rc)) {
		dbd_error(sth, rc, "odbc_get_special_columns/SQLSpecialClumns");
		return 0;
	}
	return build_results(sth);
}

int
   odbc_get_foreign_keys(dbh, sth, PK_CatalogName, PK_SchemaName, PK_TableName, FK_CatalogName, FK_SchemaName, FK_TableName)
   SV *	 dbh;
SV *	 sth;
char * PK_CatalogName;
char * PK_SchemaName;
char * PK_TableName;
char * FK_CatalogName;
char * FK_SchemaName;
char * FK_TableName;
{
    dTHR;
    D_imp_dbh(dbh);
    D_imp_sth(sth);
    RETCODE rc;

    imp_sth->henv = imp_dbh->henv;	/* needed for dbd_error */
    imp_sth->hdbc = imp_dbh->hdbc;

    imp_sth->done_desc = 0;
    rc = SQLAllocStmt(imp_dbh->hdbc, &imp_sth->hstmt);
    if (rc != SQL_SUCCESS) {
		dbd_error(sth, rc, "odbc_get_foreign_keys/SQLAllocStmt");
		return 0;
    }

    rc = SQLForeignKeys(imp_sth->hstmt, 
			PK_CatalogName, strlen(PK_CatalogName), 
			PK_SchemaName, strlen(PK_SchemaName), 
			PK_TableName, strlen(PK_TableName), 
			FK_CatalogName, strlen(FK_CatalogName), 
			FK_SchemaName, strlen(FK_SchemaName), 
			FK_TableName, strlen(FK_TableName));
    if (!SQL_ok(rc)) {
		dbd_error(sth, rc, "odbc_get_foreign_keys/SQLForeignKeys");
		return 0;
    }
    return build_results(sth);
}


int
   odbc_describe_col(sth, colno, ColumnName, BufferLength, NameLength, DataType, ColumnSize, DecimalDigits, Nullable)
   SV *sth;
int colno;
char *ColumnName;
I16 BufferLength;
I16 *NameLength;
I16 *DataType;
U32 *ColumnSize;
I16 *DecimalDigits;
I16 *Nullable;
{
    D_imp_sth(sth);
    RETCODE rc;
    rc = SQLDescribeCol(imp_sth->hstmt, colno,
			ColumnName, BufferLength, NameLength,
			DataType, ColumnSize, DecimalDigits, Nullable);
    if (!SQL_ok(rc)) {
		dbd_error(sth, rc, "DescribeCol/SQLDescribeCol");
		return 0;
    }
    return 1;
}


int
   odbc_get_type_info(dbh, sth, ftype)
   SV *dbh;
SV *sth;
int ftype;
{
    dTHR;
    D_imp_dbh(dbh);
    D_imp_sth(sth);
    RETCODE rc;
    SV **svp;
    char cname[128];			/* cursorname */

    imp_sth->henv = imp_dbh->henv;	/* needed for dbd_error */
    imp_sth->hdbc = imp_dbh->hdbc;

    imp_sth->done_desc = 0;
    rc = SQLAllocStmt(imp_dbh->hdbc, &imp_sth->hstmt);
    if (rc != SQL_SUCCESS) {
	dbd_error(sth, rc, "odbc_get_type_info/SQLGetTypeInfo");
	return 0;
    }

    /* just for sanity, later. Any internals that may rely on this (including */
    /* debugging) will have valid data */
    imp_sth->statement = (char *)safemalloc(strlen(cSqlGetTypeInfo)+ftype/10+1);
    sprintf(imp_sth->statement, cSqlGetTypeInfo, ftype);

    rc = SQLGetTypeInfo(imp_sth->hstmt, ftype);

    dbd_error(sth, rc, "odbc_get_type_info/SQLGetTypeInfo");
    if (!SQL_ok(rc)) {
	SQLFreeStmt(imp_sth->hstmt, SQL_DROP);
	imp_sth->hstmt = SQL_NULL_HSTMT;
	return 0;
    }

    return build_results(sth);
}


SV *
   odbc_cancel(sth)
   SV *sth;
{
    dTHR;
    D_imp_sth(sth);
    RETCODE rc;

    if ( !DBIc_ACTIVE(imp_sth) ) {
	dbd_error(sth, SQL_ERROR, "no statement executing");
	return Nullsv;
    }

    rc = SQLCancel(imp_sth);
    if (!SQL_ok(rc)) {
	dbd_error(sth, rc, "odbc_cancel/SQLCancel");
	return Nullsv;
    }
    return newSViv(1);
}


SV *
   odbc_col_attributes(sth, colno, desctype)
   SV *sth;
int colno;
int desctype;
{
    dTHR;
    D_imp_sth(sth);
    RETCODE rc;
    SV *retsv = NULL;
    int i;
    char rgbInfoValue[256];
    SWORD cbInfoValue = -2;
    SDWORD fDesc = -2;

    for (i = 0; i < 6; i++)
	rgbInfoValue[i] = 0xFF;

    if ( !DBIc_ACTIVE(imp_sth) ) {
	dbd_error(sth, SQL_ERROR, "no statement executing");
	return Nullsv;
    }

    /*  fprintf(DBILOGFP,
    "SQLColAttributes: colno = %d, desctype = %d, cbInfoValue = %d\n",
    colno, desctype, cbInfoValue);
    at least on Win95, calling this with colno==0 would "core" dump/GPF.
    protect, even though it's valid for some values of desctype
    (e.g. SQL_COLUMN_COUNT, since it doesn't depend on the colcount)
*/
    if (colno == 0) {
	dbd_error(sth, SQL_ERROR,
		  "can not obtain SQLColAttributes for column 0");
	return Nullsv;
    }

    rc = SQLColAttributes(imp_sth->hstmt, colno, desctype,
			  rgbInfoValue, sizeof(rgbInfoValue)-1, &cbInfoValue, &fDesc);
    if (!SQL_ok(rc)) {
	dbd_error(sth, rc, "odbc_col_attributes/SQLColAttributes");
	return Nullsv;
    }

    if (DBIS->debug >= 2) {
	fprintf(DBILOGFP,
		"SQLColAttributes: colno=%d, desctype=%d, cbInfoValue=%d, fDesc=%d",
		colno, desctype, cbInfoValue, fDesc
	       );
	if (DBIS->debug>=4)
	    fprintf(DBILOGFP,
		    " rgbInfo=[%02x,%02x,%02x,%02x,%02x,%02x\n",
		    rgbInfoValue[0] & 0xff, rgbInfoValue[1] & 0xff, rgbInfoValue[2] & 0xff, 
		    rgbInfoValue[3] & 0xff, rgbInfoValue[4] & 0xff, rgbInfoValue[5] & 0xff
		   );
	fprintf(DBILOGFP,"\n");
    }

    /*
     * sigh...Oracle's ODBC driver version 8.0.4 resets cbInfoValue to 0, when
     * putting a value in fDesc.  This is a change!
     *
     * double sigh.  SQL Server (and MySql under Unix) set cbInfoValue
     * but use fdesc, not rgbInfoValue.  This change may be problematic
     * for other drivers. (the additional || fDesc != -2...)
     */
    if (cbInfoValue == -2 || cbInfoValue == 0 || fDesc != -2)
	retsv = newSViv(fDesc);
    else if (cbInfoValue != 2 && cbInfoValue != 4)
	retsv = newSVpv(rgbInfoValue, 0);
    else if (rgbInfoValue[cbInfoValue+1] == '\0')
	retsv = newSVpv(rgbInfoValue, 0);
    else {
	if (cbInfoValue == 2)
	    retsv = newSViv(*(short *)rgbInfoValue);
	else
	    retsv = newSViv(*(int *)rgbInfoValue);
    }

    return sv_2mortal(retsv);
}

int	
   odbc_db_columns(dbh, sth, catalog, schema, table, column)
   SV *dbh;
SV *sth;
char *catalog;
char *schema;
char *table;
char *column;
{
    dTHR;
    D_imp_dbh(dbh);
    D_imp_sth(sth);
    RETCODE rc;
    imp_sth->henv = imp_dbh->henv;	/* needed for dbd_error */
    imp_sth->hdbc = imp_dbh->hdbc;

    imp_sth->done_desc = 0;
    rc = SQLAllocStmt(imp_dbh->hdbc, &imp_sth->hstmt);
    if (rc != SQL_SUCCESS) {
	dbd_error(sth, rc, "odbc_db_columns/SQLAllocStmt");
	return 0;
    }

    /* just for sanity, later.  Any internals that may rely on this (including */
    /* debugging) will have valid data */
    imp_sth->statement = (char *)safemalloc(strlen(cSqlColumns)+
					    strlen(XXSAFECHAR(catalog))+
					    strlen(XXSAFECHAR(schema))+
					    strlen(XXSAFECHAR(table))+
					    strlen(XXSAFECHAR(column))+1);

    sprintf(imp_sth->statement,
	    cSqlColumns, XXSAFECHAR(catalog), XXSAFECHAR(schema),
	    XXSAFECHAR(table), XXSAFECHAR(column));

    rc = SQLColumns(imp_sth->hstmt,
		    (catalog && *catalog) ? catalog : 0, SQL_NTS,
		    (schema && *schema) ? schema : 0, SQL_NTS,
		    (table && *table) ? table : 0, SQL_NTS,
		    (column && *column) ? column : 0, SQL_NTS);

    if (DBIS->debug >= 2)
	fprintf(DBILOGFP, "SQLColumns call: cat = %s, schema = %s, table = %s, column = %s\n",
		XXSAFECHAR(catalog), XXSAFECHAR(schema), XXSAFECHAR(table), XXSAFECHAR(column));
    dbd_error(sth, rc, "odbc_columns/SQLColumns");

    if (!SQL_ok(rc)) {
	SQLFreeStmt(imp_sth->hstmt, SQL_DROP);
	imp_sth->hstmt = SQL_NULL_HSTMT;
	return 0;
    }

    return build_results(sth);
}

/* end */
