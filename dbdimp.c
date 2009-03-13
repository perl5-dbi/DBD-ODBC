/* $Id$
 *
 * portions Copyright (c) 1994,1995,1996,1997  Tim Bunce
 * portions Copyright (c) 1997 Thomas K. Wenrich
 * portions Copyright (c) 1997-2001 Jeff Urlwin
 * portions Copyright (c) 2007-2009 Martin J. Evans
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Artistic License, as specified in the Perl README file.
 *
 */

/*
 *  NOTES:
 *
 *  o Trace levels 1 and 2 are reserved for DBI (see DBI::DBD) so don't
 *    use them here in DBIc_TRACE_LEVEL tests.
 *    "Trace Levels" in DBI defines trace levels as:
 *    0 - Trace disabled.
 *    1 - Trace DBI method calls returning with results or errors.
 *    2 - Trace method entry with parameters and returning with results.
 *    3 - As above, adding some high-level information from the driver
 *        and some internal information from the DBI.
 *    4 - As above, adding more detailed information from the driver.
 *    5 to 15 - As above but with more and more obscure information.
 */

#include "ODBC.h"
#if defined(WITH_UNICODE)
# include "unicode_helper.h"
#endif

#if defined(WITH_UNICODE) && (defined(_IODBCUNIX_H) || defined(_IODBCEXT_H))
#error DBD::ODBC will not run properly with iODBC in unicode mode as iODBC defines wide characters as being 4 bytes in size
#endif

/* Can't find a constant in DBI for SQL tracing but it is 256 */
#define SQL_TRACE_FLAG 0x100
#define TRACE0(a,b) PerlIO_printf(DBIc_LOGPIO(a), (b))
#define TRACE1(a,b,c) PerlIO_printf(DBIc_LOGPIO(a), (b), (c))
#define TRACE2(a,b,c,d) PerlIO_printf(DBIc_LOGPIO(a), (b), (c), (d))
#define TRACE3(a,b,c,d,e) PerlIO_printf(DBIc_LOGPIO(a), (b), (c), (d), (e))

static SQLSMALLINT default_parameter_type(imp_sth_t *imp_sth, phs_t *phs);
static int post_connect(SV *dbh, imp_dbh_t *imp_dbh, SV *attr);
static int set_odbc_version(SV *dbh, imp_dbh_t *imp_dbh, SV* attr);
static const char *S_SqlTypeToString (SWORD sqltype);
static const char *S_SqlCTypeToString (SWORD sqltype);
static const char *cSqlTables = "SQLTables(%s,%s,%s,%s)";
static const char *cSqlPrimaryKeys = "SQLPrimaryKeys(%s,%s,%s)";
static const char *cSqlStatistics = "SQLStatistics(%s,%s,%s,%d,%d)";
static const char *cSqlForeignKeys = "SQLForeignKeys(%s,%s,%s,%s,%s,%s)";
static const char *cSqlColumns = "SQLColumns(%s,%s,%s,%s)";
static const char *cSqlGetTypeInfo = "SQLGetTypeInfo(%d)";
static void       AllODBCErrors(HENV henv, HDBC hdbc, HSTMT hstmt, int output, PerlIO *logfp);
static int check_connection_active(SV *h);
static int build_results(SV *sth, SV *dbh, RETCODE orc);

/* AV *dbd_st_fetch(SV * sth, imp_sth_t *imp_sth); */
int dbd_describe(SV *h, imp_sth_t *imp_sth, int more);
int dbd_db_login6_sv(SV *dbh, imp_dbh_t *imp_dbh, SV *dbname, SV *uid, SV *pwd, SV *attr);
int dbd_db_login6(SV *dbh, imp_dbh_t *imp_dbh, char *dbname, char *uid, char *pwd, SV *attr);
int dbd_st_finish(SV *sth, imp_sth_t *imp_sth);

#define av_sz(av) (av_len(av) + 1)

/* for sanity/ease of use with potentially null strings */
#define XXSAFECHAR(p) ((p) ? (p) : "(null)")

/* unique value for db attrib that won't conflict with SQL types, just
 * increment by one if you are adding! */
#define ODBC_IGNORE_NAMED_PLACEHOLDERS 0x8332
#define ODBC_DEFAULT_BIND_TYPE         0x8333
#define ODBC_ASYNC_EXEC                0x8334
#define ODBC_ERR_HANDLER               0x8335
#define ODBC_ROWCACHESIZE              0x8336
#define ODBC_ROWSINCACHE               0x8337
#define ODBC_FORCE_REBIND	       0x8338
#define ODBC_EXEC_DIRECT               0x8339
#define ODBC_VERSION		       0x833A
#define ODBC_CURSORTYPE                0x833B
#define ODBC_QUERY_TIMEOUT             0x833C
#define ODBC_HAS_UNICODE               0x833D
#define ODBC_PUTDATA_START             0x833E
#define ODBC_OUTCON_STR                0x833F
#define ODBC_COLUMN_DISPLAY_SIZE       0x8340

/* This is the bind type for parameters we fall back to if the bind_param
   method was not given a parameter type and SQLDescribeParam is not supported
   or failed */
#ifdef WITH_UNICODE
# define ODBC_BACKUP_BIND_TYPE_VALUE	SQL_WVARCHAR
# define ODBC_BACKUP_LONG_BIND_TYPE_VALUE    SQL_WLONGVARCHAR
#else
# define ODBC_BACKUP_BIND_TYPE_VALUE	SQL_VARCHAR
# define ODBC_BACKUP_LONG_BIND_TYPE_VALUE	SQL_LONGVARCHAR
#endif

SV *dbd_param_err(SQLHANDLE h, int recno);
static int  rebind_param(SV *sth, imp_sth_t *imp_sth, phs_t *phs);
static void get_param_type(SV *sth, imp_sth_t *imp_sth, phs_t *phs);


DBISTATE_DECLARE;

void dbd_init(dbistate_t *dbistate)
{
   DBIS = dbistate;
}



static RETCODE odbc_set_query_timeout(SV *h, HSTMT hstmt, UV odbc_timeout)
{
   RETCODE rc;
   D_imp_xxh(h);
   if (DBIc_TRACE(imp_xxh, 0, 0, 3)) {
      TRACE1(imp_xxh, "   Set timeout to: %d\n", odbc_timeout);
   }
   rc = SQLSetStmtAttr(hstmt,(SQLINTEGER)SQL_ATTR_QUERY_TIMEOUT,
                       (SQLPOINTER)odbc_timeout,(SQLINTEGER)SQL_IS_INTEGER);
   if (!SQL_SUCCEEDED(rc)) {
      /* raise warnings if setting fails, but don't die? */
       if (DBIc_TRACE(imp_xxh, 0, 0, 3))
           TRACE1(imp_xxh,
                  "    !!Failed to set Statement ATTR Query Timeout to %d\n",
                  (int)odbc_timeout);
   }
   return rc;
}
#if 0
static UV odbc_get_query_timeout(imp_dbh_t *imp_dbh) {
   RETCODE rc;
   UV timeout;
   rc = SQLGetConnectAttr(imp_dbh->hdbc,
                          (SQLINTEGER)SQL_ATTR_QUERY_TIMEOUT,
                          (SQLPOINTER)&timeout,sizeof(timeout), 0);
   if (!SQL_SUCCEEDED(rc)) {
       if (DBIc_TRACE(imp_dbh, 0, 0, 3))
           TRACE1(imp_dbh, "    !!Failed to get ATTR query Timeout %d\n", rc);
   }
   return timeout;
}
#endif  /* 0 */



static void odbc_clear_result_set(SV *sth, imp_sth_t *imp_sth)
{
   SV *value;
   char *key;
   I32 keylen;

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

   while ( (value = hv_iternextsv((HV*)SvRV(sth), &key, &keylen)) ) {
      if (strncmp(key, "NAME_", 5) == 0 ||
	  strncmp(key, "TYPE", 4) == 0 ||
	  strncmp(key, "PRECISION", 9) == 0 ||
	  strncmp(key, "SCALE", 5) == 0 ||
	  strncmp(key, "NULLABLE", 8) == 0
	   ) {
	 hv_delete((HV*)SvRV(sth), key, keylen, G_DISCARD);
	 if (DBIc_TRACE(imp_sth, 0, 0, 4)) {
             TRACE2(imp_sth, "    ODBC_CLEAR_RESULTS '%s' => %s\n",
                    key, neatsvpv(value,0));
	 }
      }
   }
   /*
   PerlIO_printf(DBILOGFP,"CLEAR RESULTS cached attributes:\n");
   while ( (value = hv_iternextsv((HV*)SvRV(inner), &key, &keylen)) ) {
   PerlIO_printf(DBILOGFP,"CLEARRESULTS '%s' => %s\n", key, neatsvpv(value,0));
   }
*/

   imp_sth->fbh       = NULL;
   imp_sth->ColNames  = NULL;
   imp_sth->RowBuffer = NULL;
   imp_sth->done_desc = 0;

}



static void odbc_handle_outparams(imp_sth_t *imp_sth, int debug)
{
   int i = (imp_sth->out_params_av) ? AvFILL(imp_sth->out_params_av)+1 : 0;
   if (debug >= 3)
      TRACE1(imp_sth, "    processing %d output parameters\n", i);

   while (--i >= 0) {
      phs_t *phs = (phs_t*)(void*)SvPVX(AvARRAY(imp_sth->out_params_av)[i]);
      SV *sv = phs->sv;
      if (debug >= 8) {
	 TRACE2(imp_sth, "    outparam %s, length:%d\n",
                phs->name, phs->strlen_or_ind);
      }

      /* phs->strlen_or_ind has been updated by ODBC to hold the length
       * of the result */
      if (phs->strlen_or_ind != SQL_NULL_DATA) {
          /*
           * When ODBC fills an output parameter buffer, the size of the
           * data that were available is written into the memory location
           * provided by strlen_or_ind pointer argument during the
           * SQLBindParameter() call.
           *
           * If the number of bytes available exceeds the size of the output
           * buffer, ODBC will truncate the data such that it fits in the
           * available buffer. However, the strlen_or_ind will still reflect
           * the size of the data before it was truncated.
           *
           * This fact provides us a way to detect truncation on this particular
           * output parameter.  Otherwise, the only way to detect truncation is
           * through a follow-up to a SQL_SUCCESS_WITH_INFO result.  Such a call
           * cannot return enough information to state exactly where the
           * truncation occurred.
           */
          SvPOK_only(sv);           /* string, disable other OK bits */
          if (phs->strlen_or_ind > phs->maxlen) { /* out param truncated */
              SvCUR_set(sv, phs->maxlen);
              *SvEND(sv) = '\0';                /* null terminate */

              if (debug >= 2) {
                  PerlIO_printf(
                      DBIc_LOGPIO(imp_sth),
                      "    outparam %s = '%s'\t(TRUNCATED from %ld to %ld)\n",
                      phs->name, SvPV_nolen(sv), (long)phs->strlen_or_ind,
                      (long)phs->maxlen);
              }
          } else {                        /* no truncation occurred */
              SvCUR_set(sv, phs->strlen_or_ind); /* new length */
              *SvEND(sv) = '\0';                 /* null terminate */
              if (phs->strlen_or_ind == phs->maxlen &&
                  (phs->sql_type == SQL_NUMERIC ||
                   phs->sql_type == SQL_DECIMAL ||
                   phs->sql_type == SQL_INTEGER ||
                   phs->sql_type == SQL_SMALLINT ||
                   phs->sql_type == SQL_FLOAT ||
                   phs->sql_type == SQL_REAL ||
                   phs->sql_type == SQL_DOUBLE)) {
                  /*
                   * fix up for oracle, which leaves the buffer at the size
                   * requested, but only returns a few characters.  The
                   * intent is to truncate down to the actual number of
                   * characters necessary.  Need to find the first null
                   * byte and set the length there.
                   */
                  char *pstart = SvPV_nolen(sv);
                  char *p = pstart;
                  while (*p != '\0') {
                      p++;
                  }

                  if (debug >= 2) {
                      PerlIO_printf(
                          DBIc_LOGPIO(imp_sth),
                          "    outparam %s = '%s'\t(len %ld), is numeric end"
                          " of buffer = %d\n",
                          phs->name, SvPV(sv,na), (long)phs->strlen_or_ind,
                          phs->sql_type, p - pstart);
                  }
                  SvCUR_set(sv, p - pstart);
              }
          }
      } else { /* is NULL */
          if (debug >= 2)
              TRACE1(imp_sth, "    outparam %s = undef (NULL)\n", phs->name);
          (void)SvOK_off(phs->sv);
      }
   }
}



static int build_results(SV *sth, SV *dbh, RETCODE orc)
{
   RETCODE rc;
   D_imp_dbh(dbh);
   D_imp_sth(sth);
   dTHR;

   if (DBIc_TRACE(imp_sth, 0, 0, 3))
       TRACE2(imp_sth, "    build_results sql f%d\n\t%s\n",
              imp_sth->hstmt, imp_sth->statement);

   /* init sth pointers */
   imp_sth->fbh = NULL;
   imp_sth->ColNames = NULL;
   imp_sth->RowBuffer = NULL;
   imp_sth->RowCount = -1;
   imp_sth->eod = -1;

   imp_sth->odbc_column_display_size = imp_dbh->odbc_column_display_size;

   if (!dbd_describe(sth, imp_sth, 0)) {
      /* SQLFreeStmt(imp_sth->hstmt, SQL_DROP); */ /* TBD: 3.0 update */
       if (DBIc_TRACE(imp_sth, 0, 0, 3)) {
           TRACE0(imp_sth, "    !!dbd_describe failed, build_results...!\n");
      }
      SQLFreeHandle(SQL_HANDLE_STMT, imp_sth->hstmt);
      imp_sth->hstmt = SQL_NULL_HSTMT;
      return 0; /* dbd_describe already called dbd_error()	*/
   }

   if (DBIc_TRACE(imp_sth, 0, 0, 3)) {
       TRACE0(imp_sth, "    dbd_describe build_results #2...!\n");
   }
   if (dbd_describe(sth, imp_sth, 0) <= 0) {
       if (DBIc_TRACE(imp_sth, 0, 0, 3)) {
           TRACE0(imp_sth, "    dbd_describe build_results #3...!\n");
      }
      return 0;
   }

   DBIc_IMPSET_on(imp_sth);

   if (orc != SQL_NO_DATA) {
      imp_sth->RowCount = -1;
      rc = SQLRowCount(imp_sth->hstmt, &imp_sth->RowCount);
      dbd_error(sth, rc, "build_results/SQLRowCount");
      if (rc != SQL_SUCCESS) {
	 return -1;
      }
   } else {
      imp_sth->RowCount = 0;
   }

   DBIc_ACTIVE_on(imp_sth); /* XXX should only set for select ?	*/
   imp_sth->eod = SQL_SUCCESS;
   return 1;
}



int odbc_discon_all(SV *drh, imp_drh_t *imp_drh)
{
   dTHR;
   /* The disconnect_all concept is flawed and needs more work */
   if (!dirty && !SvTRUE(perl_get_sv("DBI::PERL_ENDING",0))) {
       DBIh_SET_ERR_CHAR(drh, (imp_xxh_t*)imp_drh, Nullch, 1,
                         "disconnect_all not implemented", Nullch, Nullch);
      return FALSE;
   }
   return FALSE;
}




/* error : <=(-2), ok row count : >=0, unknown count : (-1)   */
int dbd_db_execdirect( SV *dbh,
		       SV *statement )
{
   D_imp_dbh(dbh);
   SQLRETURN ret;
   SQLLEN rows;
   SQLHSTMT stmt;
   int dbh_active;
   char *sql;
   STRLEN sql_len;

   sql = SvPV(statement, sql_len);

   if ((dbh_active = check_connection_active(dbh)) == 0) return 0;

   ret = SQLAllocHandle(SQL_HANDLE_STMT,  imp_dbh->hdbc, &stmt );
   if (!SQL_SUCCEEDED(ret)) {
      dbd_error( dbh, ret, "Statement allocation error" );
      return(-2);
   }

   /* if odbc_query_timeout has been set, set it in the driver */
   if (imp_dbh->odbc_query_timeout != -1) {
      ret = odbc_set_query_timeout(dbh, stmt, imp_dbh->odbc_query_timeout);
      if (!SQL_SUCCEEDED(ret)) {
	 dbd_error(dbh, ret, "execdirect set_query_timeout");
      }
      /* don't fail if the query timeout can't be set. */
   }

   if (DBIc_TRACE(imp_dbh, SQL_TRACE_FLAG, 0, 3)) {
       TRACE1(imp_dbh, "    SQLExecDirect %s\n", SvPV_nolen(statement));
   }

#ifdef WITH_UNICODE
   if (SvOK(statement) && DO_UTF8(statement)) {
       SQLWCHAR *wsql;
       STRLEN wsql_len;
       SV *sql_copy;

       if (DBIc_TRACE(imp_dbh, 0x02000000, 0, 0))
           TRACE0(imp_dbh, "    Processing utf8 sql in unicode mode\n");

       sql_copy = sv_mortalcopy(statement);

       SV_toWCHAR(sql_copy);

       wsql = (SQLWCHAR *)SvPV(sql_copy, wsql_len);

       ret = SQLExecDirectW(stmt, wsql, wsql_len / sizeof(SQLWCHAR));
   } else {
       if (DBIc_TRACE(imp_dbh, 0x02000000, 0, 0))
           TRACE0(imp_dbh, "    Processing utf8 sql in non-unicode mode\n");

       ret = SQLExecDirect(stmt, (SQLCHAR *)sql, SQL_NTS);
   }
#else
   if (DBIc_TRACE(imp_dbh, 0x02000000, 0, 0))
       TRACE0(imp_dbh, "    Processing utf8 sql in non-unicode mode\n");
   ret = SQLExecDirect(stmt, (SQLCHAR *)sql, SQL_NTS);
#endif
   if (DBIc_TRACE(imp_dbh, 0, 0, 3))
      TRACE1(imp_dbh, "    SQLExecDirect = %d\n", ret);
   if (!SQL_SUCCEEDED(ret) && ret != SQL_NO_DATA) {
      dbd_error2(dbh, ret, "Execute immediate failed",
                 imp_dbh->henv, imp_dbh->hdbc, stmt );
      if (ret < 0)  {
	 rows = -2;
      }
      else {
	 rows = -3; /* ?? */
      }
   }
   else {
      if (ret == SQL_NO_DATA) {
	 rows = 0;
      } else {
	 ret = SQLRowCount(stmt, &rows);
	 if (!SQL_SUCCEEDED(ret)) {
	    dbd_error( dbh, ret, "SQLRowCount failed" );
	    if (ret < 0)
	       rows = -1;
	 }
      }
   }
   ret = SQLFreeHandle(SQL_HANDLE_STMT,stmt);
   if (!SQL_SUCCEEDED(ret)) {
      dbd_error2(dbh, ret, "Statement destruction error",
                 imp_dbh->henv, imp_dbh->hdbc, stmt);
   }

   return (int)rows;
}



void dbd_db_destroy(SV *dbh, imp_dbh_t *imp_dbh)
{
   if (DBIc_ACTIVE(imp_dbh))
      dbd_db_disconnect(dbh, imp_dbh);
   /* Nothing in imp_dbh to be freed	*/

   DBIc_IMPSET_off(imp_dbh);
   if (DBIc_TRACE(imp_dbh, 0, 0, 8))
       TRACE0(imp_dbh, "    DBD::ODBC Disconnected!\n");
}




/*
 * quick dumb function to handle case insensitivity for DSN= or DRIVER=
 * in DSN...note this is becuase strncmpi is not available on all
 * platforms using that name (VC++, Debian, etc most notably).
 * Note, also, strupr doesn't seem to have a standard name, either...
 */

int dsnHasDriverOrDSN(char *dsn) {

   char upper_dsn[512];
   char *cp = upper_dsn;
   strncpy(upper_dsn, dsn, sizeof(upper_dsn)-1);
   upper_dsn[sizeof(upper_dsn)-1] = '\0';
   while (*cp != '\0') {
      *cp = toupper(*cp);
      cp++;
   }
   return (strncmp(upper_dsn, "DSN=", 4) == 0 ||
           strncmp(upper_dsn, "DRIVER=", 7) == 0);
}



int dsnHasUIDorPWD(char *dsn) {

   char upper_dsn[512];
   char *cp = upper_dsn;
   strncpy(upper_dsn, dsn, sizeof(upper_dsn)-1);
   upper_dsn[sizeof(upper_dsn)-1] = '\0';
   while (*cp != '\0') {
      *cp = toupper(*cp);
      cp++;
   }
   return (strstr(upper_dsn, "UID=") != 0 || strstr(upper_dsn, "PWD=") != 0);
}



/*------------------------------------------------------------
connecting to a data source.
Allocates henv and hdbc.
NOTE: This is the old 5 argument version
------------------------------------------------------------*/
int
   dbd_db_login(dbh, imp_dbh, dbname, uid, pwd)
   SV *dbh; imp_dbh_t *imp_dbh; char *dbname; char *uid; char *pwd;
{
   return dbd_db_login6(dbh, imp_dbh, dbname, uid, pwd, Nullsv);
}



/************************************************************************/
/*                                                                      */
/*  dbd_db_login6_sv                                                    */
/*  ================                                                    */
/*                                                                      */
/*  This API was introduced to DBI after 1.607 (subversion revision     */
/*  11723) and is the same as dbd_db_login6 except the connection       */
/*  strings are SVs so we can detect unicode strings and call           */
/*  SQLDriverConnectW.                                                  */
/*                                                                      */
/************************************************************************/
int dbd_db_login6_sv(
    SV *dbh,
    imp_dbh_t *imp_dbh,
    SV *dbname,
    SV *uid,
    SV *pwd,
    SV *attr)
{
#ifndef WITH_UNICODE
   if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
       TRACE0(imp_dbh, "non-Unicode login6\n");
   return dbd_db_login6(dbh, imp_dbh, SvPV_nolen(dbname), SvPV_nolen(uid),
                         SvPV_nolen(pwd), attr);
#else

   D_imp_drh_from_dbh;
   dTHR;
   RETCODE rc;
   SV *wconstr;
   SQLWCHAR dc_constr[512];
   STRLEN dc_constr_len;

   if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0)) {
       TRACE0(imp_dbh, "Unicode login6 \n");
       TRACE2(imp_dbh, "dbname=%s, uid=%s, pwd=xxxxx\n",
              SvPV_nolen(dbname), SvPV_nolen(uid));
   }

   imp_dbh->out_connect_string = NULL;

   if (!imp_drh->connects) {
      rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &imp_drh->henv);
      dbd_error(dbh, rc, "db_login/SQLAllocHandle(env)");
      if (!SQL_SUCCEEDED(rc)) return 0;

      if (set_odbc_version(dbh, imp_dbh, attr) != 1) return 0;
   }

   imp_dbh->henv = imp_drh->henv;	/* needed for dbd_error */

   rc = SQLAllocHandle(SQL_HANDLE_DBC, imp_drh->henv, &imp_dbh->hdbc);
   if (!SQL_SUCCEEDED(rc)) {
      dbd_error(dbh, rc, "db_login/SQLAllocHandle(dbc)");
      if (imp_drh->connects == 0) {
          SQLFreeHandle(SQL_HANDLE_ENV, imp_drh->henv);
          imp_drh->henv = SQL_NULL_HENV;
          imp_dbh->henv = SQL_NULL_HENV;    /* needed for dbd_error */
      }
      return 0;
   }

   /* If the connection string is too long to pass to SQLConnect or it
      contains DSN or DRIVER, we've little choice to but to call
      SQLDriverConnect and need to tag the uid/pwd on the end of the
      connection string (unless they already exist). */

   if ((SvCUR(dbname) > SQL_MAX_DSN_LENGTH ||
        dsnHasDriverOrDSN(SvPV_nolen(dbname))) &&
       !dsnHasUIDorPWD(SvPV_nolen(dbname))) {
       sv_catpvf(dbname, ";UID=%s;PWD=%s;",
                 SvPV_nolen(uid), SvPV_nolen(pwd));
       if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
           TRACE1(imp_dbh, "Now using dbname = %s\n", SvPV_nolen(dbname));
   }

   if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
      TRACE2(imp_dbh, "    SQLDriverConnect '%s', '%s', 'xxxx'\n",
             SvPV_nolen(dbname), SvPV_nolen(uid));

   wconstr = sv_mortalcopy(dbname);
#ifdef sv_utf8_decode
   sv_utf8_decode(wconstr);
#else
   SvUTF8_on(wconstr);
#endif
   SV_toWCHAR(wconstr);

   /* The following is to work around a bug in SQLDriverConnectW in unixODBC
      which in at least 2.2.11 (and probably up to 2.2.13 official release
      [not pre-release]) core dumps if the wide connection string does not end
      in a 0. */
   {
       char *p;

       memset(dc_constr, '\0', sizeof(dc_constr));
       p = SvPV(wconstr, dc_constr_len);
       if (dc_constr_len > (sizeof(dc_constr) - 2)) {
           croak("Cannot process connection string - too long");
       }
       memcpy(dc_constr, p, dc_constr_len);
   }

   {
       SQLWCHAR wout_str[512];
       SQLSMALLINT wout_str_len;

       rc = SQLDriverConnectW(imp_dbh->hdbc,
                              0, /* no hwnd */
                              dc_constr,
                              (SQLSMALLINT)(dc_constr_len / sizeof(SQLWCHAR)),
                              wout_str, sizeof(wout_str) / sizeof(wout_str[0]),
                              &wout_str_len,
                              SQL_DRIVER_NOPROMPT);
       if (SQL_SUCCEEDED(rc)) {
           imp_dbh->out_connect_string = sv_newwvn(wout_str, wout_str_len);
           if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
               TRACE1(imp_dbh, "Out connection string: %s\n",
                      SvPV_nolen(imp_dbh->out_connect_string));
       }
   }

   if (!SQL_SUCCEEDED(rc)) {
       SV *wuid, *wpwd;
       if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
           TRACE0(imp_dbh, "    SQLDriverConnect failed:\n");
       /*
        * Added code for DBD::ODBC 0.39 to help return a better
        * error code in the case where the user is using a
        * DSN-less connection and the dbname doesn't look like a
        * true DSN.
        */
       /* wanted to use strncmpi, but couldn't find one on all
        * platforms.  Sigh. */
       if (SvCUR(dbname) > SQL_MAX_DSN_LENGTH ||
           dsnHasDriverOrDSN(SvPV_nolen(dbname))) {

           /* must be DSN= or some "direct" connection attributes,
            * probably best to error here and give the user a real
            * error code because the SQLConnect call could hide the
            * real problem.
            */
           dbd_error(dbh, rc, "db_login/SQLConnect");
           SQLFreeHandle(SQL_HANDLE_DBC, imp_dbh->hdbc);
           if (imp_drh->connects == 0) {
               SQLFreeHandle(SQL_HANDLE_ENV, imp_drh->henv);
               imp_drh->henv = SQL_NULL_HENV;
               imp_dbh->henv = SQL_NULL_HENV;	/* needed for dbd_error */
           }
           return 0;
       }

       /* ok, the DSN is short, so let's try to use it to connect
        * and quietly take all error messages */
       AllODBCErrors(imp_dbh->henv, imp_dbh->hdbc, 0, 0, DBIc_LOGPIO(imp_dbh));

       if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
           TRACE2(imp_dbh, "    SQLConnect '%s', '%s'\n",
                  neatsvpv(dbname, 0), neatsvpv(uid, 0));

       wconstr = sv_mortalcopy(dbname);
#ifdef sv_utf8_decode
       sv_utf8_decode(wconstr);
#else
       SvUTF8_on(wconstr);
#endif
       SV_toWCHAR(wconstr);

       wuid = sv_mortalcopy(uid);
#ifdef sv_utf8_decode
       sv_utf8_decode(wuid);
#else
       SvUTF8_on(wuid);
#endif
       SV_toWCHAR(wuid);

       wpwd = sv_mortalcopy(pwd);
#ifdef sv_utf8_decode
       sv_utf8_decode(wpwd);
#else
       SvUTF8_on(wpwd);
#endif
       SV_toWCHAR(wpwd);

       rc = SQLConnectW(imp_dbh->hdbc,
                        (SQLWCHAR *)SvPV_nolen(wconstr),
                        (SQLSMALLINT)(SvCUR(wconstr) / sizeof(SQLWCHAR)),
                        (SQLWCHAR *)SvPV_nolen(wuid),
                        (SQLSMALLINT)(SvCUR(wuid) / sizeof(SQLWCHAR)),
                        (SQLWCHAR *)SvPV_nolen(wpwd),
                        (SQLSMALLINT)(SvCUR(wpwd) / sizeof(SQLWCHAR)));
   }
   if (!SQL_SUCCEEDED(rc)) {
      dbd_error(dbh, rc, "db_login/SQLConnect");
      SQLFreeHandle(SQL_HANDLE_DBC, imp_dbh->hdbc);/* TBD: 3.0 update */
      if (imp_drh->connects == 0) {
          SQLFreeHandle(SQL_HANDLE_ENV, imp_drh->henv);
          imp_drh->henv = SQL_NULL_HENV;
          imp_dbh->henv = SQL_NULL_HENV;	/* needed for dbd_error */
      }
      return 0;
   } else if (rc == SQL_SUCCESS_WITH_INFO) {
       dbd_error(dbh, rc, "db_login/SQLConnect");
   }

   if (post_connect(dbh, imp_dbh, attr) != 1) return 0;


   imp_drh->connects++;
   DBIc_IMPSET_on(imp_dbh);	/* imp_dbh set up now			*/
   DBIc_ACTIVE_on(imp_dbh);	/* call disconnect before freeing	*/
   return 1;
#endif  /* WITH_UNICODE */

}



/************************************************************************/
/*                                                                      */
/*  dbd_db_login6                                                       */
/*  =============                                                       */
/*                                                                      */
/*  A newer version of the dbd_db_login API with the additional attr as */
/*  the sixth argument. Once everyone upgrades to at least              */
/*  DBI 1.60X (where X > 7) this API won't get called anymore since     */
/*  dbd_db_login6_sv will be favoured.                                  */
/*                                                                      */
/*  NOTE: I had hoped to make dbd_db_login6_sv support Unicode and      */
/*  dbd_db_login6 to not support Unicode but as no one (except me) has  */
/*  a DBI which supports dbd_db_login6_sv and unixODBC REQUIRES us to   */
/*  call SQLDriverConnectW if we are going to call other SQLXXXW        */
/*  functions later I've got no choice but to convert the ASCII strings */
/*  passed to dbd_db_login6 to wide characters when DBD::ODBC is built  */
/*  for Unicode.                                                        */
/*                                                                      */
/************************************************************************/
int dbd_db_login6(
    SV *dbh,
    imp_dbh_t *imp_dbh,
    char *dbname,
    char *uid,
    char *pwd,
    SV *attr)
{
   D_imp_drh_from_dbh;
   dTHR;

   RETCODE rc;
   char dbname_local[512];
#ifdef WITH_UNICODE
   SQLWCHAR wconstr[512];
   STRLEN wconstr_len;
   unsigned int i;
#endif

   if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
       TRACE0(imp_dbh, "dbd_db_login6\n");
   if (!imp_drh->connects) {
      rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &imp_drh->henv);
      dbd_error(dbh, rc, "db_login/SQLAllocHandle(env)");
      if (!SQL_SUCCEEDED(rc)) return 0;

      if (set_odbc_version(dbh, imp_dbh, attr) != 1) return 0;
   }

   imp_dbh->henv = imp_drh->henv;	/* needed for dbd_error */

   imp_dbh->out_connect_string = NULL;

   rc = SQLAllocHandle(SQL_HANDLE_DBC, imp_drh->henv, &imp_dbh->hdbc);
   if (!SQL_SUCCEEDED(rc)) {
      dbd_error(dbh, rc, "db_login/SQLAllocHandle(dbc)");
      if (imp_drh->connects == 0) {
          SQLFreeHandle(SQL_HANDLE_ENV, imp_drh->henv);
          imp_drh->henv = SQL_NULL_HENV;
          imp_dbh->henv = SQL_NULL_HENV;    /* needed for dbd_error */
      }
      return 0;
   }

#ifndef DBD_ODBC_NO_SQLDRIVERCONNECT
   /* If the connection string is too long to pass to SQLConnect or it
      contains DSN or DRIVER, we've little choice to but to call
      SQLDriverConnect and need to tag the uid/pwd on the end of the
      connection string (unless they already exist). */

   if ((strlen(dbname) > SQL_MAX_DSN_LENGTH ||
        dsnHasDriverOrDSN(dbname)) && !dsnHasUIDorPWD(dbname)) {
       if ((strlen(dbname) + strlen(uid) + strlen(pwd) + 12) >
           sizeof(dbname_local)) {
           croak("Connection string too long");
       }
       sprintf(dbname_local, "%s;UID=%s;PWD=%s;", dbname, uid, pwd);
       dbname = dbname_local;
   }

   if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
      TRACE2(imp_dbh, "    SQLDriverConnect '%s', '%s', 'xxxx'\n", dbname, uid);

# ifdef WITH_UNICODE
   if (strlen(dbname) > (sizeof(wconstr) / sizeof(wconstr[0]))) {
       croak("Connection string too big to convert to wide characters");
   }

   for (i = 0; i < strlen(dbname); i++) {
       wconstr[i] = dbname[i];
   }
   wconstr[i] = 0;
   wconstr_len = i;

   {
       SQLWCHAR wout_str[512];
       SQLSMALLINT wout_str_len;

       rc = SQLDriverConnectW(imp_dbh->hdbc,
                              0, /* no hwnd */
                              wconstr, (SQLSMALLINT)wconstr_len,
                              wout_str, sizeof(wout_str) / sizeof(wout_str[0]),
                              &wout_str_len,
                              SQL_DRIVER_NOPROMPT);
       if (SQL_SUCCEEDED(rc)) {
           imp_dbh->out_connect_string = sv_newwvn(wout_str, wout_str_len);
           if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
               TRACE1(imp_dbh, "Out connection string: %s\n",
                      SvPV_nolen(imp_dbh->out_connect_string));
       }
   }

# else  /* WITH_UNICODE */

   {
       char out_str[512];
       SQLSMALLINT out_str_len;

       rc = SQLDriverConnect(imp_dbh->hdbc,
                             0, /* no hwnd */
                             dbname,
                             (SQLSMALLINT)strlen(dbname),
                             out_str, sizeof(out_str), &out_str_len,
                             SQL_DRIVER_NOPROMPT);
       if (SQL_SUCCEEDED(rc)) {
           imp_dbh->out_connect_string = newSVpv(out_str, out_str_len);
           if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
       	       TRACE1(imp_dbh, "Out connection string: %s\n",
                      SvPV_nolen(imp_dbh->out_connect_string));
       }
   }

# endif  /* WITH_UNICODE */
#else
   /* if we are using something that can not handle SQLDriverconnect,
    * then set rc to a not OK state and we'll fall back on SQLConnect
    */
   rc = SQL_ERROR;
#endif

   if (!SQL_SUCCEEDED(rc)) {
       if (DBIc_TRACE(imp_dbh, 0, 0, 4)) {
#ifdef DBD_ODBC_NO_SQLDRIVERCONNECT
           TRACE0(imp_dbh, "    !SQLDriverConnect unsupported.\n");
#else
           TRACE0(imp_dbh, "    SQLDriverConnect failed:\n");
#endif
      }

#ifndef DBD_ODBC_NO_SQLDRIVERCONNECT
      /*
       * Added code for DBD::ODBC 0.39 to help return a better
       * error code in the case where the user is using a
       * DSN-less connection and the dbname doesn't look like a
       * true DSN.
       */
      /* wanted to use strncmpi, but couldn't find one on all
       * platforms.  Sigh. */
      if (strlen(dbname) > SQL_MAX_DSN_LENGTH || dsnHasDriverOrDSN(dbname)) {

	 /* must be DSN= or some "direct" connection attributes,
	  * probably best to error here and give the user a real
	  * error code because the SQLConnect call could hide the
	  * real problem.
	  */
	 dbd_error(dbh, rc, "db_login/SQLConnect");
	 SQLFreeHandle(SQL_HANDLE_DBC, imp_dbh->hdbc);
	 if (imp_drh->connects == 0) {
             SQLFreeHandle(SQL_HANDLE_ENV, imp_drh->henv);
             imp_drh->henv = SQL_NULL_HENV;
             imp_dbh->henv = SQL_NULL_HENV;	/* needed for dbd_error */
	 }
	 return 0;
      }

      /* ok, the DSN is short, so let's try to use it to connect
       * and quietly take all error messages */
      AllODBCErrors(imp_dbh->henv, imp_dbh->hdbc, 0, 0, DBIc_LOGPIO(imp_dbh));
#endif /* DriverConnect supported */

      if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
          TRACE2(imp_dbh, "    SQLConnect '%s', '%s'\n", dbname, uid);
#ifdef WITH_UNICODE
      {
          SQLWCHAR wuid[100], wpwd[100];
          for (i = 0; i < strlen(uid); i++) {
              wuid[i] = uid[i];
          }
          wuid[i] = 0;
          for (i = 0; i < strlen(pwd); i++) {
              wpwd[i] = pwd[i];
          }
          wpwd[i] = 0;
          for (i = 0; i < strlen(dbname); i++) {
              wconstr[i] = dbname[i];
          }
          wconstr[i] = 0;
          wconstr_len = i;

          rc = SQLConnectW(imp_dbh->hdbc,
                           wconstr, wconstr_len,
                          wuid, (SQLSMALLINT)strlen(uid),
                          wpwd, (SQLSMALLINT)strlen(pwd));
      }
#else
      rc = SQLConnect(imp_dbh->hdbc,
		      dbname, (SQLSMALLINT)strlen(dbname),
		      uid, (SQLSMALLINT)strlen(uid),
		      pwd, (SQLSMALLINT)strlen(pwd));
#endif
   }

   if (!SQL_SUCCEEDED(rc)) {
      dbd_error(dbh, rc, "db_login/SQLConnect");
      SQLFreeHandle(SQL_HANDLE_DBC, imp_dbh->hdbc);/* TBD: 3.0 update */
      if (imp_drh->connects == 0) {
          SQLFreeHandle(SQL_HANDLE_ENV, imp_drh->henv);
          imp_drh->henv = SQL_NULL_HENV;
          imp_dbh->henv = SQL_NULL_HENV;	/* needed for dbd_error */
      }
      return 0;
   } else if (rc == SQL_SUCCESS_WITH_INFO) {
       dbd_error(dbh, rc, "db_login/SQLConnect");
   }

   if (post_connect(dbh, imp_dbh, attr) != 1) return 0;


   imp_drh->connects++;
   DBIc_IMPSET_on(imp_dbh);	/* imp_dbh set up now			*/
   DBIc_ACTIVE_on(imp_dbh);	/* call disconnect before freeing	*/
   return 1;
}



int dbd_db_disconnect(SV *dbh, imp_dbh_t *imp_dbh)
{
   RETCODE rc;
   D_imp_drh_from_dbh;
   UDWORD autoCommit = 0;
   dTHR;

   /* We assume that disconnect will always work	*/
   /* since most errors imply already disconnected.	*/
   DBIc_ACTIVE_off(imp_dbh);

   if (imp_dbh->out_connect_string) {
       SvREFCNT_dec(imp_dbh->out_connect_string);
   }

   /* If not autocommit, should we rollback?  I don't think that's
    * appropriate.  -- TBD: Need to check this, maybe we should
    * rollback?
    */

   /* TBD: 3.0 update */
   rc = SQLGetConnectOption(imp_dbh->hdbc, SQL_AUTOCOMMIT, &autoCommit);
   /* quietly handle a problem with SQLGetConnectOption() */
   if (!SQL_SUCCEEDED(rc)) {
       AllODBCErrors(imp_dbh->henv, imp_dbh->hdbc, 0,
                     DBIc_TRACE(imp_dbh, 0, 0, 4), DBIc_LOGPIO(imp_dbh));
   }
   else {
      if (!autoCommit) {
	 rc = dbd_db_rollback(dbh, imp_dbh);
	 if (DBIc_TRACE(imp_dbh, 0, 0, 3)) {
             TRACE1(imp_dbh,
                 "** auto-rollback due to disconnect without commit"
                 " returned %d\n", rc);
	 }
      }
   }
   rc = SQLDisconnect(imp_dbh->hdbc);
   if (!SQL_SUCCEEDED(rc)) {
      dbd_error(dbh, rc, "db_disconnect/SQLDisconnect");
      /* return 0;	XXX if disconnect fails, fall through... */
   }
   if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
       TRACE1(imp_dbh, "SQLDisconnect=%d\n", rc);

   SQLFreeHandle(SQL_HANDLE_DBC, imp_dbh->hdbc);
   imp_dbh->hdbc = SQL_NULL_HDBC;
   imp_drh->connects--;
   strcpy(imp_dbh->odbc_dbname, "disconnect");
   if (imp_drh->connects == 0) {
       SQLFreeHandle(SQL_HANDLE_ENV, imp_drh->henv);
       imp_drh->henv = SQL_NULL_HENV;
       imp_dbh->henv = SQL_NULL_HENV;	/* needed for dbd_error */
   }
   /* We don't free imp_dbh since a reference still exists	*/
   /* The DESTROY method is the only one to 'free' memory.	*/
   /* Note that statement objects may still exists for this dbh!	*/

   return 1;
}




int dbd_db_commit(SV *dbh, imp_dbh_t *imp_dbh)
{
   RETCODE rc;
   dTHR;

   /* TBD: 3.0 update */
   rc = SQLTransact(imp_dbh->henv, imp_dbh->hdbc, SQL_COMMIT);
   if (!SQL_SUCCEEDED(rc)) {
      dbd_error(dbh, rc, "db_commit/SQLTransact");
      return 0;
   }
   /* support for DBI 1.20 begin_work */
   if (DBIc_has(imp_dbh, DBIcf_BegunWork)) {
      /* reset autocommit */
      rc = SQLSetConnectOption(imp_dbh->hdbc, SQL_AUTOCOMMIT,
                               SQL_AUTOCOMMIT_ON);
      DBIc_off(imp_dbh,DBIcf_BegunWork);
   }
   return 1;
}



int dbd_db_rollback(SV *dbh, imp_dbh_t *imp_dbh)
{
   RETCODE rc;
   dTHR;

   /* TBD: 3.0 update */
   rc = SQLTransact(imp_dbh->henv, imp_dbh->hdbc, SQL_ROLLBACK);
   if (!SQL_SUCCEEDED(rc)) {
      dbd_error(dbh, rc, "db_rollback/SQLTransact");
      return 0;
   }
   /* support for DBI 1.20 begin_work */
   if (DBIc_has(imp_dbh, DBIcf_BegunWork)) {
      /*  reset autocommit */
      rc = SQLSetConnectOption(imp_dbh->hdbc, SQL_AUTOCOMMIT,
                               SQL_AUTOCOMMIT_ON);
      DBIc_off(imp_dbh,DBIcf_BegunWork);
   }
   return 1;
}



void dbd_error2(
    SV *h,
    RETCODE err_rc,
    char *what,
    HENV henv,
    HDBC hdbc,
    HSTMT hstmt)
{
   D_imp_xxh(h);
   dTHR;

   /*
    * It's a shame to have to add all this stuff with imp_dbh and
    * imp_sth, but imp_dbh is needed to get the odbc_err_handler
    * and imp_sth is needed to get imp_dbh.
    */
   struct imp_dbh_st *imp_dbh = NULL;
   struct imp_sth_st *imp_sth = NULL;

   if (DBIc_TRACE(imp_xxh, 0, 0, 4)) {
       PerlIO_printf(DBIc_LOGPIO(imp_xxh),
                     "    !!dbd_error2(err_rc=%d, what=%s, handles=(%p,%p,%p)\n",
                     err_rc, (what ? what : "null"), henv, hdbc, hstmt);
   }

   switch(DBIc_TYPE(imp_xxh)) {
      case DBIt_ST:
	 imp_sth = (struct imp_sth_st *)(imp_xxh);
	 imp_dbh = (struct imp_dbh_st *)(DBIc_PARENT_COM(imp_sth));
	 break;
      case DBIt_DB:
	 imp_dbh = (struct imp_dbh_st *)(imp_xxh);
	 break;
      default:
	 croak("panic: dbd_error2 on bad handle type");
   }

   while(henv != SQL_NULL_HENV) {
      UCHAR sqlstate[SQL_SQLSTATE_SIZE+1];
      /*
       *  ODBC spec says ErrorMsg must not be greater than
       *  SQL_MAX_MESSAGE_LENGTH (says spec) but we concatenate a little
       *  on the end later (e.g. sql state) so make room for more.
       */
      UCHAR ErrorMsg[SQL_MAX_MESSAGE_LENGTH+512];
      SWORD ErrorMsgLen;
      SDWORD NativeError;
      RETCODE rc = 0;

      /* TBD: 3.0 update */
      while(SQL_SUCCEEDED(rc=SQLError(
                              henv, hdbc, hstmt,
                              sqlstate, &NativeError,
                              ErrorMsg, sizeof(ErrorMsg)-1, &ErrorMsgLen))) {

         ErrorMsg[ErrorMsgLen] = '\0';
         sqlstate[SQL_SQLSTATE_SIZE] = '\0';

         if (DBIc_TRACE(imp_dbh, 0, 0, 3)) {
             PerlIO_printf(DBIc_LOGPIO(imp_dbh),
                           "    !SQLError(%p,%p,%p) = "
                           "(%s, %ld, %s)\n",
                           henv, hdbc, hstmt, sqlstate, NativeError, ErrorMsg);
         }

         /*
	  * If there's an error handler, run it and see what it returns...
	  * (lifted from DBD:Sybase 0.21)
	  */
	 if(imp_dbh->odbc_err_handler) {
	    dSP;
	    /* SV *sv, **svp; */
	    /* HV *hv; */
	    int retval, count;

	    ENTER;
	    SAVETMPS;
	    PUSHMARK(sp);

	    if (DBIc_TRACE(imp_dbh, 0, 0, 3))
                TRACE0(imp_dbh, "    Calling error handler\n");

	    /*
	     * Here are the args to the error handler routine:
	     *    1. sqlstate (string)
	     *    2. ErrorMsg (string)
	     *    3. NativeError (integer)
	     * That's it for now...
	     */
	    XPUSHs(sv_2mortal(newSVpv(sqlstate, 0)));
	    XPUSHs(sv_2mortal(newSVpv(ErrorMsg, 0)));
	    XPUSHs(sv_2mortal(newSViv(NativeError)));

	    PUTBACK;
	    if((count = perl_call_sv(imp_dbh->odbc_err_handler, G_SCALAR)) != 1)
	       croak("An error handler can't return a LIST.");
	    SPAGAIN;
	    retval = POPi;

	    PUTBACK;
	    FREETMPS;
	    LEAVE;

	    /* If the called sub returns 0 then ignore this error */
	    if(retval == 0) {
                if (DBIc_TRACE(imp_dbh, 0, 0, 3))
                    TRACE0(imp_dbh,
                           "    Handler caused error to be ignored\n");
                continue;
            }
	 }
	 strcat(ErrorMsg, " (SQL-");
	 strcat(ErrorMsg, sqlstate);
	 strcat(ErrorMsg, ")");
	 /* maybe bad way to add hint about invalid transaction
	  * state upon disconnect...
	  */
	 if (what && !strcmp(sqlstate, "25000") &&
             !strcmp(what, "db_disconnect/SQLDisconnect")) {
             strcat(ErrorMsg, " You need to commit before disconnecting! ");
	 }

         if (SQL_SUCCEEDED(err_rc)) {
             DBIh_SET_ERR_CHAR(h, imp_xxh, "", 1, ErrorMsg, sqlstate, Nullch);
         } else {
             DBIh_SET_ERR_CHAR(h, imp_xxh, Nullch, 1, ErrorMsg,
                               sqlstate, Nullch);
         }
         continue;
      }
      if (rc != SQL_NO_DATA_FOUND) {	/* should never happen */
          if (DBIc_TRACE(imp_xxh, 0, 0, 3))
              TRACE1(imp_dbh,
                  "    !!SQLError returned %d unexpectedly.\n", rc);
          DBIh_SET_ERR_CHAR(
              h, imp_xxh, Nullch, 1,
              "Unable to fetch information about the error", "IM008", Nullch);
      }
      /* climb up the tree each time round the loop		*/
      if      (hstmt != SQL_NULL_HSTMT) hstmt = SQL_NULL_HSTMT;
      else if (hdbc  != SQL_NULL_HDBC)  hdbc  = SQL_NULL_HDBC;
      else henv = SQL_NULL_HENV;	/* done the top		*/
   }
}



/*------------------------------------------------------------
empties entire ODBC error queue.
------------------------------------------------------------*/
void dbd_error(SV *h, RETCODE err_rc, char *what)
{
   D_imp_xxh(h);
   dTHR;

   struct imp_dbh_st *imp_dbh = NULL;
   struct imp_sth_st *imp_sth = NULL;
   HSTMT hstmt = SQL_NULL_HSTMT;

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
   /*
    * If status is SQL_SUCCESS, there's no error, so we can just return.
    * There may be status or other non-error messsages though.
    * We want those messages if the debug level is set to at least 3.
    * If an error handler is installed, let it decide what messages
    * should or shouldn't be reported.
    */
   if ((err_rc == SQL_SUCCESS) && DBIc_TRACE(imp_dbh, 0, 0, 3) &&
       !imp_dbh->odbc_err_handler)
       return;

   dbd_error2(h, err_rc, what, imp_dbh->henv, imp_dbh->hdbc, hstmt);
}




void dbd_caution(SV *h, char *what)
{
   D_imp_xxh(h);
   dTHR;

   SV *errstr;
   errstr = DBIc_ERRSTR(imp_xxh);
   sv_setpvn(errstr, "", 0);
   sv_setiv(DBIc_ERR(imp_xxh), (IV)-1);
   /* sqlstate isn't set for SQL_NO_DATA returns  */
   sv_setpvn(DBIc_STATE(imp_xxh), "00000", 5);

   if (what) {
      sv_catpv(errstr, "(DBD: ");
      sv_catpv(errstr, what);
      sv_catpv(errstr, " err=-1)");
   }

   if (DBIc_TRACE(imp_xxh, 0, 0, 3))
      PerlIO_printf(DBIc_LOGPIO(imp_xxh), "    !!%s error %d recorded: %s\n",
		    what, -1, SvPV(errstr,na));
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
void dbd_preparse(imp_sth_t *imp_sth, char *statement)
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
   char literal_ch = '\0';

   /* allocate room for copy of statement with spare capacity	*/
   imp_sth->statement = (char*)safemalloc(strlen(statement)+1);

   /* initialize phs ready to be cloned per placeholder	*/
   memset(&phs_tpl, 0, sizeof(phs_tpl));
   phs_tpl.value_type = SQL_C_CHAR;
   phs_tpl.sv = &sv_undef;

   src  = statement;
   dest = imp_sth->statement;
   if (DBIc_TRACE(imp_sth, 0, 0, 5))
       TRACE1(imp_sth, "    ignore named placeholders = %d\n",
              imp_sth->odbc_ignore_named_placeholders);
   while(*src) {
      /*
       * JLU 10/6/2000 fixed to make the literal a " instead of '
       * JLU 1/28/2001 fixed to make literals either " or ', but deal
       * with ' "foo" ' or " foo's " correctly (just to be safe).
       *
       */
      if (*src == '"' || *src == '\'') {
	 if (!in_literal) {
	    literal_ch = *src;
	    in_literal = 1;
	 } else {
	    if (*src == literal_ch) {
	       in_literal = 0;
	    }
	 }
      }
      if ((*src != ':' && *src != '?') || in_literal) {
	 *dest++ = *src++;
	 continue;
      }
      start = dest;                         /* save name inc colon */
      ch = *src++;
      if (ch == '?') {                    /* X/Open standard */
	 idx++;
	 sprintf(name, "%d", idx);
	 *dest++ = ch;
	 style = 3;
      }
      else if (isDIGIT(*src)) {                 /* ':1' */
	 char *p = name;
	 *dest++ = '?';
	 idx = atoi(src);
	 while(isDIGIT(*src))
	    *p++ = *src++;
	 *p = 0;
	 style = 1;
	 if (DBIc_TRACE(imp_sth, 0, 0, 5))
             TRACE1(imp_sth, "    found numbered parameter = %s\n", name);
      }
      else if (!imp_sth->odbc_ignore_named_placeholders && isALNUM(*src)) {
	 /* ':foo' is valid, only if we are ignoring named
	  * parameters
	  */
	 char *p = name;
         idx++;
	 *dest++ = '?';

	 while(isALNUM(*src))	/* includes '_'	*/
	    *p++ = *src++;
	 *p = 0;
	 style = 2;
	 if (DBIc_TRACE(imp_sth, 0, 0, 5))
             TRACE1(imp_sth, "    found named parameter = %s\n", name);
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

      svpp = hv_fetch(imp_sth->all_params_hv, name, (I32)namelen, 0);
      if (svpp == NULL) {
          if (DBIc_TRACE(imp_sth, 0, 0, 5))
              TRACE1(imp_sth, "    creating new parameter key %s\n", name);
	 /* create SV holding the placeholder */
	 phs_sv = newSVpv((char*)&phs_tpl, sizeof(phs_tpl)+namelen+1);
	 phs = (phs_t*)SvPVX(phs_sv);
	 strcpy(phs->name, name);
	 phs->idx = idx;

	 /* store placeholder to all_params_hv */
	 svpp = hv_store(imp_sth->all_params_hv, name, (I32)namelen, phs_sv, 0);
      } else {
          if (DBIc_TRACE(imp_sth, 0, 0, 5))
              TRACE1(imp_sth, "    parameter key %s already exists\n", name);
         croak("DBD::ODBC does not yet support binding a named parameter more than once\n");
      }
   }
   *dest = '\0';
   if (imp_sth->all_params_hv) {
      DBIc_NUM_PARAMS(imp_sth) = (int)HvKEYS(imp_sth->all_params_hv);
      if (DBIc_TRACE(imp_sth, 0, 0, 4))
          TRACE1(imp_sth,
                 "    dbd_preparse scanned %d distinct placeholders\n",
                 (int)DBIc_NUM_PARAMS(imp_sth));
   }
}



int dbd_st_tables(
    SV *dbh,
    SV *sth,
    char *catalog,
    char *schema,
    char *table,
    char *table_type)
{
   D_imp_dbh(dbh);
   D_imp_sth(sth);
   RETCODE rc;
   int dbh_active;
   /* SV **svp; */
   /* char cname[128];	*/				/* cursorname */
   dTHR;

   imp_sth->henv = imp_dbh->henv;	/* needed for dbd_error */
   imp_sth->hdbc = imp_dbh->hdbc;

   imp_sth->done_desc = 0;

   if ((dbh_active = check_connection_active(dbh)) == 0) return 0;

   rc = SQLAllocHandle(SQL_HANDLE_STMT, imp_dbh->hdbc, &imp_sth->hstmt);
   if (rc != SQL_SUCCESS) {
      dbd_error(sth, rc, "st_tables/SQLAllocHandle(stmt)");
      return 0;
   }

   /* just for sanity, later.  Any internals that may rely on this (including */
   /* debugging) will have valid data */
   imp_sth->statement = (char *)safemalloc(strlen(cSqlTables)+
					   strlen(XXSAFECHAR(catalog)) +
					   strlen(XXSAFECHAR(schema)) +
					   strlen(XXSAFECHAR(table)) +
					   strlen(XXSAFECHAR(table_type))+1);
   sprintf(imp_sth->statement, cSqlTables, XXSAFECHAR(catalog),
	   XXSAFECHAR(schema), XXSAFECHAR(table), XXSAFECHAR(table_type));

   rc = SQLTables(imp_sth->hstmt,
		  (catalog && *catalog) ? catalog : 0, SQL_NTS,
		  (schema && *schema) ? schema : 0, SQL_NTS,
		  (table && *table) ? table : 0, SQL_NTS,
		  table_type && *table_type ? table_type : 0,
                  SQL_NTS		/* type (view, table, etc) */
		 );

   if (DBIc_TRACE(imp_sth, 0, 0, 4))
       TRACE2(imp_dbh, "   Tables result %d (%s)\n",
           rc, table_type ? table_type : "(null)");

   dbd_error(sth, rc, "st_tables/SQLTables");
   if (!SQL_SUCCEEDED(rc)) {
      SQLFreeHandle(SQL_HANDLE_STMT,imp_sth->hstmt);
      /* SQLFreeStmt(imp_sth->hstmt, SQL_DROP);*/ /* TBD: 3.0 update */
      imp_sth->hstmt = SQL_NULL_HSTMT;
      return 0;
   }
   return build_results(sth, dbh, rc);
}



int dbd_st_primary_keys(
    SV *dbh,
    SV *sth,
    char *catalog,
    char *schema,
    char *table)
{
   dTHR;
   D_imp_dbh(dbh);
   D_imp_sth(sth);
   RETCODE rc;
   int dbh_active;

   imp_sth->henv = imp_dbh->henv;	/* needed for dbd_error */
   imp_sth->hdbc = imp_dbh->hdbc;

   imp_sth->done_desc = 0;

   if ((dbh_active = check_connection_active(dbh)) == 0) return 0;

   rc = SQLAllocHandle(SQL_HANDLE_STMT, imp_dbh->hdbc, &imp_sth->hstmt);
   if (rc != SQL_SUCCESS) {
      dbd_error(sth, rc, "odbc_db_primary_key_info/SQLAllocHandle(stmt)");
      return 0;
   }

   /* just for sanity, later.  Any internals that may rely on this (including */
   /* debugging) will have valid data */
   imp_sth->statement = (char *)safemalloc(strlen(cSqlPrimaryKeys)+
					   strlen(XXSAFECHAR(catalog))+
					   strlen(XXSAFECHAR(schema))+
					   strlen(XXSAFECHAR(table))+1);

   sprintf(imp_sth->statement,
	   cSqlPrimaryKeys, XXSAFECHAR(catalog), XXSAFECHAR(schema),
	   XXSAFECHAR(table));

   rc = SQLPrimaryKeys(imp_sth->hstmt,
		       (catalog && *catalog) ? catalog : 0, SQL_NTS,
		       (schema && *schema) ? schema : 0, SQL_NTS,
		       (table && *table) ? table : 0, SQL_NTS);

   if (DBIc_TRACE(imp_sth, 0, 0, 4))
       PerlIO_printf(
           DBIc_LOGPIO(imp_dbh),
           "SQLPrimaryKeys call: cat = %s, schema = %s, table = %s\n",
           XXSAFECHAR(catalog), XXSAFECHAR(schema), XXSAFECHAR(table));

   dbd_error(sth, rc, "st_primary_key_info/SQLPrimaryKeys");

   if (!SQL_SUCCEEDED(rc)) {
      SQLFreeHandle(SQL_HANDLE_STMT,imp_sth->hstmt);
      /* SQLFreeStmt(imp_sth->hstmt, SQL_DROP);*/ /* TBD: 3.0 update */
      imp_sth->hstmt = SQL_NULL_HSTMT;
      return 0;
   }

   return build_results(sth, dbh, rc);
}



int dbd_st_statistics(
    SV *dbh,
    SV *sth,
    char *catalog,
    char *schema,
    char *table,
    int unique,
    int quick)
{
   dTHR;
   D_imp_dbh(dbh);
   D_imp_sth(sth);
   RETCODE rc;
   int dbh_active;
   SQLUSMALLINT odbc_unique;
   SQLUSMALLINT odbc_quick;

   imp_sth->henv = imp_dbh->henv;	/* needed for dbd_error */
   imp_sth->hdbc = imp_dbh->hdbc;

   imp_sth->done_desc = 0;

   if ((dbh_active = check_connection_active(dbh)) == 0) return 0;

   rc = SQLAllocHandle(SQL_HANDLE_STMT, imp_dbh->hdbc, &imp_sth->hstmt);
   if (rc != SQL_SUCCESS) {
      dbd_error(sth, rc, "odbc_db_primary_key_info/SQLAllocHandle(stmt)");
      return 0;
   }

   odbc_unique = (unique ? SQL_INDEX_UNIQUE : SQL_INDEX_ALL);
   odbc_quick = (quick ? SQL_QUICK : SQL_ENSURE);

   /* just for sanity, later.  Any internals that may rely on this (including */
   /* debugging) will have valid data */
   imp_sth->statement = (char *)safemalloc(strlen(cSqlStatistics)+
					   strlen(XXSAFECHAR(catalog))+
					   strlen(XXSAFECHAR(schema))+
					   strlen(XXSAFECHAR(table))+1);

   sprintf(imp_sth->statement,
	   cSqlStatistics, XXSAFECHAR(catalog), XXSAFECHAR(schema),
	   XXSAFECHAR(table), unique, quick);

   rc = SQLStatistics(imp_sth->hstmt,
                      (catalog && *catalog) ? catalog : 0, SQL_NTS,
                      (schema && *schema) ? schema : 0, SQL_NTS,
                      (table && *table) ? table : 0, SQL_NTS,
                      odbc_unique, odbc_quick);

   if (DBIc_TRACE(imp_sth, 0, 0, 4))
      PerlIO_printf(
          DBIc_LOGPIO(imp_dbh),
          "SQLStatistics call: cat = %s, schema = %s, table = %s"
          ", unique=%d, quick = %d\n",
          XXSAFECHAR(catalog), XXSAFECHAR(schema), XXSAFECHAR(table),
          odbc_unique, odbc_quick);

   dbd_error(sth, rc, "st_statistics/SQLStatistics");

   if (!SQL_SUCCEEDED(rc)) {
      SQLFreeHandle(SQL_HANDLE_STMT,imp_sth->hstmt);
      /* SQLFreeStmt(imp_sth->hstmt, SQL_DROP);*/ /* TBD: 3.0 update */
      imp_sth->hstmt = SQL_NULL_HSTMT;
      return 0;
   }

   return build_results(sth, dbh, rc);
}



/************************************************************************/
/*                                                                      */
/*  odbc_st_prepare                                                     */
/*  ===============                                                     */
/*                                                                      */
/*  dbd_st_prepare is the old API which is now replaced with            */
/*  dbd_st_prepare_sv (taking a perl scalar) so this is now:            */
/*                                                                      */
/*  a) just a wrapper around dbd_st_prepare_sv and                      */
/*  b) not used - see ODBC.c                                            */
/*                                                                      */
/************************************************************************/
int odbc_st_prepare(
   SV *sth,
   imp_sth_t *imp_sth,
   char *statement,
   SV *attribs)
{
   dTHR;
   SV *sql;

   sql = sv_newmortal();

   sv_setpvn(sql, statement, strlen(statement));

   return dbd_st_prepare_sv(sth, imp_sth, sql, attribs);

}



/************************************************************************/
/*                                                                      */
/*  odbc_st_prepare_sv                                                  */
/*  ==================                                                  */
/*                                                                      */
/*  dbd_st_prepare_sv is the newer version of dbd_st_prepare taking a   */
/*  a perl scalar for the sql statement instead of a char*.             */
/*                                                                      */
/************************************************************************/
int odbc_st_prepare_sv(
    SV *sth,
    imp_sth_t *imp_sth,
    SV *statement,
    SV *attribs)
{
   dTHR;
   D_imp_dbh_from_sth;
   RETCODE rc;
   int dbh_active;
   char *sql;
   STRLEN sql_len;

   sql = SvPV(statement, sql_len);

   imp_sth->done_desc = 0;
   imp_sth->henv = imp_dbh->henv;	/* needed for dbd_error */
   imp_sth->hdbc = imp_dbh->hdbc;
   imp_sth->odbc_ignore_named_placeholders =
       imp_dbh->odbc_ignore_named_placeholders;
   imp_sth->odbc_default_bind_type = imp_dbh->odbc_default_bind_type;
   imp_sth->odbc_force_rebind = imp_dbh->odbc_force_rebind;
   imp_sth->odbc_query_timeout = imp_dbh->odbc_query_timeout;
   imp_sth->odbc_putdata_start = imp_dbh->odbc_putdata_start;
   imp_sth->odbc_column_display_size = imp_dbh->odbc_column_display_size;
   if (DBIc_TRACE(imp_dbh, 0, 0, 5))
       TRACE1(imp_dbh, "    initializing sth query timeout to %d\n",
              (int)imp_dbh->odbc_query_timeout);

   if ((dbh_active = check_connection_active(sth)) == 0) return 0;

   rc = SQLAllocHandle(SQL_HANDLE_STMT, imp_dbh->hdbc, &imp_sth->hstmt);
   if (!SQL_SUCCEEDED(rc)) {
      dbd_error(sth, rc, "st_prepare/SQLAllocHandle(stmt)");
      return 0;
   }

   imp_sth->odbc_exec_direct = imp_dbh->odbc_exec_direct;

   {
      /*
       * allow setting of odbc_execdirect in prepare() or overriding
       */
      SV **odbc_exec_direct_sv;
      /* if the attribute is there, let it override what the default
       * value from the dbh is (set above).
       * NOTE:
       * There are unfortunately two possible attributes because of an early
       * typo in DBD::ODBC which we keep for backwards compatibility.
       */
      if ((odbc_exec_direct_sv =
           DBD_ATTRIB_GET_SVP(attribs, "odbc_execdirect",
                              (I32)strlen("odbc_execdirect"))) != NULL) {
	 imp_sth->odbc_exec_direct = SvIV(*odbc_exec_direct_sv) != 0;
      }
      if ((odbc_exec_direct_sv =
           DBD_ATTRIB_GET_SVP(attribs, "odbc_exec_direct",
                              (I32)strlen("odbc_exec_direct"))) != NULL) {
	 imp_sth->odbc_exec_direct = SvIV(*odbc_exec_direct_sv) != 0;
      }
   }
   /* scan statement for '?', ':1' and/or ':foo' style placeholders	*/
   dbd_preparse(imp_sth, sql);

   /* Hold this statement for subsequent call of dbd_execute */
   if (!imp_sth->odbc_exec_direct) {
       /* parse the (possibly edited) SQL statement */
       if (DBIc_TRACE(imp_dbh, SQL_TRACE_FLAG, 0, 3)) {
           TRACE1(imp_dbh, "    SQLPrepare %s\n", imp_sth->statement);
       }
#ifdef WITH_UNICODE
       if (SvOK(statement) && DO_UTF8(statement)) {
           SQLWCHAR *wsql;
           STRLEN wsql_len;
           SV *sql_copy;

           if (DBIc_TRACE(imp_dbh, 0x02000000, 0, 0))
               TRACE0(imp_dbh, "    Processing utf8 sql in unicode mode\n");

           sql_copy = sv_newmortal();
           sv_setpv(sql_copy, imp_sth->statement);
#ifdef sv_utf8_decode
           sv_utf8_decode(sql_copy);
#else
           SvUTF8_on(sql_copy);
#endif
           SV_toWCHAR(sql_copy);

           wsql = (SQLWCHAR *)SvPV(sql_copy, wsql_len);

           rc = SQLPrepareW(imp_sth->hstmt,
                            wsql, wsql_len / sizeof(SQLWCHAR));
       } else {
           if (DBIc_TRACE(imp_dbh, 0x02000000, 0, 0))
               TRACE0(imp_dbh, "    Processing non-utf8 sql in unicode mode\n");

           rc = SQLPrepare(imp_sth->hstmt, imp_sth->statement,
                           strlen(imp_sth->statement));
       }

#else
       if (DBIc_TRACE(imp_dbh, 0x02000000, 0, 0))
           TRACE0(imp_dbh, "    Processing sql in non-unicode mode\n");

       rc = SQLPrepare(imp_sth->hstmt, imp_sth->statement,
                       strlen(imp_sth->statement));
#endif
       if (DBIc_TRACE(imp_dbh, 0, 0, 3))
           TRACE1(imp_dbh, "    SQLPrepare = %d\n", rc);

       if (!SQL_SUCCEEDED(rc)) {
           dbd_error(sth, rc, "st_prepare/SQLPrepare");
           SQLFreeHandle(SQL_HANDLE_STMT,imp_sth->hstmt);
           /* SQLFreeStmt(imp_sth->hstmt, SQL_DROP);*/ /* TBD: 3.0 update */
           imp_sth->hstmt = SQL_NULL_HSTMT;
           return 0;
       }
   } else if (DBIc_TRACE(imp_dbh, 0, 0, 3)) {
       TRACE1(imp_dbh, "    odbc_exec_direct=1, statement (%s) "
              "held for later exec\n", imp_sth->statement);
   }


   /* init sth pointers */
   imp_sth->henv = imp_dbh->henv;
   imp_sth->hdbc = imp_dbh->hdbc;
   imp_sth->fbh = NULL;
   imp_sth->ColNames = NULL;
   imp_sth->RowBuffer = NULL;
   imp_sth->RowCount = -1;
   imp_sth->eod = -1;

   /*
    * If odbc_async_exec is set and odbc_async_type is SQL_AM_STATEMENT,
    * we need to set the SQL_ATTR_ASYNC_ENABLE attribute.
    */
   if (imp_dbh->odbc_async_exec &&
       imp_dbh->odbc_async_type == SQL_AM_STATEMENT){
      rc = SQLSetStmtAttr(imp_sth->hstmt,
			  SQL_ATTR_ASYNC_ENABLE,
			  (SQLPOINTER) SQL_ASYNC_ENABLE_ON,
			  SQL_IS_UINTEGER
			 );
      if (!SQL_SUCCEEDED(rc)) {
	 dbd_error(sth, rc, "st_prepare/SQLSetStmtAttr");
	 SQLFreeStmt(imp_sth->hstmt, SQL_DROP);
	 imp_sth->hstmt = SQL_NULL_HSTMT;
	 return 0;
      }
   }

   /*
    * If odbc_query_timeout is set (not -1)
    * we need to set the SQL_ATTR_QUERY_TIMEOUT
    */
   if (imp_sth->odbc_query_timeout != -1){
      odbc_set_query_timeout(sth, imp_sth->hstmt, imp_sth->odbc_query_timeout);
      if (!SQL_SUCCEEDED(rc)) {
	 dbd_error(sth, rc, "set_query_timeout");
      }
      /* don't fail if the query timeout can't be set. */
   }

   DBIc_IMPSET_on(imp_sth);
   return 1;
}



/* Given SQL type return string description - only used in debug output */
static const char *S_SqlTypeToString (SWORD sqltype)
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
#ifdef SQL_WCHAR
      case SQL_WCHAR: return "UNICODE CHAR";
#endif
#ifdef SQL_WVARCHAR
        /* added for SQLServer 7 ntext type 2/24/2000 */
      case SQL_WVARCHAR: return "UNICODE VARCHAR";
#endif
#ifdef SQL_WLONGVARCHAR
      case SQL_WLONGVARCHAR: return "UNICODE LONG VARCHAR";
#endif
      case SQL_DATE:	return "DATE";
      case SQL_TYPE_DATE:	return "DATE";
      case SQL_TIME:	return "TIME";
      case SQL_TYPE_TIME:	return "TIME";
      case SQL_TIMESTAMP:	return "TIMESTAMP";
      case SQL_TYPE_TIMESTAMP: return "TIMESTAMP";
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



static const char *S_SqlCTypeToString (SWORD sqltype)
{
   static char s_buf[100];
#define s_c(x) case x: return #x
   switch(sqltype) {
      s_c(SQL_C_CHAR);
      s_c(SQL_C_WCHAR);
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
      s_c(SQL_C_TYPE_DATE);
      s_c(SQL_C_TYPE_TIME);
      s_c(SQL_C_TYPE_TIMESTAMP);
   }
#undef s_c
   sprintf(s_buf, "(CType %d)", sqltype);
   return s_buf;
}




/*
 * describes the output variables of a query,
 * allocates buffers for result rows,
 * and binds this buffers to the statement.
 */
int dbd_describe(SV *h, imp_sth_t *imp_sth, int more)
{
    dTHR;
    SQLRETURN rc;                           /* ODBC fn return value */
    UCHAR *rbuf_ptr;
    SQLSMALLINT i;
    imp_fbh_t *fbh;
    SQLLEN t_dbsize = 0;                     /* size of native type */
    SQLSMALLINT num_fields;
    SQLCHAR *cur_col_name;
    struct imp_dbh_st *imp_dbh = NULL;
    imp_dbh = (struct imp_dbh_st *)(DBIc_PARENT_COM(imp_sth));

    if (DBIc_TRACE(imp_sth, 0, 0, 4))
        TRACE1(imp_sth, "    dbd_describe done_desc=%d\n", imp_sth->done_desc);

    if (imp_sth->done_desc)
        return 1;                       /* success, already done it */

    rc = SQLNumResultCols(imp_sth->hstmt, &num_fields);
    if (!SQL_SUCCEEDED(rc)) {
        dbd_error(h, rc, "dbd_describe/SQLNumResultCols");
        return 0;
    } else if (DBIc_TRACE(imp_sth, 0, 0, 4))
        TRACE1(imp_sth, "    dbd_describe SQLNumResultCols=0 (rows=%d)\n",
               num_fields);

    /*
     * A little extra check to see if SQLMoreResults is supported
     * before trying to call it.  This is to work around some strange
     * behavior with SQLServer's driver and stored procedures which
     * insert data.
     */
    imp_sth->done_desc = 1;	/* assume ok from here on */
    if (!more) {
        while (num_fields == 0 &&
               imp_dbh->odbc_sqlmoreresults_supported == 1) {
            rc = SQLMoreResults(imp_sth->hstmt);
            if (DBIc_TRACE(imp_sth, 0, 0, 8))
                TRACE1(imp_sth,
                       "    Numfields = 0, SQLMoreResults = %d\n", rc);
            if (rc == SQL_SUCCESS_WITH_INFO) {
                AllODBCErrors(imp_sth->henv, imp_sth->hdbc, imp_sth->hstmt,
                              DBIc_TRACE(imp_sth, 0, 0, 4),
                              DBIc_LOGPIO(imp_dbh));
            }
            if (rc == SQL_NO_DATA) {
                imp_sth->moreResults = 0;
                break;
            } else if (!SQL_SUCCEEDED(rc)) {
                break;
            }
            /* reset describe flags, so that we re-describe */
            imp_sth->done_desc = 0;

            /* force future executes to rebind automatically */
            imp_sth->odbc_force_rebind = 1;

            rc = SQLNumResultCols(imp_sth->hstmt, &num_fields);
            if (!SQL_SUCCEEDED(rc)) {
                dbd_error(h, rc, "dbd_describe/SQLNumResultCols");
                return 0;
            } else if (DBIc_TRACE(imp_sth, 0, 0, 8)) {
                TRACE1(imp_dbh, "    num fields after MoreResults = %d\n",
                       num_fields);
            }
        } /* end of SQLMoreResults */
    } /* end of more */

    DBIc_NUM_FIELDS(imp_sth) = num_fields;

    if (num_fields == 0) {
        if (DBIc_TRACE(imp_sth, 0, 0, 4))
            TRACE0(imp_dbh, "    dbd_describe skipped (no result cols)\n");
        imp_sth->done_desc = 1;
        return 1;
    }

    /* allocate field buffers */
    Newz(42, imp_sth->fbh, num_fields, imp_fbh_t);
    /* the +255 below instead is due to an old comment in this code before
       this change claiming foxpro wrote off end of memory
       (and so 255 bytes were added - kept here without evidence) */
    Newz(42, imp_sth->ColNames,
         (num_fields + 1) * imp_dbh->max_column_name_len + 255, UCHAR);

    cur_col_name = imp_sth->ColNames;
    /* Pass 1: Get space needed for field names, display buffer and dbuf */
    for (fbh=imp_sth->fbh, i=0; i < num_fields; i++, fbh++) {
        fbh->imp_sth = imp_sth;
#ifdef WITH_UNICODE
        rc = SQLDescribeColW(imp_sth->hstmt,
                            (SQLSMALLINT)(i+1),
                             (SQLWCHAR *) cur_col_name,
                            (SQLSMALLINT)imp_dbh->max_column_name_len,
                            &fbh->ColNameLen,
                            &fbh->ColSqlType,
                            &fbh->ColDef,
                            &fbh->ColScale,
                            &fbh->ColNullable);
        /*
        printf("strlen=%d, collen=%d\n", strlen(cur_col_name), fbh->ColNameLen);

        {

            int i;

            SQLWCHAR *wp = (SQLWCHAR *)cur_col_name;

            printf("SQLDescribeCol = ");

            for (i = 0; i < fbh->ColNameLen; i++) {
                printf("%ld, ", wp[i]);
            }
            printf("\n");
        }
        */

#else /* WITH_UNICODE */
        rc = SQLDescribeCol(imp_sth->hstmt,
                            (SQLSMALLINT)(i+1),
                            cur_col_name,
                            (SQLSMALLINT)imp_dbh->max_column_name_len,
                            &fbh->ColNameLen,
                            &fbh->ColSqlType,
                            &fbh->ColDef,
                            &fbh->ColScale,
                            &fbh->ColNullable);
#endif /* WITH_UNICODE */
        if (!SQL_SUCCEEDED(rc)) {	/* should never fail */
            dbd_error(h, rc, "describe/SQLDescribeCol");
            break;
        }
        fbh->ColName = cur_col_name;
#ifdef WITH_UNICODE
        cur_col_name += fbh->ColNameLen * sizeof(SQLWCHAR);
#else
        cur_col_name += fbh->ColNameLen + 1;
        cur_col_name[fbh->ColNameLen] = '\0';
#endif
#ifdef SQL_COLUMN_DISPLAY_SIZE
        if (DBIc_TRACE(imp_sth, 0, 0, 8))
            PerlIO_printf(DBIc_LOGPIO(imp_dbh),
                          "   DescribeCol column = %d, name = %s, "
                          "len = %d, type = %s(%d), "
                          "precision = %ld, scale = %d, nullable = %d\n",
                          i+1, fbh->ColName,
                          fbh->ColNameLen,
                          S_SqlTypeToString(fbh->ColSqlType),
                          fbh->ColSqlType,
                          fbh->ColDef, fbh->ColScale, fbh->ColNullable);
        rc = SQLColAttributes(imp_sth->hstmt,
                              (SQLSMALLINT)(i+1),SQL_COLUMN_DISPLAY_SIZE,
                              NULL, 0, NULL ,
                              &fbh->ColDisplaySize);/* TBD: 3.0 update */
        if (!SQL_SUCCEEDED(rc)) {
            /* Some ODBC drivers don't support SQL_COLUMN_DISPLAY_SIZE on
               some result-sets. e.g., The "Infor Integration ODBC driver"
               cannot handle SQL_COLUMN_DISPLAY_SIZE and SQL_COLUMN_LENGTH
               for SQLTables and SQLColumns calls. We used to fail here but
               there is a prescident not to as this code is already in an
               ifdef for drivers that do not define SQL_COLUMN_DISPLAY_SIZE.
               Since just about everyone will be using an ODBC driver manager
               now it is unlikely these attributes will not be defined so we
               default if the call fails now */
             if( DBIc_TRACE(imp_sth, 0, 0, 8) ) {
	       TRACE0(imp_sth,
		      "     describe/SQLColAttributes/SQL_COLUMN_DISPLAY_SIZE "
		      "not supported, will be equal to SQL_COLUMN_LENGTH\n");
             }
             /* ColDisplaySize will be made equal to ColLength */
             fbh->ColDisplaySize = 0;
             rc = SQL_SUCCESS;
        } else if (DBIc_TRACE(imp_sth, 0, 0, 8)) {
            TRACE1(imp_sth, "     display size = %ld\n", fbh->ColDisplaySize);
        }

        /* TBD: should we only add a terminator if it's a char??? */
        fbh->ColDisplaySize += 1; /* add terminator */
#else
        fbh->ColDisplaySize = imp_sth->odbc_column_display_size;
#endif

#ifdef SQL_COLUMN_LENGTH
        rc = SQLColAttributes(imp_sth->hstmt,(SQLSMALLINT)(i+1),
                              SQL_COLUMN_LENGTH,
                              NULL, 0, NULL ,&fbh->ColLength);
        if (!SQL_SUCCEEDED(rc)) {
            /* See comment above under SQL_COLUMN_DISPLAY_SIZE */
	    fbh->ColLength = imp_sth->odbc_column_display_size;
	    if( DBIc_TRACE(imp_sth, 0, 0, 8) ) {
	      TRACE1(imp_sth,
		     "     describe/SQLColAttributes/SQL_COLUMN_LENGTH not "
		     "supported, fallback on %d\n", fbh->ColLength);
	    }
	    rc = SQL_SUCCESS;
        } else if (DBIc_TRACE(imp_sth, 0, 0, 8)) {
            TRACE1(imp_sth, "     column length = %ld\n", fbh->ColLength);
        }
# if defined(WITH_UNICODE)
        fbh->ColLength += 1; /* add extra byte for double nul terminator */
# endif
#else
        fbh->ColLength = imp_sth->odbc_column_display_size;
#endif

        /* may want to ensure Display Size at least as large as column
         * length -- workaround for some drivers which report a shorter
         * display length
         * */
        fbh->ColDisplaySize =
            fbh->ColDisplaySize > fbh->ColLength ?
            fbh->ColDisplaySize : fbh->ColLength;

        /*
         * change fetched size, decimal digits etc for some types,
         * The tests for ColDef = 0 are for when the driver does not give
         * us a length for the column e.g., "max" column types in SQL Server
         * like varbinary(max).
         */
        fbh->ftype = SQL_C_CHAR;
        switch(fbh->ColSqlType)
        {
          case SQL_VARBINARY:
          case SQL_BINARY:
	    fbh->ftype = SQL_C_BINARY;
            if (fbh->ColDef == 0) {             /* cope with varbinary(max) */
                fbh->ColDisplaySize = DBIc_LongReadLen(imp_sth);
            }
	    break;
#if defined(WITH_UNICODE)
          case SQL_WCHAR:
          case SQL_WVARCHAR:
            fbh->ftype = SQL_C_WCHAR;
            /* MS SQL returns bytes, Oracle returns characters ... */

            if (fbh->ColDef == 0) {             /* cope with nvarchar(max) */
                fbh->ColDisplaySize = DBIc_LongReadLen(imp_sth);
                fbh->ColLength = DBIc_LongReadLen(imp_sth);
            }

            fbh->ColDisplaySize*=sizeof(SQLWCHAR);
            fbh->ColLength*=sizeof(SQLWCHAR);
            break;
#else
# if defined(SQL_WCHAR)
          case SQL_WCHAR:
            if (fbh->ColDef == 0) {
                fbh->ColDisplaySize = DBIc_LongReadLen(imp_sth);
            }
            break;
# endif
# if defined(SQL_WVARCHAR)
          case SQL_WVARCHAR:
            if (fbh->ColDef == 0) {
                fbh->ColDisplaySize = DBIc_LongReadLen(imp_sth);
            }
            break;
# endif
#endif /* WITH_UNICODE */
          case SQL_LONGVARBINARY:
	    fbh->ftype = SQL_C_BINARY;
	    fbh->ColDisplaySize = DBIc_LongReadLen(imp_sth);
	    break;
#ifdef SQL_WLONGVARCHAR
          case SQL_WLONGVARCHAR:	/* added for SQLServer 7 ntext type */
# if defined(WITH_UNICODE)
            fbh->ftype = SQL_C_WCHAR;
            /* MS SQL returns bytes, Oracle returns characters ... */
            fbh->ColLength*=sizeof(SQLWCHAR);
            fbh->ColDisplaySize = DBIc_LongReadLen(imp_sth)+1;
            break;
# endif	/* WITH_UNICODE */
#endif
          case SQL_VARCHAR:
            if (fbh->ColDef == 0) {
                fbh->ColDisplaySize = DBIc_LongReadLen(imp_sth)+1;
            }
            break;
          case SQL_LONGVARCHAR:
	    fbh->ColDisplaySize = DBIc_LongReadLen(imp_sth)+1;
	    break;
#ifdef TIMESTAMP_STRUCT	/* XXX! */
          case SQL_TIMESTAMP:
          case SQL_TYPE_TIMESTAMP:
	    fbh->ftype = SQL_C_TIMESTAMP;
	    fbh->ColDisplaySize = sizeof(TIMESTAMP_STRUCT);
	    break;
#endif
        }

        /* make sure alignment is accounted for on all types, including
         * chars */
#if 0
        if (fbh->ftype != SQL_C_CHAR) {
            t_dbsize += t_dbsize % sizeof(int); /* alignment (JLU incorrect!) */
        }
#endif
        t_dbsize += fbh->ColDisplaySize;
        /* alignment -- always pad so the next column is aligned on a
           word boundary */
        t_dbsize += (sizeof(int) - (t_dbsize % sizeof(int))) % sizeof(int);

        if (DBIc_TRACE(imp_sth, 0, 0, 4))
            PerlIO_printf(DBIc_LOGPIO(imp_dbh),
                          "     now using col %d: type = %s (%d), len = %d, "
                          "display size = %d, prec = %d, scale = %d\n",
                          i+1, S_SqlTypeToString(fbh->ColSqlType),
                          fbh->ColSqlType,
                          fbh->ColLength, fbh->ColDisplaySize,
                          fbh->ColDef, fbh->ColScale);
    }
    if (!SQL_SUCCEEDED(rc)) {
        /* dbd_error called above */
        Safefree(imp_sth->fbh);
        return 0;
    }

    /* allocate Row memory */
    Newz(42, imp_sth->RowBuffer, t_dbsize + num_fields, UCHAR);

    /* Second pass:
       - get column names
       - bind column output
    */

    rbuf_ptr = imp_sth->RowBuffer;

    for(i=0, fbh = imp_sth->fbh;
        i < num_fields && SQL_SUCCEEDED(rc); i++, fbh++)
    {
        /* not sure I need this anymore, since we are trying to align
         * the columns anyway
         * */
        switch(fbh->ftype)
        {
          case SQL_C_BINARY:
          case SQL_C_TIMESTAMP:
          case SQL_C_TYPE_TIMESTAMP:
	    /* make sure pointer is on word boundary for Solaris */
	    rbuf_ptr += (sizeof(int) -
                         ((rbuf_ptr - imp_sth->RowBuffer) % sizeof(int))) %
                sizeof(int);

	    break;
        }

        fbh->data = rbuf_ptr;
        rbuf_ptr += fbh->ColDisplaySize;
        /* alignment -- always pad so the next column is aligned on a word
           boundary */
        rbuf_ptr += (sizeof(int) - ((rbuf_ptr - imp_sth->RowBuffer) %
                                    sizeof(int))) % sizeof(int);

        /* Bind output column variables */
        rc = SQLBindCol(imp_sth->hstmt,
                        (SQLSMALLINT)(i+1),
                        fbh->ftype, fbh->data,
                        fbh->ColDisplaySize, &fbh->datalen);
        if (!SQL_SUCCEEDED(rc)) {
            dbd_error(h, rc, "describe/SQLBindCol");
            break;
        }
    } /* end pass 2 */

    if (!SQL_SUCCEEDED(rc)) {
        /* dbd_error called above */
        Safefree(imp_sth->fbh);
        return 0;
    }
    return 1;
}



/*======================================================================*/
/*                                                                      */
/* dbd_st_execute                                                       */
/* ==============                                                       */
/*                                                                      */
/* returns:                                                             */
/*   -2 - error                                                         */
/*   >=0 - ok, row count                                                */
/*   -1 - unknown count                                                 */
/*                                                                      */
/*======================================================================*/
int dbd_st_execute(
    SV *sth, imp_sth_t *imp_sth)
{
   dTHR;
   RETCODE rc;
   D_imp_dbh_from_sth;
   int outparams = 0;
   int ret;

   if (DBIc_TRACE(imp_sth, 0, 0, 3))
       TRACE1(imp_dbh, "+dbd_st_execute(%p)\n", sth);

   /*
    * if the handle is active, we need to finish it here.
    * Note that dbd_st_finish already checks to see if it's active.
    */
   dbd_st_finish(sth, imp_sth);;

   /*
    * bind_param_inout support
    */
   outparams = (imp_sth->out_params_av) ? AvFILL(imp_sth->out_params_av)+1 : 0;
   if (DBIc_TRACE(imp_sth, 0, 0, 4)) {
       TRACE1(imp_dbh, "    outparams = %d\n", outparams);
   }

   if (imp_dbh->odbc_defer_binding) {
      rc = SQLFreeStmt(imp_sth->hstmt, SQL_RESET_PARAMS);
      /* check bind input parameters */
      if (imp_sth->all_params_hv) {
	 HV *hv = imp_sth->all_params_hv;
	 SV *sv;
	 char *key;
	 I32 retlen;
	 hv_iterinit(hv);
	 while( (sv = hv_iternextsv(hv, &key, &retlen)) != NULL ) {
	    if (sv != &sv_undef) {
	       phs_t *phs = (phs_t*)(void*)SvPVX(sv);
	       if (!rebind_param(sth, imp_sth, phs)) return -2;
	       if (DBIc_TRACE(imp_sth, 0, 0, 8)) {
		  if (SvOK(phs->sv) && (phs->value_type == SQL_C_CHAR)) {
                      char sbuf[256];
                      unsigned int i = 0;

                      while((phs->sv_buf[i] != 0) && (i < (sizeof(sbuf) - 6))) {
                          sbuf[i] = phs->sv_buf[i];
                          i++;
                      }
                      strcpy(&sbuf[i], "...");

                      TRACE2(imp_dbh,
                             "    rebind check char Param %d (%s)\n",
                             phs->idx, sbuf);
		  }
	       }
	    }
	 }
      }
   }

   if (outparams) {    /* check validity of bind_param_inout SV's      */
      int i = outparams;
      while(--i >= 0) {
	 phs_t *phs = (phs_t*)(void*)SvPVX(AvARRAY(imp_sth->out_params_av)[i]);
	 /* Make sure we have the value in string format. Typically a number */
	 /* will be converted back into a string using the same bound buffer */
	 /* so the sv_buf test below will not trip.                   */

	 /* mutation check */
	 if (SvTYPE(phs->sv) != phs->sv_type /* has the type changed? */
	     || (SvOK(phs->sv) && !SvPOK(phs->sv)) /* is there still a string? */
	     || SvPVX(phs->sv) != phs->sv_buf /* has the string buffer moved? */
	      ) {
	    if (!rebind_param(sth, imp_sth, phs))
	       croak("Can't rebind placeholder %s", phs->name);
	 } else {
	    /* no mutation found */
	 }
      }
   }


   if (imp_sth->odbc_exec_direct) {
       /* statement ready for SQLExecDirect */
       if (DBIc_TRACE(imp_sth, 0, 0, 5)) {
           TRACE0(imp_dbh,
                  "    odbc_exec_direct=1, using SQLExecDirect\n");
       }
       rc = SQLExecDirect(imp_sth->hstmt, imp_sth->statement, SQL_NTS);
   } else {
      rc = SQLExecute(imp_sth->hstmt);
   }
   if (DBIc_TRACE(imp_sth, 0, 0, 8))
       TRACE2(imp_dbh, "    SQLExecute/SQLExecDirect(%p)=%d\n",
              imp_sth->hstmt, rc);
   /*
    * If asynchronous execution has been enabled, SQLExecute will
    * return SQL_STILL_EXECUTING until it has finished.
    * Grab whatever messages occur during execution...
    */
   while (rc == SQL_STILL_EXECUTING){
      dbd_error(sth, rc, "st_execute/SQLExecute");

      /*
       * Wait a second so we don't loop too fast and bring the machine
       * to its knees
       */
      if (DBIc_TRACE(imp_sth, 0, 0, 5))
          TRACE1(imp_dbh, "    SQLExecute(%p) still executing", imp_sth->hstmt);
      sleep(1);
      rc = SQLExecute(imp_sth->hstmt);
   }
   /* patches to handle blobs better, via Jochen Wiedmann */
   while (rc == SQL_NEED_DATA) {
      phs_t* phs;
      STRLEN len;
      UCHAR* ptr;

      if (DBIc_TRACE(imp_sth, 0, 0, 5))
          TRACE1(imp_dbh, "    NEED DATA\n", imp_sth->hstmt);

      while ((rc = SQLParamData(imp_sth->hstmt, (PTR*) &phs)) ==
              SQL_STILL_EXECUTING) {
          if (DBIc_TRACE(imp_sth, 0, 0, 5))
              TRACE1(imp_dbh, "    SQLParamData(%p) still executing",
                     imp_sth->hstmt);
          /*
           * wait a while to avoid looping too fast waiting for SQLParamData
           * to complete.
           */
          sleep(1);
      }
      if (rc !=  SQL_NEED_DATA) {
	 break;
      }

      /* phs->sv is already upgraded to a PV in rebind_param.
       * It is not NULL, because we otherwise won't be called here
       * (value_len = 0).
       */
      ptr = SvPV(phs->sv, len);
      rc = SQLPutData(imp_sth->hstmt, ptr, len);
      if (!SQL_SUCCEEDED(rc)) {
	 break;
      }
      rc = SQL_NEED_DATA;  /*  So the loop continues ...  */
   }

   /*
    * Call dbd_error regardless of the value of rc so we can
    * get any status messages that are desired.
    */
   dbd_error(sth, rc, "st_execute/SQLExecute");
   if (!SQL_SUCCEEDED(rc) && rc != SQL_NO_DATA) {
      /* dbd_error(sth, rc, "st_execute/SQLExecute"); */
       if (DBIc_TRACE(imp_sth, 0, 0, 3))
           TRACE1(imp_dbh, "-dbd_st_execute(%p)=-2\n", sth);
      return -2;
   }

   if (rc != SQL_NO_DATA) {

      /* SWORD num_fields; */
      RETCODE rc2;
      rc2 = SQLRowCount(imp_sth->hstmt, &imp_sth->RowCount);
      if (DBIc_TRACE(imp_sth, 0, 0, 7))
          TRACE2(imp_dbh, "    SQLRowCount=%d (rows=%d)\n",
                 rc2, (SQL_SUCCEEDED(rc2) ? imp_sth->RowCount : -1));
      if (!SQL_SUCCEEDED(rc2)) {
	 dbd_error(sth, rc2, "st_execute/SQLRowCount");	/* XXX ? */
	 imp_sth->RowCount = -1;
      }

      /* sanity check for strange circumstances and multiple types of
       * result sets.  Crazy that it can happen, but it can with
       * multiple result sets and stored procedures which return
       * result sets.
       * This seems to slow things down a bit and is rarely needed.
       *
       * This can happen in Sql Server in strange cases where stored
       * procs have multiple result sets.  Sometimes, if there is an
       * select then an insert, etc.  Maybe this should be a special
       * attribute to force a re-describe after every execute? */
      if (imp_sth->odbc_force_rebind) {
	 /* force calling dbd_describe after each execute */
	 odbc_clear_result_set(sth, imp_sth);
      }
   } else {
      /* SQL_NO_DATA returned, must have no rows :) */
      /* seem to need to reset the done_desc, but not sure if this is
       * what we want yet */
      if (DBIc_TRACE(imp_sth, 0, 0, 7))
          TRACE0(imp_dbh,
                 "    SQL_NO_DATA...resetting done_desc!\n");
      imp_sth->done_desc = 0;
      imp_sth->RowCount = 0;
   }

   /*
    *  MS SQL Server is very picky wrt to completing a procedure i.e.,
    *  it says the output bound parameters are not available until the procedure
    *  is complete and the procedure is not complete until you have called
    *  SQLMoreResults and it has returned SQL_NO_DATA. So, if you call a
    *  procedure multiple times in the same statement (e.g., by just calling
    *  execute) DBD::ODBC will call dbd_describe to describe the first
    *  execute, discover there is no result-set and call SQLMoreResults - ok,
    *  but after that, the dbd_describe is done and SQLMoreResults will not
    *  get called. The following is a klude to get around this until
    *  a) DBD::ODBC can be changed to stop skipping over non-result-set
    *  generating statements and b) the SQLMoreResults calls move out of
    *  dbd_describe.
    */
   {
       SQLSMALLINT flds = 0;
       SQLRETURN sts;

       sts = SQLNumResultCols(imp_sth->hstmt, &flds);
       if (flds == 0) {                         /* not a result-set */
           if (DBIc_TRACE(imp_sth, 0, 0, 4))
               TRACE2(imp_dbh, "    nflds=(%d,%d), resetting done_desc\n",
                      flds, DBIc_NUM_FIELDS(imp_sth));
           imp_sth->done_desc = 0;
       }
   }

   if (!imp_sth->done_desc) {
      /* This needs to be done after SQLExecute for some drivers!	*/
      /* Especially for order by and join queries.			*/
      /* See Microsoft Knowledge Base article (#Q124899)		*/
      /* describe and allocate storage for results (if any needed)	*/
      if (!dbd_describe(sth, imp_sth, 0)) {
          if (DBIc_TRACE(imp_sth, 0, 0, 3)) {
              TRACE0(imp_sth,
                     "    !!dbd_describe failed, dbd_st_execute #1...!\n");
          }
          if (DBIc_TRACE(imp_sth, 0, 0, 3))
              TRACE1(imp_dbh, "-dbd_st_execute(%p)=-2\n", sth);
          return -2; /* dbd_describe already called dbd_error()	*/
      }
   }

   if (DBIc_NUM_FIELDS(imp_sth) > 0) {
      DBIc_ACTIVE_on(imp_sth);	/* only set for select (?)	*/
      if (DBIc_TRACE(imp_sth, 0, 0, 4)) {
          TRACE1(imp_sth, "    have %d fields\n",
                 DBIc_NUM_FIELDS(imp_sth));
      }

   } else {
      if (DBIc_TRACE(imp_sth, 0, 0, 4)) {
          TRACE0(imp_dbh, "    got no rows: resetting ACTIVE, moreResults\n");
      }
      imp_sth->moreResults = 0;
      /* flag that we've done the describe to avoid a problem
       * where calling describe after execute returned no rows
       * caused SQLServer to provide a description of a query
       * that didn't quite apply. */

      /* imp_sth->done_desc = 1;  */
      DBIc_ACTIVE_off(imp_sth);
   }
   imp_sth->eod = SQL_SUCCESS;

   if (outparams) {	/* check validity of bound output SV's	*/
       odbc_handle_outparams(imp_sth, DBIc_TRACE_LEVEL(imp_sth));
   }

   /*
    * JLU: Jon Smirl had:
    *      return (imp_sth->RowCount == -1 ? -1 : abs(imp_sth->RowCount));
    * why?  Why do you need the abs() of the rowcount?  Special reason?
    * The e-mail that accompanied the change indicated that Sybase would return
    * a negative value for an estimate.  Wouldn't you WANT that to stay
    * negative?
    *
    * dgood: JLU had:
    *      return imp_sth->RowCount;
    * Because you return -2 on errors so if you don't abs() it, a perfectly
    * valid return value will get flagged as an error...
    */
   ret = (imp_sth->RowCount == -1 ? -1 : abs(imp_sth->RowCount));

   if (DBIc_TRACE(imp_sth, 0, 0, 3))
       TRACE2(imp_dbh, "-dbd_st_execute(%p)=%d\n", sth, ret);
   return ret;
   /* return imp_sth->RowCount; */
}




/*----------------------------------------
 * running $sth->fetch()
 *----------------------------------------
 */
AV *dbd_st_fetch(SV *sth, imp_sth_t *imp_sth)
{
   dTHR;
   D_imp_dbh_from_sth;
   int i;
   AV *av;
   RETCODE rc;
   int num_fields;
#ifdef TIMESTAMP_STRUCT /* iODBC doesn't define this */
   char cvbuf[512];
#endif
   int ChopBlanks;

   /* Check that execute() was executed sucessfully. This also implies	*/
   /* that dbd_describe() executed sucessfuly so the memory buffers	*/
   /* are allocated and bound.						*/
   if ( !DBIc_ACTIVE(imp_sth) ) {
      dbd_error(sth, SQL_ERROR, "no select statement currently executing");
      return Nullav;
   }

   rc = SQLFetch(imp_sth->hstmt);
   if (DBIc_TRACE(imp_sth, 0, 0, 4))
       TRACE1(imp_dbh, "    SQLFetch rc %d\n", rc);
   imp_sth->eod = rc;
   if (!SQL_SUCCEEDED(rc)) {
      if (SQL_NO_DATA_FOUND == rc) {

	 if (imp_dbh->odbc_sqlmoreresults_supported == 1) {
	    rc = SQLMoreResults(imp_sth->hstmt);
	    /* Check for multiple results */
	    if (DBIc_TRACE(imp_sth, 0, 0, 6))
                TRACE1(imp_dbh, "    Getting more results: %d\n", rc);

	    if (rc == SQL_SUCCESS_WITH_INFO) {
	       dbd_error(sth, rc, "st_fetch/SQLMoreResults");
	       /* imp_sth->moreResults = 0; */
	    }
	    if (SQL_SUCCEEDED(rc)){
	       /* More results detected.  Clear out the old result */
	       /* stuff and re-describe the fields.                */
                if (DBIc_TRACE(imp_sth, 0, 0, 3)) {
                    TRACE0(imp_dbh, "    MORE Results!\n");
	       }
	       odbc_clear_result_set(sth, imp_sth);

               /* force future executes to rebind automatically */
	       imp_sth->odbc_force_rebind = 1;

	       /* tell the odbc driver that we need to unbind the
		* bound columns.  Fix bug for 0.35 (2/8/02) */
	       rc = SQLFreeStmt(imp_sth->hstmt, SQL_UNBIND);
	       if (!SQL_SUCCEEDED(rc)) {
		  AllODBCErrors(imp_dbh->henv, imp_dbh->hdbc, 0,
				DBIc_TRACE(imp_sth, 0, 0, 3),
                                DBIc_LOGPIO(imp_dbh));
	       }

	       if (!dbd_describe(sth, imp_sth, 1)) {
                   if (DBIc_TRACE(imp_sth, 0, 0, 3))
                       TRACE0(imp_dbh,
                              "    !!MORE Results dbd_describe failed...!\n");
		  return Nullav; /* dbd_describe already called dbd_error() */
	       }


	       if (DBIc_TRACE(imp_sth, 0, 0, 4)) {
                   TRACE0(imp_dbh,
                          "    MORE Results dbd_describe success...!\n");
	       }
	       /* set moreResults so we'll know we can keep fetching */
	       imp_sth->moreResults = 1;
	       imp_sth->done_desc = 0;
	       return Nullav;
	    }
	    else if (rc == SQL_NO_DATA_FOUND || rc == SQL_NO_DATA ||
                     rc == SQL_SUCCESS_WITH_INFO){
	       /* No more results */
	       /* need to check output params here... */
	       int outparams = (imp_sth->out_params_av) ?
                   AvFILL(imp_sth->out_params_av)+1 : 0;

	       if (DBIc_TRACE(imp_sth, 0, 0, 6)) {
                   TRACE1(imp_sth, "    No more results -- outparams = %d\n",
                          outparams);
	       }
	       imp_sth->moreResults = 0;
	       imp_sth->done_desc = 1;
	       if (outparams) {
                   odbc_handle_outparams(imp_sth, DBIc_TRACE_LEVEL(imp_sth));
	       }
	       /* XXX need to 'finish' here */
	       dbd_st_finish(sth, imp_sth);
	       return Nullav;
	    }
	    else {
	       dbd_error(sth, rc, "st_fetch/SQLMoreResults");
	    }
	 }
	 else {
	    /*
	     * SQLMoreResults not supported, just finish.
	     * per bug found by Jarkko Hyöty [hyoty@medialab.sonera.fi]
	     * No more results
	     */
	    imp_sth->moreResults = 0;
	    /* XXX need to 'finish' here */
	    dbd_st_finish(sth, imp_sth);
	    return Nullav;
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

   av = DBIc_DBISTATE(imp_sth)->get_fbav(imp_sth);
   num_fields = AvFILL(av)+1;

   if (DBIc_TRACE(imp_sth, 0, 0, 4))
       TRACE1(imp_dbh, "    fetch num_fields=%d\n", num_fields);

   ChopBlanks = DBIc_has(imp_sth, DBIcf_ChopBlanks);

   for(i=0; i < num_fields; ++i) {
      imp_fbh_t *fbh = &imp_sth->fbh[i];
      SV *sv = AvARRAY(av)[i]; /* Note: we (re)use the SV in the AV	*/

      if (DBIc_TRACE(imp_sth, 0, 0, 4))
	 PerlIO_printf(
             DBIc_LOGPIO(imp_dbh), "    fetch col#%d %s datalen=%d displ=%d\n",
             i, fbh->ColName, fbh->datalen, fbh->ColDisplaySize);

      if (fbh->datalen == SQL_NULL_DATA) {	/* NULL value		*/
	 SvOK_off(sv);
	 continue;
      }

      if (fbh->datalen > fbh->ColDisplaySize || fbh->datalen < 0) {
	 /* truncated LONG ??? DBIcf_LongTruncOk() */
	 /* DBIcf_LongTruncOk this should only apply to LONG type fields */
	 /* truncation of other fields should always be an error since it's */
	 /* a sign of an internal error */
	 if (!DBIc_has(imp_sth, DBIcf_LongTruncOk)
	     /*  && rc == SQL_SUCCESS_WITH_INFO */) {

	    /*
	     * Since we've detected the problem locally via the datalen,
	     * we don't need to worry about the value of rc.
	     *
	     * This used to make sure rc was set to SQL_SUCCESS_WITH_INFO
	     * but since it's an error and not SUCCESS, call dbd_error()
	     * with SQL_ERROR explicitly instead.
	     */

	    dbd_error(
                sth, SQL_ERROR,
                "st_fetch/SQLFetch (long truncated DBI attribute LongTruncOk "
                "not set and/or LongReadLen too small)");
	    return Nullav;
	 }
	 /* LongTruncOk true, just ensure perl has the right length
	  * for the truncated data.
	  */
	 sv_setpvn(sv, (char*)fbh->data, fbh->ColDisplaySize);
      }
      else switch(fbh->ftype) {
#ifdef TIMESTAMP_STRUCT /* iODBC doesn't define this */
	 TIMESTAMP_STRUCT *ts;
	 case SQL_C_TIMESTAMP:
	 case SQL_C_TYPE_TIMESTAMP:
	    ts = (TIMESTAMP_STRUCT *)fbh->data;
	    sprintf(cvbuf, "%04d-%02d-%02d %02d:%02d:%02d",
		    ts->year, ts->month, ts->day,
		    ts->hour, ts->minute, ts->second, ts->fraction);
	    sv_setpv(sv, cvbuf);
	    break;
#endif
#if defined(WITH_UNICODE)
         case SQL_C_WCHAR:
	   if (ChopBlanks && fbh->ColSqlType == SQL_WCHAR && fbh->datalen > 0)
	   {
	     SQLWCHAR *p = (SQLWCHAR*)fbh->data;
	     while(fbh->datalen && p[fbh->datalen-1]==L' ') {
	       --fbh->datalen;
	     }
           }
	   sv_setwvn(sv,(SQLWCHAR*)fbh->data,fbh->datalen/sizeof(SQLWCHAR));
	   break;
#endif /* WITH_UNICODE */
	 default:
           if (ChopBlanks && fbh->ColSqlType == SQL_CHAR && fbh->datalen > 0)
           {
	       char *p = (char*)fbh->data;
	       while(fbh->datalen && p[fbh->datalen - 1]==' ')
                   --fbh->datalen;
           }
           sv_setpvn(sv, (char*)fbh->data, fbh->datalen);
      }
   }
   return av;
}




int dbd_st_rows(SV *sth, imp_sth_t *imp_sth)
{
   return (int)imp_sth->RowCount;
}




int dbd_st_finish(SV *sth, imp_sth_t *imp_sth)
{
   dTHR;
   D_imp_dbh_from_sth;
   RETCODE rc;

   if (DBIc_TRACE(imp_sth, 0, 0, 3))
       TRACE1(imp_sth, "    dbd_st_finish(%p)\n", sth);

   /* Cancel further fetches from this cursor.                 */
   /* We don't close the cursor till DESTROY (dbd_st_destroy). */
   /* The application may re execute(...) it.                  */

   /* XXX semantics of finish (eg oracle vs odbc) need lots more thought */
   /* re-read latest DBI specs and ODBC manuals */
   if (DBIc_ACTIVE(imp_sth) && imp_dbh->hdbc != SQL_NULL_HDBC) {

      rc = SQLFreeStmt(imp_sth->hstmt, SQL_CLOSE);/* TBD: 3.0 update */
      if (!SQL_SUCCEEDED(rc)) {
	 dbd_error(sth, rc, "finish/SQLFreeStmt(SQL_CLOSE)");
	 return 0;
      }
      if (DBIc_TRACE(imp_sth, 0, 0, 6)) {
	 TRACE0(imp_dbh, "    dbd_st_finish closed query:\n");
      }
   }
   DBIc_ACTIVE_off(imp_sth);
   return 1;
}



void dbd_st_destroy(SV *sth, imp_sth_t *imp_sth)
{
   dTHR;
   D_imp_dbh_from_sth;
   RETCODE rc;

   /* Free contents of imp_sth	*/

   /* PerlIO_printf(DBIc_LOGPIO(imp_dbh), "  dbd_st_destroy\n"); */
   Safefree(imp_sth->fbh);
   Safefree(imp_sth->RowBuffer);
   Safefree(imp_sth->ColNames);
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

   /* SQLxxx functions dump core when no connection exists. This happens
    * when the db was disconnected before perl ending.  Hence,
    * checking for the dirty flag.
    */
   if (imp_dbh->hdbc != SQL_NULL_HDBC && !dirty) {

      rc = SQLFreeHandle(SQL_HANDLE_STMT,imp_sth->hstmt);
      /* rc = SQLFreeStmt(imp_sth->hstmt, SQL_DROP);*/ /* TBD: 3.0 update */

      if (DBIc_TRACE(imp_sth, 0, 0, 5))
          TRACE1(imp_dbh, "   SQLFreeStmt=%d.\n", rc);

      if (!SQL_SUCCEEDED(rc)) {
	 dbd_error(sth, rc, "st_destroy/SQLFreeStmt(SQL_DROP)");
	 /* return 0; */
      }
   }

   DBIc_IMPSET_off(imp_sth);		/* let DBI know we've done it	*/
}


/************************************************************************/
/*                                                                      */
/*  get_param_type                                                      */
/*  ==============                                                      */
/*                                                                      */
/*  Sets the following fields for a parameter in the phs_st:            */
/*                                                                      */
/*  sql_type - the SQL type to use when binding this parameter          */
/*  describe_param_called - set to 1 if we called SQLDescribeParam      */
/*  describe_param_status - set to result of SQLDescribeParam if        */
/*                          SQLDescribeParam called                     */
/*  described_sql_type - the sql type returned by SQLDescribeParam      */
/*  param_size - the parameter size returned by SQLDescribeParam        */
/*                                                                      */
/*  The sql_type field is set to one of the following:                  */
/*    value passed in bind method call if specified                     */
/*    if SQLDescribeParam not supported:                                */
/*      value of odbc_default_bind_type attribute if set else           */
/*        SQL_VARCHAR                                                   */
/*    if SQLDescribeParam supported:                                    */
/*      if SQLDescribeParam succeeds:                                   */
/*        parameter type returned by SQLDescribeParam                   */
/*      else if SQLDescribeParam fails:                                 */
/*        value of odbc_default_bind_type attribute if set else         */
/*          SQL_VARCHAR                                                 */
/*                                                                      */
/*  NOTE: Just because an ODBC driver says it supports SQLDescribeParam */
/*  does not mean you can call it successfully e.g., MS SQL Server      */
/*  implements SQLDescribeParam by examining your SQL and rewriting it  */
/*  to be a select statement so it can find the column types etc. This  */
/*  fails horribly when the statement does not contain a table          */
/*  e.g., "select ?, LEN(?)" and so do most other SQL Server drivers.   */
/*                                                                      */
/************************************************************************/
static void get_param_type(SV *sth, imp_sth_t *imp_sth, phs_t *phs)
{
   SWORD fNullable;
   SWORD ibScale;
   D_imp_dbh_from_sth;
   RETCODE rc;

   if (DBIc_TRACE(imp_sth, 0, 0, 4))
       TRACE2(imp_sth, "    +get_param_type(%p,%s)\n", sth, phs->name);

   if (imp_dbh->odbc_sqldescribeparam_supported != 1) {
       /* As SQLDescribeParam is not supported by the ODBC driver we need to
          default a SQL type to bind the parameter as. The default is either
          the value set with odbc_default_bind_type or a fallback of
          SQL_VARCHAR. */
       phs->sql_type = default_parameter_type(imp_sth, phs);
       if (DBIc_TRACE(imp_sth, 0, 0, 4))
           TRACE1(imp_dbh, "      defaulted param type to %d\n", phs->sql_type);
   } else if (!phs->describe_param_called) {
       /* If we haven't had a go at calling SQLDescribeParam before for this
          parameter, have a go now. If it fails we'll default the sql type
          as above when driver does not have SQLDescribeParam */

       rc = SQLDescribeParam(imp_sth->hstmt,
                             phs->idx, &phs->described_sql_type,
                             &phs->param_size, &ibScale,
                             &fNullable);
       phs->describe_param_called = 1;
       phs->describe_param_status = rc;
       if (!SQL_SUCCEEDED(rc)) {
           phs->sql_type = default_parameter_type(imp_sth, phs);
           if (DBIc_TRACE(imp_sth, 0, 0, 3))
               TRACE1(imp_dbh, "      SQLDescribeParam failed reverting to "
                      "default SQL bind type %d\n", phs->sql_type);
           /* show any odbc errors in log */
           AllODBCErrors(imp_sth->henv, imp_sth->hdbc, imp_sth->hstmt,
                         DBIc_TRACE(imp_sth, 0, 0, 3),
                         DBIc_LOGPIO(imp_sth));
       } else {
           if (DBIc_TRACE(imp_sth, 0, 0, 5))
               PerlIO_printf(DBIc_LOGPIO(imp_dbh),
                             "      SQLDescribeParam %s: SqlType=%s(%d) "
                             "param_size=%d Scale=%d Nullable=%d\n",
                             phs->name,
                             S_SqlTypeToString(phs->described_sql_type),
                             phs->described_sql_type,
                             phs->param_size, ibScale, fNullable);

           /*
            * for non-integral numeric types, let the driver/database handle
            * the conversion for us
            */
           switch(phs->described_sql_type) {
             case SQL_NUMERIC:
             case SQL_DECIMAL:
             case SQL_FLOAT:
             case SQL_REAL:
             case SQL_DOUBLE:
               if (DBIc_TRACE(imp_sth, 0, 0, 5))
                   TRACE3(imp_dbh,
                          "      Param %s is numeric SQL type %s "
                          "(param size:%d) changed to SQL_VARCHAR",
                          phs->name,
                          S_SqlTypeToString(phs->described_sql_type),
                          phs->param_size);
               phs->sql_type = SQL_VARCHAR;
               break;
             default:
               phs->sql_type = phs->described_sql_type;
           }
       }
   } else if (phs->describe_param_called) {
       if (DBIc_TRACE(imp_sth, 0, 0, 5))
           TRACE1(imp_dbh,
                  "      SQLDescribeParam already run and returned rc=%d\n",
                  phs->describe_param_status);
   }

   if (phs->requested_type != 0) {
       phs->sql_type = phs->requested_type;
       if (DBIc_TRACE(imp_sth, 0, 0, 5))
           TRACE1(imp_dbh, "      Overriding sql type with requested type %d\n",
                  phs->requested_type);
   }

#if defined(WITH_UNICODE)
   /* for Unicode string types, change value_type to SQL_C_WCHAR*/
   switch (phs->sql_type) {
     case SQL_WCHAR:
     case SQL_WVARCHAR:
     case SQL_WLONGVARCHAR:
       phs->value_type = SQL_C_WCHAR;
       if (DBIc_TRACE(imp_sth, 0, 0, 8)) {
           TRACE0(imp_dbh,
                  "      get_param_type: modified value type to SQL_C_WCHAR\n");
       }
       break;
   }
#endif /* WITH_UNICODE */
   if (DBIc_TRACE(imp_sth, 0, 0, 8)) TRACE0(imp_dbh, "    -get_param_type\n");
}



/*======================================================================*/
/*                                                                      */
/* rebind_param                                                         */
/* ============                                                         */
/*                                                                      */
/*======================================================================*/
static int rebind_param(
    SV *sth,
    imp_sth_t *imp_sth,
    phs_t *phs)
{
   dTHR;
   D_imp_dbh_from_sth;
   SQLRETURN rc;
   /* args of SQLBindParameter() call */
   SQLSMALLINT param_type;
   SQLSMALLINT value_type;
   UCHAR *value_ptr;
   SQLULEN column_size;
   SQLSMALLINT d_digits;
   SQLLEN buffer_length;
   SQLULEN default_column_size;
   STRLEN value_len = 0;

   if (DBIc_TRACE(imp_sth, 0, 0, 4)) {
      char *text = neatsvpv(phs->sv,value_len);
      PerlIO_printf(
          DBIc_LOGPIO(imp_dbh),
          "    +rebind_param %s '%.100s' (size svCUR=%d/SvLEN=%d/max=%ld) "
          "svtype %ld, value type:%d sql type:%d\n",
          phs->name, text, SvOK(phs->sv) ? SvCUR(phs->sv) : -1,
          SvOK(phs->sv) ? SvLEN(phs->sv) : -1 ,phs->maxlen,
          SvTYPE(phs->sv), phs->value_type, phs->sql_type);
   }

   if (phs->is_inout) {
       /*
        * At the moment we always do sv_setsv() and rebind.
        * Later we may optimise this so that more often we can
        * just copy the value & length over and not rebind.
        */
       if (SvREADONLY(phs->sv))
           croak(no_modify);
       /* phs->sv _is_ the real live variable, it may 'mutate' later   */
       /* pre-upgrade high to reduce risk of SvPVX realloc/move        */
       (void)SvUPGRADE(phs->sv, SVt_PVNV);
       /* ensure room for result, 28 is magic number (see sv_2pv)      */
#if defined(WITH_UNICODE)
       SvGROW(phs->sv,
              (phs->maxlen+sizeof(SQLWCHAR) < 28) ?
              28 : phs->maxlen+sizeof(SQLWCHAR));
#else
       SvGROW(phs->sv, (phs->maxlen < 28) ? 28 : phs->maxlen+1);
#endif /* WITH_UNICODE */
   } else {
       /* phs->sv is copy of real variable, upgrade to at least string */
       (void)SvUPGRADE(phs->sv, SVt_PV);
   }

   /*
    * At this point phs->sv must be at least a PV with a valid buffer,
    * even if it's undef (null)
    * Here we set phs->sv_buf, and value_len.
    */
   if (SvOK(phs->sv)) {
      phs->sv_buf = SvPV(phs->sv, value_len);
   } else {
      /* it's undef but if it was inout param it would point to a
       * valid buffer, at least  */
      phs->sv_buf = SvPVX(phs->sv);
      value_len = 0;
   }

   get_param_type(sth, imp_sth, phs);

#if defined(WITH_UNICODE)
   if (phs->value_type == SQL_C_WCHAR) {
       if (DBIc_TRACE(imp_sth, 0, 0, 8)) {
           TRACE1(imp_dbh,
                  "      Need to modify phs->sv in place: old length = %i\n",
                  value_len);
       }
       SV_toWCHAR(phs->sv);        /* may modify SvPV(phs->sv), ... */
       /* ... so phs->sv_buf must be updated */
       phs->sv_buf=SvPV(phs->sv,value_len);
       if (DBIc_TRACE(imp_sth, 0, 0, 8)) {
           TRACE1(imp_dbh,
                  "      Need to modify phs->sv in place: new length = %i\n",
                  value_len);
       }
   }

   /* value_len has current value length now */
   phs->sv_type = SvTYPE(phs->sv);        /* part of mutation check */
   phs->maxlen  = SvLEN(phs->sv) - sizeof(SQLWCHAR); /* avail buffer space */

#else  /* !WITH_UNICODE */
   /* value_len has current value length now */
   phs->sv_type = SvTYPE(phs->sv);        /* part of mutation check */
   phs->maxlen  = SvLEN(phs->sv) - 1;         /* avail buffer space */
#endif /* WITH_UNICODE */

   if (DBIc_TRACE(imp_sth, 0, 0, 4)) {
       PerlIO_printf(
           DBIc_LOGPIO(imp_dbh),
           "      bind %s '%.100s' value_len=%d maxlen=%ld null=%d)\n",
           phs->name, SvOK(phs->sv) ? phs->sv_buf : "(null)",
           value_len,(long)phs->maxlen, SvOK(phs->sv) ? 0 : 1);
   }

   /*
    * JLU: was SQL_PARAM_OUTPUT only, but that caused a problem with
    * Oracle's drivers and in/out parameters.  Can't be output only
    * with Oracle.  Need to test on other platforms to ensure this
    * does not cause a problem.
    */
   param_type = phs->is_inout ? SQL_PARAM_INPUT_OUTPUT : SQL_PARAM_INPUT;
   value_type = phs->value_type;
   d_digits = value_len;
   column_size = phs->is_inout ? phs->maxlen : value_len;

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
   if (phs->sql_type == SQL_VARCHAR && !phs->is_inout) {
      d_digits = 0;
      /* default to at least 80 if this is the first time through */
      if (phs->biggestparam == 0) {
	 phs->biggestparam = (value_len > 80) ? value_len : 80;
      } else {
	 /* bump up max, if needed */
	 if (value_len > phs->biggestparam) {
	    phs->biggestparam = value_len;
	 }
      }
   }

   if ((phs->describe_param_called == 1) &&
       (SQL_SUCCEEDED(phs->describe_param_status))) {
       default_column_size = phs->param_size;
   } else {
       if ((phs->sql_type == SQL_VARCHAR) && !phs->is_inout) {
           default_column_size = phs->biggestparam;
       } else {
           default_column_size = value_len;
       }
   }

   /* JLU
    * need to look at this.
    * was buffer_length = value_len for some binding purposes
    */
   buffer_length = phs->is_inout ? phs->maxlen : value_len;

   /* When we fill a LONGVARBINARY, the CTYPE must be set to SQL_C_BINARY */
   if (value_type == SQL_C_CHAR) {	/* could be changed by bind_plh */
       d_digits = 0;                            /* not relevent to char types */
      switch(phs->sql_type) {
	 case SQL_LONGVARBINARY:
	 case SQL_BINARY:
	 case SQL_VARBINARY:
	    value_type = SQL_C_BINARY;
            column_size = default_column_size;
	    break;
#ifdef SQL_WLONGVARCHAR
	 case SQL_WLONGVARCHAR:	/* added for SQLServer 7 ntext type */
#endif
         case SQL_CHAR:
         case SQL_VARCHAR:
	 case SQL_LONGVARCHAR:
            column_size = default_column_size;
	    break;
	 case SQL_DATE:
	 case SQL_TYPE_DATE:
	 case SQL_TIME:
	 case SQL_TYPE_TIME:
	    break;
	 case SQL_TIMESTAMP:
	 case SQL_TYPE_TIMESTAMP:
	    d_digits = 0;		/* tbd: millisecondS?) */
	    if (SvOK(phs->sv)) {
	       char *cp;
	       if (phs->sv_buf && *phs->sv_buf) {
		  cp = strchr(phs->sv_buf, '.');
		  if (cp) {
		     ++cp;
		     while (*cp != '\0' && isdigit(*cp)) {
			cp++;
			d_digits++;
		     }
		  }
	       } else {
                   /* hard code for SQL Server when passing Undef to
                      bound parameters */
		  column_size = 23;
	       }
	    }
	    break;
	 default:
	    break;
      }
   } else if ( value_type = SQL_C_WCHAR) {
   		d_digits = 0;
  }

   if (!SvOK(phs->sv)) {
       phs->strlen_or_ind = SQL_NULL_DATA;
      /* if is_inout, shouldn't we null terminate the buffer and send
       * it, instead?? */
      if (!phs->is_inout) {
	 column_size = 1;
      }
      if (phs->is_inout) {
	 if (!phs->sv_buf) {
	    croak("panic: DBD::ODBC binding undef with bad buffer!!!!");
	 }
         /* just in case, we *know* we called SvGROW above */
	 phs->sv_buf[0] = '\0';
	 /* patch for binding undef inout params on sql server */
	 d_digits = 1;
         value_ptr = phs->sv_buf;
      } else {
	 value_ptr = NULL;
      }
   }
   else {
      value_ptr = phs->sv_buf;
      phs->strlen_or_ind = value_len;
      /* not undef, may be a blank string or something */
      if (!phs->is_inout && phs->strlen_or_ind == 0) {
          column_size = 1;
      }
   }
   if (DBIc_TRACE(imp_sth, 0, 0, 4)) {
      PerlIO_printf(DBIc_LOGPIO(imp_dbh),
		    "      bind %s value_type:%d %s cs=%d dd=%d bl=%ld\n",
		    phs->name, value_type, S_SqlTypeToString(phs->sql_type),
		    column_size, d_digits, buffer_length);
   }

   if (value_len < imp_sth->odbc_putdata_start) {
      /* already set and should be left alone JLU */
      /* d_digits = value_len; */
   } else {
      SQLLEN vl = value_len;

      if (DBIc_TRACE(imp_sth, 0, 0, 4))
          TRACE1(imp_dbh, "      using data_at_exec for size %ld\n", value_len);

      d_digits = 0;                         /* not relevant to lobs */
      phs->strlen_or_ind = SQL_LEN_DATA_AT_EXEC(vl);
      value_ptr = (UCHAR*) phs;
   }


#if THE_FOLLOWING_CODE_IS_FLAWED_AND_BROKEN
      /*
       * value_ptr is not null terminated - it is a byte array so PVallocW
       * won't work as it works on null terminated strings
       */
#if defined(WITH_UNICODE)
      if (value_type==SQL_C_WCHAR) {
          char * c1;
          c1 = PVallocW((SQLWCHAR *)value_ptr);
          TRACE1(imp_dbh, "      Param value = L'%s'\n", c1);
          PVfreeW(c1);
      }
#endif /* WITH_UNICODE */
#endif


   /*
    *  The following code is a workaround for a problem in SQL Server
    *  when inserting more than 400K into varbinary(max) or varchar(max)
    *  columns. The older SQL Server driver (not the native client driver):
    *
    *  o reports the size of xxx(max) columns as 2147483647 bytes in size
    *    when in reality they can be a lot bigger than that.
    *  o if you bind more than 400K you get the following errors:
    *    (HY000, 0, [Microsoft][ODBC SQL Server Driver]
    *      Warning: Partial insert/update. The insert/update of a text or
    *      image column(s) did not succeed.)
    *    (42000, 7125, [Microsoft][ODBC SQL Server Driver][SQL Server]
    *      The text, ntext, or image pointer value conflicts with the column
    *      name specified.)
    *
    *  There appear to be 2 workarounds but I was not prepared to do the first.
    *  The first is simply to set the indicator to SQL_LEN_DATA_AT_EXEC(409600)
    *  if the parameter was larger than 409600 - miraculously it works but
    *  shouldn't according to MSDN.
    *  The second workaround (used here) is to set the indicator to
    *  SQL_LEN_DATA_AT_EXEC(0) and the buffer_length to 0.
    *
    */
   if ((strcmp(imp_dbh->odbc_driver_name, "SQLSRV32.DLL") == 0) &&
       ((phs->sql_type == SQL_LONGVARCHAR) || (phs->sql_type == SQL_LONGVARBINARY)) &&
       (column_size == 2147483647) && (phs->strlen_or_ind < 0) &&
       ((-phs->strlen_or_ind + SQL_LEN_DATA_AT_EXEC_OFFSET) >= 409600)) {
       phs->strlen_or_ind = SQL_LEN_DATA_AT_EXEC(0);
       buffer_length = 0;
   }
#if defined(WITH_UNICODE)
   /*
    * rt43384 - MS Access does not seem to like us binding parameters as
    * wide characters and then SQLBindParameter column_size to byte length.
    * e.g., if you have a text(255) column and try and insert 190 ascii chrs
    * then the unicode enabled version of DBD::ODBC will convert those 190
    * ascii chrs to wide chrs and hence double the size to 380. If you pass
    * 380 to Access for column_size it just returns an invalid precision
    * value. This changes to column_size to chrs instead of bytes but
    * only if column_size is not reduced to 0 - which also produces
    * an access error e.g., in the empty string '' case.
    */
    else if ((strcmp(imp_dbh->odbc_driver_name, "odbcjt32.dll") == 0) &&
             (value_type == SQL_C_WCHAR) && (column_size > 1)) {
        column_size = column_size / 2;
        if (DBIc_TRACE(imp_sth, 0, 0, 4))
            TRACE0(imp_dbh, "    MSAccess - setting chrs not bytes\n");
   }
#endif

   /*
    * workaround bug in SQL Server ODBC driver where it can describe some
    * parameters (especially in SQL using sub selects) the wrong way.
    * If this is a varchar then the column_size must be at least as big
    * as the buffer size but if SQL Server associated the wrong column with
    * our parameter it could get a totally different size. Without this
    * a varchar(10) column can be desribed as a varchar(n) where n is less
    * than 10 and this leads to data truncation errors - see rt 39841.
    */
   if (((strcmp(imp_dbh->odbc_driver_name, "SQLSRV32.DLL") == 0) ||
        (strcmp(imp_dbh->odbc_driver_name, "sqlncli10.dll") == 0) ||
        (strcmp(imp_dbh->odbc_driver_name, "SQLNCLI.DLL") == 0)) &&
       (phs->sql_type == SQL_VARCHAR) &&
       (column_size < buffer_length)) {
       column_size = buffer_length;
   }
   /*
    * Yet another workaround for SQL Server native client.
    * If you have a varbinary(max) or varchar(max) you have to pass 0
    * for the column_size or you get HY104 "Invalid precision value".
    * See rt_38977.t which causes this.
    * The versions of native client I've seen this with are:
    * 2007.100.1600.22 sqlncli10.dll driver version = ?
    * 2005.90.1399.00 SQLNCLI.DLL driver version = 09.00.1399
    */
   if (((strcmp(imp_dbh->odbc_driver_name, "sqlncli10.dll") == 0) ||
        ((strcmp(imp_dbh->odbc_driver_name, "SQLNCLI.DLL") == 0))) &&
       (phs->strlen_or_ind < 0) &&
       (phs->param_size == 0)) {
       column_size = 0;
   } 
   if (DBIc_TRACE(imp_sth, 0, 0, 5)) {
      PerlIO_printf(
          DBIc_LOGPIO(imp_dbh),
          "    SQLBindParameter: idx=%d: param_type=%d, name=%s, "
          "value_type=%d, SQL_Type=%d, column_size=%d, d_digits=%d, "
          "value_ptr=%p, buffer_length=%d, cbValue=%d, param_size=%d\n",
          phs->idx, param_type, phs->name, value_type, phs->sql_type,
          column_size, d_digits, value_ptr, buffer_length, phs->strlen_or_ind,
          phs->param_size);
      /* avoid tracing data_at_exec as value_ptr will point to phs */
      if ((value_type == SQL_C_CHAR) && (phs->strlen_or_ind > 0)) {
          TRACE1(imp_sth, "      Param value = %s\n", value_ptr);
      }
   }
   rc = SQLBindParameter(imp_sth->hstmt,
			 phs->idx, param_type, value_type, phs->sql_type,
			 column_size, d_digits,
			 value_ptr, buffer_length, &phs->strlen_or_ind);

   if (!SQL_SUCCEEDED(rc)) {
      dbd_error(sth, rc, "rebind_param/SQLBindParameter");
      return 0;
   }

   if (DBIc_TRACE(imp_sth, 0, 0, 4)) TRACE0(imp_dbh, "    -rebind_param\n");

   return 1;
}



/*------------------------------------------------------------
 * bind placeholder.
 *  Is called from ODBC.xs execute()
 *  AND from ODBC.xs bind_param()
 */
int dbd_bind_ph(
    SV *sth,
    imp_sth_t *imp_sth,
    SV *ph_namesv,
    SV *newvalue,
    IV sql_type,
    SV *attribs,
    int is_inout,
    IV maxlen)
{
   dTHR;
   SV **phs_svp;
   STRLEN name_len;
   char *name;
   char namebuf[30];
   phs_t *phs;
   D_imp_dbh_from_sth;

   if (SvNIOK(ph_namesv) ) {                /* passed as a number */
      name = namebuf;
      sprintf(name, "%d", (int)SvIV(ph_namesv));
      name_len = strlen(name);
   }
   else {
      name = SvPV(ph_namesv, name_len);
   }
   if (DBIc_TRACE(imp_sth, 0, 0, 4)) {
       PerlIO_printf(
           DBIc_LOGPIO(imp_dbh),
           "+dbd_bind_ph(%p, %s, value='%.200s', attribs=%s, sql_type=%ld, is_inout=%d, maxlen=%ld\n",
           sth, name, SvOK(newvalue) ? SvPV_nolen(newvalue) : "undef",
           attribs ? SvPV_nolen(attribs) : "", sql_type, is_inout, maxlen);
   }

   /* the problem with the code below is we are getting SVt_PVLV when
    * an "undef" value from a hash lookup that doesn't exist.  It's an
    * "undef" value, but it doesn't come in as a scalar.
    * from a hash is arriving.  Let's leave this out until we are
    * handling arrays. JLU 7/12/02
    */
#if 0
   if (SvTYPE(newvalue) > SVt_PVMG) {    /* hook for later array logic   */
       if (DBIc_TRACE(imp_sth, 0, 0, 3))
           TRACE2(imp_dbh, "    !!bind %s perl type = %d -- croaking!\n",
                  name, SvTYPE(newvalue));
       croak("Can't bind non-scalar value (currently)");
   }
#endif

   /*
    * all_params_hv created during dbd_preparse.
    */
   phs_svp = hv_fetch(imp_sth->all_params_hv, name, (I32)name_len, 0);
   if (phs_svp == NULL)
      croak("Can't bind unknown placeholder '%s'", name);
   phs = (phs_t*)SvPVX(*phs_svp);	/* placeholder struct	*/

   phs->requested_type = sql_type;           /* save type requested */

   if (phs->sv == &sv_undef) { /* first bind for this placeholder */
      phs->value_type = SQL_C_CHAR;             /* default */

      phs->maxlen = maxlen;                     /* 0 if not inout */
      phs->is_inout = is_inout;
      if (is_inout) {
	 phs->sv = SvREFCNT_inc(newvalue);  /* point to live var */
	 ++imp_sth->has_inout_params;
	 /* build array of phs's so we can deal with out vars fast */
	 if (!imp_sth->out_params_av)
	    imp_sth->out_params_av = newAV();
	 av_push(imp_sth->out_params_av, SvREFCNT_inc(*phs_svp));
      }
   }
   /* check later rebinds for any changes */
   /*
    * else if (is_inout || phs->is_inout) {
    * croak("Can't rebind or change param %s in/out mode after first bind", phs->name);
    * }
    * */
   else if (is_inout != phs->is_inout) {
      croak("Can't rebind or change param %s in/out mode after first bind "
	    "(%d => %d)", phs->name, phs->is_inout, is_inout);
   }
   else if (maxlen && maxlen > phs->maxlen) {
       if (DBIc_TRACE(imp_sth, 0, 0, 4))
           PerlIO_printf(DBIc_LOGPIO(imp_dbh),
                         "!attempt to change param %s maxlen (%ld->$ld)\n",
                         phs->name, phs->maxlen, maxlen);
       croak("Can't change param %s maxlen (%ld->%ld) after first bind",
             phs->name, phs->maxlen, maxlen);
   }

   if (!is_inout) {    /* normal bind to take a (new) copy of current value */
      if (phs->sv == &sv_undef)       /* (first time bind) */
	 phs->sv = newSV(0);
      sv_setsv(phs->sv, newvalue);
   } else if (newvalue != phs->sv) {
      if (phs->sv)
	 SvREFCNT_dec(phs->sv);
      phs->sv = SvREFCNT_inc(newvalue);       /* point to live var */
   }

   if (imp_dbh->odbc_defer_binding) {
      get_param_type(sth, imp_sth, phs);

      if (DBIc_TRACE(imp_sth, 0, 0, 4)) TRACE0(imp_dbh, "-dbd_bind_ph=1\n");
      return 1;
   }
   /* fall through for "immediate" binding */

   if (DBIc_TRACE(imp_sth, 0, 0, 4))
       TRACE0(imp_dbh, "-dbd_bind_ph=rebind_param\n");
   return rebind_param(sth, imp_sth, phs);
}


/*------------------------------------------------------------
 * blob_read:
 * read part of a BLOB from a table.
 * XXX needs more thought
 */
int dbd_st_blob_read(sth, imp_sth, field, offset, len, destrv, destoffset)
SV *sth;
imp_sth_t *imp_sth;
int field;
long offset;
long len;
SV *destrv;
long destoffset;
{
   dTHR;
   SQLLEN retl;
   SV *bufsv;
   RETCODE rc;

   bufsv = SvRV(destrv);
   sv_setpvn(bufsv,"",0);      /* ensure it's writable string  */
   SvGROW(bufsv, len+destoffset+1);    /* SvGROW doesn't do +1 */

   /* XXX for this to work be probably need to avoid calling SQLGetData in
    * fetch. The definition of SQLGetData doesn't work well with the DBI's
    * notion of how LongReadLen would work. Needs more thought.	*/

   rc = SQLGetData(imp_sth->hstmt, (SQLSMALLINT)(field+1),
		   SQL_C_BINARY,
		   ((UCHAR *)SvPVX(bufsv)) + destoffset, len, &retl
		  );
   if (DBIc_TRACE(imp_sth, 0, 0, 4))
      PerlIO_printf(DBIc_LOGPIO(imp_sth),
		    "SQLGetData(...,off=%d, len=%d)->rc=%d,len=%d SvCUR=%d\n",
		    destoffset, len, rc, retl, SvCUR(bufsv));

   if (!SQL_SUCCEEDED(rc)) {
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

   if (DBIc_TRACE(imp_sth, 0, 0, 4))
       TRACE1(imp_sth, "    blob_read: SvCUR=%d\n", SvCUR(bufsv));

   return 1;
}



/*======================================================================*/
/*                                                                      */
/* S_db_storeOptions                                                    */
/* =================                                                    */
/* S_db_fetchOptions                                                    */
/* =================                                                    */
/*                                                                      */
/* An array of options/attributes we support on database handles for    */
/* storing and fetching.                                                */
/*                                                                      */
/*======================================================================*/
typedef struct {
   const char *str;
   UWORD fOption;
   UDWORD atrue;
   UDWORD afalse;
} db_params;

static db_params S_db_storeOptions[] =  {
   { "AutoCommit", SQL_AUTOCOMMIT, SQL_AUTOCOMMIT_ON, SQL_AUTOCOMMIT_OFF },
   { "ReadOnly", SQL_ATTR_ACCESS_MODE, SQL_MODE_READ_ONLY, SQL_MODE_READ_WRITE},
#if 0 /* not defined by DBI/DBD specification */
   { "TRANSACTION",
   SQL_ACCESS_MODE, SQL_MODE_READ_ONLY, SQL_MODE_READ_WRITE },
   { "solid_trace", SQL_OPT_TRACE, SQL_OPT_TRACE_ON, SQL_OPT_TRACE_OFF },
   { "solid_timeout", SQL_LOGIN_TIMEOUT },
   { "ISOLATION", SQL_TXN_ISOLATION },
   { "solid_tracefile", SQL_OPT_TRACEFILE },
#endif
   { "odbc_SQL_ROWSET_SIZE", SQL_ROWSET_SIZE },
   { "odbc_ignore_named_placeholders", ODBC_IGNORE_NAMED_PLACEHOLDERS },
   { "odbc_default_bind_type", ODBC_DEFAULT_BIND_TYPE },
   { "odbc_force_rebind", ODBC_FORCE_REBIND },
   { "odbc_async_exec", ODBC_ASYNC_EXEC },
   { "odbc_err_handler", ODBC_ERR_HANDLER },
   { "odbc_exec_direct", ODBC_EXEC_DIRECT },
   { "odbc_version", ODBC_VERSION },
   { "odbc_cursortype", ODBC_CURSORTYPE },
   { "odbc_query_timeout", ODBC_QUERY_TIMEOUT },
   { "odbc_putdata_start", ODBC_PUTDATA_START },
   { "odbc_column_display_size", ODBC_COLUMN_DISPLAY_SIZE },
   { NULL },
};

static db_params S_db_fetchOptions[] =  {
   { "AutoCommit", SQL_AUTOCOMMIT, SQL_AUTOCOMMIT_ON, SQL_AUTOCOMMIT_OFF },
   { "ReadOnly", SQL_ATTR_ACCESS_MODE, SQL_MODE_READ_ONLY, SQL_MODE_READ_WRITE},   { "RowCacheSize", ODBC_ROWCACHESIZE },
   { "odbc_SQL_ROWSET_SIZE", SQL_ROWSET_SIZE },
   { "odbc_SQL_DRIVER_ODBC_VER", SQL_DRIVER_ODBC_VER },
   { "odbc_ignore_named_placeholders", ODBC_IGNORE_NAMED_PLACEHOLDERS },
   { "odbc_default_bind_type", ODBC_DEFAULT_BIND_TYPE },
   { "odbc_force_rebind", ODBC_FORCE_REBIND },
   { "odbc_async_exec", ODBC_ASYNC_EXEC },
   { "odbc_err_handler", ODBC_ERR_HANDLER },
   { "odbc_SQL_DBMS_NAME", SQL_DBMS_NAME },
   { "odbc_exec_direct", ODBC_EXEC_DIRECT },
   { "odbc_query_timeout", ODBC_QUERY_TIMEOUT},
   { "odbc_putdata_start", ODBC_PUTDATA_START},
   { "odbc_column_display_size", ODBC_COLUMN_DISPLAY_SIZE},
   { "odbc_has_unicode", ODBC_HAS_UNICODE},
   { "odbc_out_connect_string", ODBC_OUTCON_STR},
   { NULL }
};

/*======================================================================*/
/*                                                                      */
/*  S_dbOption                                                          */
/*  ==========                                                          */
/*                                                                      */
/*  Given a string and a length, locate this option in the specified    */
/*  array of valid options. Typically used by STORE and FETCH methods   */
/*  to decide if this option/attribute is supported by us.              */
/*                                                                      */
/*======================================================================*/
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
   if (pars->str == NULL) {
      return NULL;
   }
   return pars;
}



/*======================================================================*/
/*                                                                      */
/* dbd_db_STORE_attrib                                                  */
/* ===================                                                  */
/*                                                                      */
/* This function handles:                                               */
/*                                                                      */
/*   $dbh->{$key} = $value                                              */
/*                                                                      */
/* Method to handle the setting of driver specific attributes and DBI   */
/* attributes AutoCommit and ChopBlanks (no other DBI attributes).      */
/*                                                                      */
/* Return TRUE if the attribute was handled, else FALSE.                */
/*                                                                      */
/*======================================================================*/
int dbd_db_STORE_attrib(SV *dbh, imp_dbh_t *imp_dbh, SV *keysv, SV *valuesv)
{
   dTHR;
   D_imp_drh_from_dbh;
   RETCODE rc;
   STRLEN kl;
   STRLEN plen;
   char *key = SvPV(keysv,kl);
   int on;
   UDWORD vParam;
   const db_params *pars;
   int bSetSQLConnectionOption;

   if ((pars = S_dbOption(S_db_storeOptions, key, kl)) == NULL) {
       if (DBIc_TRACE(imp_dbh, 0, 0, 3))
           TRACE1(imp_dbh,
                  "    !!DBD::ODBC unsupported attribute passed (%s)\n", key);

      return FALSE;
   }

   bSetSQLConnectionOption = TRUE;
   switch(pars->fOption)
   {
      case SQL_LOGIN_TIMEOUT:
      case SQL_TXN_ISOLATION:
      case SQL_ROWSET_SIZE:
	 vParam = SvIV(valuesv);
	 break;
      case SQL_OPT_TRACEFILE:
	 vParam = (UDWORD) SvPV(valuesv, plen);
	 break;

      case ODBC_IGNORE_NAMED_PLACEHOLDERS:
	 bSetSQLConnectionOption = FALSE;
	 /*
	  * set value to ignore placeholders.  Will affect all
	  * statements from here on.
	  */
	 imp_dbh->odbc_ignore_named_placeholders = SvTRUE(valuesv);
	 break;

      case ODBC_DEFAULT_BIND_TYPE:
	 bSetSQLConnectionOption = FALSE;
	 /*
	  * set value of default bind type.  Default is SQL_VARCHAR,
	  * but setting to 0 will cause SQLDescribeParam to be used.
	  */
	 imp_dbh->odbc_default_bind_type = (SQLSMALLINT)SvIV(valuesv);

	 break;

      case ODBC_FORCE_REBIND:
	 bSetSQLConnectionOption = FALSE;
	 /*
	  * set value to force rebind
	  */
	 imp_dbh->odbc_force_rebind = SvTRUE(valuesv);
	 break;

      case ODBC_QUERY_TIMEOUT:
	 bSetSQLConnectionOption = FALSE;
	 imp_dbh->odbc_query_timeout = (SQLINTEGER)SvIV(valuesv);
	 break;

      case ODBC_PUTDATA_START:
	 bSetSQLConnectionOption = FALSE;
	 imp_dbh->odbc_putdata_start = SvIV(valuesv);
	 break;

      case ODBC_COLUMN_DISPLAY_SIZE:
	 bSetSQLConnectionOption = FALSE;
	 imp_dbh->odbc_column_display_size = SvIV(valuesv);
	 break;

      case ODBC_EXEC_DIRECT:
	 bSetSQLConnectionOption = FALSE;
	 /*
	  * set value of odbc_exec_direct.  Non-zero will
	  * make prepare, essentially a noop and make execute
	  * use SQLExecDirect.  This is to support drivers that
	  * _only_ support SQLExecDirect.
	  */
	 imp_dbh->odbc_exec_direct = SvTRUE(valuesv);
	 break;

      case ODBC_ASYNC_EXEC:
	 bSetSQLConnectionOption = FALSE;
	 /*
	  * set asynchronous execution.  It can only be turned on if
	  * the driver supports it, but will fail silently.
	  */
	 if (SvTRUE(valuesv)) {
	    /* Only bother setting the attribute if it's not already set! */
	    if (imp_dbh->odbc_async_exec)
	       break;

	    /*
	     * Determine which method of async execution this
	     * driver allows -- per-connection or per-statement
	     */
	    rc = SQLGetInfo(imp_dbh->hdbc,
			    SQL_ASYNC_MODE,
			    &imp_dbh->odbc_async_type,
			    sizeof(imp_dbh->odbc_async_type),
			    NULL
			   );
	    /*
	     * Normally, we'd do a if (!SQL_ok(rc)) ... here.
	     * Unfortunately, if the driver doesn't support async
	     * mode, it may return an error here.  There doesn't
	     * seem to be any other way to check (other than doing
	     * a special check for the SQLSTATE).  We'll just default
	     * to doing nothing and not bother checking errors.
	     */

	    if (imp_dbh->odbc_async_type == SQL_AM_CONNECTION){
	       /*
		* Driver has per-connection async option.  Set it
		* now in the dbh.
		*/
                if (DBIc_TRACE(imp_dbh, 0, 0, 4))
                    TRACE0(imp_dbh,
                           "    Supported AsyncType is SQL_AM_CONNECTION\n");
	       rc = SQLSetConnectOption(imp_dbh->hdbc,
					SQL_ATTR_ASYNC_ENABLE,
					SQL_ASYNC_ENABLE_ON
				       );
	       if (!SQL_SUCCEEDED(rc)) {
		  dbd_error(dbh, rc, "db_STORE/SQLSetConnectOption");
		  return FALSE;
	       }
	       imp_dbh->odbc_async_exec = 1;
	    }
	    else if (imp_dbh->odbc_async_type == SQL_AM_STATEMENT){
	       /*
		* Driver has per-statement async option.  Just set
		* odbc_async_exec and the rest will be handled by
		* dbd_st_prepare.
		*/
                if (DBIc_TRACE(imp_dbh, 0, 0, 4))
                    TRACE0(imp_dbh,
                           "    Supported AsyncType is SQL_AM_STATEMENT\n");
	       imp_dbh->odbc_async_exec = 1;
	    }
	    else {   /* (imp_dbh->odbc_async_type == SQL_AM_NONE) */
	       /*
		* We're out of luck.
		*/
                if (DBIc_TRACE(imp_dbh, 0, 0, 4))
                    TRACE0(imp_dbh, "    Supported AsyncType is SQL_AM_NONE\n");
	       imp_dbh->odbc_async_exec = 0;
	       return FALSE;
	    }
	 } else {
	    /* Only bother turning it off if it was previously set... */
	    if (imp_dbh->odbc_async_exec == 1) {

	       /* We only need to do anything here if odbc_async_type is
		* SQL_AM_CONNECTION since the per-statement async type
		* is turned on only when the statement handle is created.
		*/
	       if (imp_dbh->odbc_async_type == SQL_AM_CONNECTION){
		  rc = SQLSetConnectOption(imp_dbh->hdbc,
					   SQL_ATTR_ASYNC_ENABLE,
					   SQL_ASYNC_ENABLE_OFF
					  );
		  if (!SQL_SUCCEEDED(rc)) {
		     dbd_error(dbh, rc, "db_STORE/SQLSetConnectOption");
		     return FALSE;
		  }
	       }
	    }
	    imp_dbh->odbc_async_exec = 0;
	 }
	 break;

      case ODBC_ERR_HANDLER:
	 bSetSQLConnectionOption = FALSE;

	 /* This was taken from DBD::Sybase 0.21 */
	 /* I believe the following if test which has been in DBD::ODBC
          * for ages is wrong and should (at least now) use SvOK or
          *  it is impossible to reset the error handler
          *
          *  if(valuesv == &PL_sv_undef) {
	  *  imp_dbh->odbc_err_handler = NULL;
          */
	 if (!SvOK(valuesv)) {
	    imp_dbh->odbc_err_handler = NULL;
         } else if(imp_dbh->odbc_err_handler == (SV*)NULL) {
	    imp_dbh->odbc_err_handler = newSVsv(valuesv);
	 } else {
	    sv_setsv(imp_dbh->odbc_err_handler, valuesv);
	 }
	 break;
      case ODBC_VERSION:
	 /* set only in connect, nothing to store */
	 bSetSQLConnectionOption = FALSE;
	 break;

      case ODBC_CURSORTYPE:
	 /* set only in connect, nothing to store */
	 bSetSQLConnectionOption = FALSE;
	 break;

       case SQL_ATTR_ACCESS_MODE:
         on = SvTRUE(valuesv);
	 vParam = on ? pars->atrue : pars->afalse;
         break;

      default:
	 on = SvTRUE(valuesv);
	 vParam = on ? pars->atrue : pars->afalse;
	 break;
   }

   if (bSetSQLConnectionOption) {
      /* TBD: 3.0 update */

      rc = SQLSetConnectOption(imp_dbh->hdbc, pars->fOption, vParam);

      if (!SQL_SUCCEEDED(rc)) {
	 dbd_error(dbh, rc, "db_STORE/SQLSetConnectOption");
	 return FALSE;
      }
      /* keep our flags in sync */
      if (kl == 10 && strEQ(key, "AutoCommit"))
	 DBIc_set(imp_dbh, DBIcf_AutoCommit, SvTRUE(valuesv));
   }
   return TRUE;
}



/*======================================================================*/
/*                                                                      */
/* dbd_db_FETCH_attrib                                                  */
/* ===================                                                  */
/*                                                                      */
/* Counterpart of dbd_db_STORE_attrib handing:                          */
/*                                                                      */
/*   $value = $dbh->{$key};                                             */
/*                                                                      */
/* returns an "SV" with the value                                       */
/*                                                                      */
/*======================================================================*/
SV *dbd_db_FETCH_attrib(SV *dbh, imp_dbh_t *imp_dbh, SV *keysv)
{
   dTHR;
   D_imp_drh_from_dbh;
   RETCODE rc;
   STRLEN kl;
   char *key = SvPV(keysv,kl);
   UDWORD vParam = 0;
   const db_params *pars;
   SV *retsv = Nullsv;

   /* checking pars we need FAST */

   if (DBIc_TRACE(imp_dbh, 0, 0, 8))
       TRACE1(imp_dbh, "    FETCH %s\n", key);


   if ((pars = S_dbOption(S_db_fetchOptions, key, kl)) == NULL)
      return Nullsv;

   switch (pars->fOption) {
     case ODBC_OUTCON_STR:
       if (!imp_dbh->out_connect_string) {
           retsv = &PL_sv_undef;
       } else {
           retsv = newSVsv(imp_dbh->out_connect_string);
       }
       break;

      case SQL_DRIVER_ODBC_VER:
        retsv = newSVpv(imp_dbh->odbc_ver, 0);
        break;

      case SQL_DBMS_NAME:
	 retsv = newSVpv(imp_dbh->odbc_dbname, 0);
	 break;

      case ODBC_IGNORE_NAMED_PLACEHOLDERS:
        retsv = newSViv(imp_dbh->odbc_ignore_named_placeholders);
        break;

      case ODBC_QUERY_TIMEOUT:
        /*
         * fetch current value of query timeout
         *
         * -1 is our internal flag saying odbc_query_timeout has never been
         * set so we map it back to the default for ODBC which is 0
         */
        if (imp_dbh->odbc_query_timeout == -1) {
            retsv = newSViv(0);
        } else {
            retsv = newSViv(imp_dbh->odbc_query_timeout);
        }
        break;

      case ODBC_PUTDATA_START:
        retsv = newSViv(imp_dbh->odbc_putdata_start);
        break;

      case ODBC_COLUMN_DISPLAY_SIZE:
        retsv = newSViv(imp_dbh->odbc_column_display_size);
        break;

      case ODBC_HAS_UNICODE:
        retsv = newSViv(imp_dbh->odbc_has_unicode);
        break;

      case ODBC_DEFAULT_BIND_TYPE:
        retsv = newSViv(imp_dbh->odbc_default_bind_type);
        break;

      case ODBC_FORCE_REBIND:
        retsv = newSViv(imp_dbh->odbc_force_rebind);
        break;

      case ODBC_EXEC_DIRECT:
        retsv = newSViv(imp_dbh->odbc_exec_direct);
        break;

      case ODBC_ASYNC_EXEC:
        /*
         * fetch current value of asynchronous execution (should be
         * either 0 or 1).
         */
        retsv = newSViv(imp_dbh->odbc_async_exec);
        break;

      case ODBC_ERR_HANDLER:
	 /* fetch current value of the error handler (a coderef). */
	 if(imp_dbh->odbc_err_handler) {
	    retsv = newSVsv(imp_dbh->odbc_err_handler);
	 } else {
	    retsv = &sv_undef;
	 }
	 break;

      case ODBC_ROWCACHESIZE:
	 retsv = newSViv(imp_dbh->RowCacheSize);
	 break;
      default:
	 /*
	  * The remainders we support are ODBC attributes like
          * AutoCommit (SQL_AUTOCOMMIT)
          * odbc_SQL_ROWSET_SIZE (SQL_ROWSET_SIZE)
          *
          * Nothing else should get here for now unless any item is added
          * to S_db_fetchOptions.
	  */

	 rc = SQLGetConnectOption(imp_dbh->hdbc, pars->fOption, &vParam);/* TBD: 3.0 update */
	 dbd_error(dbh, rc, "db_FETCH/SQLGetConnectOption");
	 if (!SQL_SUCCEEDED(rc)) {
             if (DBIc_TRACE(imp_dbh, 0, 0, 3))
                 TRACE1(imp_dbh,
                        "    !!SQLGetConnectOption=%d in dbd_db_FETCH\n", rc);
	    return Nullsv;
	 }

	 switch(pars->fOption) {
	    case SQL_ROWSET_SIZE:
	       retsv = newSViv(vParam);
	       break;
	    default:
	       if (vParam == pars->atrue)
		  retsv = newSViv(1);
	       else
		  retsv = newSViv(0);
	       break;
	 } /* inner switch */
   } /* outer switch */

   return sv_2mortal(retsv);
}



/*======================================================================*/
/*                                                                      */
/* S_st_fetch_params                                                    */
/* =================                                                    */
/* S_st_store_params                                                    */
/* =================                                                    */
/*                                                                      */
/* An array of options/attributes we support on statement handles for   */
/* storing and fetching.                                                */
/*                                                                      */
/*======================================================================*/

/*
 * added "need_describe" flag to handle the situation where you don't
 * have a result set yet to describe.  Certain attributes don't need
 * the result set to operate, hence don't do a describe unless you need
 * to do one.
 * DBD::ODBC 0.45_15
 * */
typedef struct {
   const char *str;
   unsigned len:8;
   unsigned array:1;
   unsigned need_describe:1;
   unsigned filler:22;
} T_st_params;

#define s_A(str,need_describe) { str, sizeof(str)-1,0,need_describe }
static T_st_params S_st_fetch_params[] =
{
   s_A("NUM_OF_PARAMS",1),	/* 0 */
   s_A("NUM_OF_FIELDS",1),	/* 1 */
   s_A("NAME",1),		/* 2 */
   s_A("NULLABLE",1),		/* 3 */
   s_A("TYPE",1),		/* 4 */
   s_A("PRECISION",1),		/* 5 */
   s_A("SCALE",1),		/* 6 */
   s_A("sol_type",1),		/* 7 */
   s_A("sol_length",1),         /* 8 */
   s_A("CursorName",1),		/* 9 */
   s_A("odbc_more_results",1),	/* 10 */
   s_A("ParamValues",0),        /* 11 */

   s_A("LongReadLen",0),        /* 12 */
   s_A("odbc_ignore_named_placeholders",0),	/* 13 */
   s_A("odbc_default_bind_type",0),             /* 14 */
   s_A("odbc_force_rebind",0),	/* 15 */
   s_A("odbc_query_timeout",0),	/* 16 */
   s_A("odbc_putdata_start",0),	/* 17 */
   s_A("ParamTypes",0),        /* 18 */
   s_A("odbc_column_display_size",0),	/* 19 */
   s_A("",0),			/* END */
};

static T_st_params S_st_store_params[] =
{
   s_A("odbc_ignore_named_placeholders",0),	/* 0 */
   s_A("odbc_default_bind_type",0),	/* 1 */
   s_A("odbc_force_rebind",0),	/* 2 */
   s_A("odbc_query_timeout",0),	/* 3 */
   s_A("odbc_putdata_start",0),	/* 4 */
   s_A("odbc_column_display_size",0),	/* 5 */
   s_A("",0),			/* END */
};
#undef s_A



/*======================================================================*/
/*                                                                      */
/*  dbd_st_FETCH_attrib                                                 */
/*  ===================                                                 */
/*                                                                      */
/*======================================================================*/
SV *dbd_st_FETCH_attrib(SV *sth, imp_sth_t *imp_sth, SV *keysv)
{
   dTHR;
   STRLEN kl;
   char *key = SvPV(keysv,kl);
   int i;
   SV *retsv = NULL;
   T_st_params *par;
   char cursor_name[256];
   SWORD cursor_name_len;
   RETCODE rc;

   for (par = S_st_fetch_params; par->len > 0; par++)
      if (par->len == kl && strEQ(key, par->str))
	 break;


   if (par->len <= 0)
      return Nullsv;

   if (par->need_describe && !imp_sth->done_desc && !dbd_describe(sth, imp_sth,0))
   {
      /* dbd_describe has already called dbd_error()          */
      /* we can't return Nullsv here because the xs code will */
      /* then just pass the attribute name to DBI for FETCH.  */
       if (DBIc_TRACE(imp_sth, 0, 0, 4)) {
           TRACE1(imp_sth,
                  "   !!!dbd_st_FETCH_attrib (%s) needed query description, "
                  "but failed\n", par->str);
      }
      if (DBIc_WARN(imp_sth)) {
	 warn("Describe failed during %s->FETCH(%s,%d)",
              SvPV(sth,na), key,imp_sth->done_desc);
      }
      return &sv_undef;
   }

   i = DBIc_NUM_FIELDS(imp_sth);


   switch(par - S_st_fetch_params)
   {
      AV *av;

      case 0:			/* NUM_OF_PARAMS */
	 return Nullsv;	/* handled by DBI */
      case 1:			/* NUM_OF_FIELDS */
        if (DBIc_TRACE(imp_sth, 0, 0, 9)) {
	    TRACE1(imp_sth, "    dbd_st_FETCH_attrib NUM_OF_FIELDS %d\n", i);
        }
        retsv = newSViv(i);
        break;
      case 2: 			/* NAME */
	 av = newAV();
	 retsv = newRV(sv_2mortal((SV*)av));
	 if (DBIc_TRACE(imp_sth, 0, 0, 9)) {
	    int j;
	    TRACE1(imp_sth, "    dbd_st_FETCH_attrib NAMES %d\n", i);

	    for (j = 0; j < i; j++)
                TRACE1(imp_sth, "\t%s\n", imp_sth->fbh[j].ColName);
	 }
	 while(--i >= 0) {
             if (DBIc_TRACE(imp_sth, 0, 0, 9)) {
                 TRACE2(imp_sth, "    Colname %d => %s\n",
                        i, imp_sth->fbh[i].ColName);
             }
#ifdef WITH_UNICODE
             av_store(av, i,
                      sv_newwvn((SQLWCHAR *)imp_sth->fbh[i].ColName,
                                imp_sth->fbh[i].ColNameLen));
#else
             av_store(av, i, newSVpv(imp_sth->fbh[i].ColName, 0));
#endif
	 }
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
	 rc = SQLGetCursorName(imp_sth->hstmt, cursor_name,
                               sizeof(cursor_name), &cursor_name_len);
	 if (!SQL_SUCCEEDED(rc)) {
	    dbd_error(sth, rc, "st_FETCH/SQLGetCursorName");
	    return Nullsv;
	 }
	 retsv = newSVpv(cursor_name, cursor_name_len);
	 break;
      case 10:                /* odbc_more_results */
	 retsv = newSViv(imp_sth->moreResults);
	 if (i == 0 && imp_sth->moreResults == 0) {
	    int outparams = (imp_sth->out_params_av) ?
                AvFILL(imp_sth->out_params_av)+1 : 0;
	    if (DBIc_TRACE(imp_sth, 0, 0, 4)) {
                TRACE0(imp_sth,
                       "    numfields == 0 && moreResults = 0 finish\n");
	    }
	    if (outparams) {
	       odbc_handle_outparams(imp_sth, DBIc_TRACE_LEVEL(imp_sth));
	    }
	       /* XXX need to 'finish' here */
	    dbd_st_finish(sth, imp_sth);
	 }
	 break;
      case 11:                                  /* ParamValues */
      {
	 /* not sure if there's a memory leak here. */
	 HV *paramvalues = newHV();
	 if (imp_sth->all_params_hv) {
	    HV *hv = imp_sth->all_params_hv;
	    SV *sv;
	    char *key;
	    I32 retlen;
	    hv_iterinit(hv);
	    while( (sv = hv_iternextsv(hv, &key, &retlen)) != NULL ) {
	       if (sv != &sv_undef) {
		  phs_t *phs = (phs_t*)(void*)SvPVX(sv);
		  hv_store(paramvalues, phs->name, (I32)strlen(phs->name),
                           newSVsv(phs->sv), 0);
	       }
	    }
	 }
	 /* ensure HV is freed when the ref is freed */
	 retsv = newRV_noinc((SV *)paramvalues);
         break;
      }
      case 12: /* LongReadLen */
	 retsv = newSViv(DBIc_LongReadLen(imp_sth));
	 break;
      case 13: /* odbc_ignore_named_placeholders */
	 retsv = newSViv(imp_sth->odbc_ignore_named_placeholders);
	 break;
      case 14: /* odbc_default_bind_type */
	 retsv = newSViv(imp_sth->odbc_default_bind_type);
	 break;
      case 15: /* odbc_force_rebind */
	 retsv = newSViv(imp_sth->odbc_force_rebind);
	 break;
      case 16: /* odbc_query_timeout */
        /*
         * -1 is our internal flag saying odbc_query_timeout has never been
         * set so we map it back to the default for ODBC which is 0
         */
        if (imp_sth->odbc_query_timeout == -1) {
            retsv = newSViv(0);
        } else {
            retsv = newSViv(imp_sth->odbc_query_timeout);
        }
        break;
      case 17: /* odbc_putdata_start */
        retsv = newSViv(imp_sth->odbc_putdata_start);
        break;
      case 18:                                  /* ParamTypes */
      {
	 /* not sure if there's a memory leak here. */
	 HV *paramtypes = newHV();
	 if (imp_sth->all_params_hv) {
	    HV *hv = imp_sth->all_params_hv;
	    SV *sv;
	    char *key;
	    I32 retlen;
	    hv_iterinit(hv);
	    while( (sv = hv_iternextsv(hv, &key, &retlen)) != NULL ) {
	       if (sv != &sv_undef) {
                   HV *subh = newHV();

                   phs_t *phs = (phs_t*)(void*)SvPVX(sv);
                   hv_store(subh, "TYPE", 4, newSViv(phs->sql_type), 0);
                   hv_store(paramtypes, phs->name, (I32)strlen(phs->name),
                            newRV_noinc((SV *)subh), 0);
	       }
	    }
	 }
	 /* ensure HV is freed when the ref is freed */
	 retsv = newRV_noinc((SV *)paramtypes);
         break;
      }
      case 19: /* odbc_column_display_size */
        retsv = newSViv(imp_sth->odbc_column_display_size);
        break;
      default:
	 return Nullsv;
   }
   return sv_2mortal(retsv);
}



/*======================================================================*/
/*                                                                      */
/*  dbd_st_STORE_attrib                                                 */
/*  ===================                                                 */
/*                                                                      */
/*======================================================================*/
int dbd_st_STORE_attrib(SV *sth, imp_sth_t *imp_sth, SV *keysv, SV *valuesv)
{
   dTHR;
   D_imp_dbh_from_sth;
   STRLEN kl;
   STRLEN vl;
   char *key = SvPV(keysv,kl);
   char *value = SvPV(valuesv, vl);
   T_st_params *par;

   for (par = S_st_store_params; par->len > 0; par++)
      if (par->len == kl && strEQ(key, par->str))
	 break;

   if (par->len <= 0)
      return FALSE;

   switch(par - S_st_store_params)
   {
      case 0:
	 imp_sth->odbc_ignore_named_placeholders = SvTRUE(valuesv);
	 return TRUE;

      case 1:
	 imp_sth->odbc_default_bind_type = SvIV(valuesv);
	 return TRUE;
	 break;

      case 2:
	 imp_sth->odbc_force_rebind = (int)SvIV(valuesv);
	 return TRUE;
	 break;

      case 3:
	 imp_sth->odbc_query_timeout = SvIV(valuesv);
	 return TRUE;
	 break;

      case 4:
         imp_sth->odbc_putdata_start = SvIV(valuesv);
         return TRUE;
         break;

      case 5:
         imp_sth->odbc_column_display_size = SvIV(valuesv);
         return TRUE;
         break;
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
   int size = 256;
   char *rgbInfoValue;
   SWORD cbInfoValue = -2;

   New(0, rgbInfoValue, size, char);

   /* See fancy logic below */
   for (i = 0; i < 6; i++)
      rgbInfoValue[i] = (char)0xFF;

   rc = SQLGetInfo(imp_dbh->hdbc, (SQLUSMALLINT)ftype,
		   rgbInfoValue, (SQLSMALLINT)(size-1), &cbInfoValue);
   if (cbInfoValue > size-1) {
      Renew(rgbInfoValue, cbInfoValue+1, char);
      rc = SQLGetInfo(imp_dbh->hdbc, (SQLUSMALLINT)ftype,
		      rgbInfoValue, cbInfoValue, &cbInfoValue);
   }
   if (!SQL_SUCCEEDED(rc)) {
      dbd_error(dbh, rc, "odbc_get_info/SQLGetInfo");
      Safefree(rgbInfoValue);
      /* patched 2/12/02, thanks to Steffen Goldner */
      return &sv_undef;
      /* return Nullsv; */
   }

   /* Fancy logic here to determine if result is a string or int */
   if (cbInfoValue == -2)				/* is int */
      retsv = newSViv(*(int *)rgbInfoValue);	/* XXX cast */
   else if (cbInfoValue != 2 && cbInfoValue != 4)	/* must be string */
      retsv = newSVpv(rgbInfoValue, 0);
   else if (rgbInfoValue[cbInfoValue] == '\0')	/* must be string */ /* patch from Steffen Goldner 0.37 2/12/02 */
      retsv = newSVpv(rgbInfoValue, 0);
   else if (cbInfoValue == 2)			/* short */
      retsv = newSViv(*(short *)rgbInfoValue);	/* XXX cast */
   else if (cbInfoValue == 4)			/* int */
      retsv = newSViv(*(int *)rgbInfoValue);	/* XXX cast */
   else
      croak("panic: SQLGetInfo cbInfoValue == %d", cbInfoValue);

   if (DBIc_TRACE(imp_dbh, 0, 0, 4))
      PerlIO_printf(
          DBIc_LOGPIO(imp_dbh),
          "    SQLGetInfo: ftype %d, cbInfoValue %d: %s\n",
          ftype, cbInfoValue, neatsvpv(retsv,0));

   Safefree(rgbInfoValue);
   return sv_2mortal(retsv);
}

#ifdef THE_FOLLOWING_NO_LONGER_USED_REPLACE_BY_dbd_st_primary_keys
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
   int dbh_active;

   imp_sth->henv = imp_dbh->henv;	/* needed for dbd_error */
   imp_sth->hdbc = imp_dbh->hdbc;

   imp_sth->done_desc = 0;

   if ((dbh_active = check_connection_active(dbh)) == 0) return 0;

   rc = SQLAllocHandle(SQL_HANDLE_STMT, imp_dbh->hdbc, &imp_sth->hstmt);
   if (rc != SQL_SUCCESS) {
      dbd_error(sth, rc, "odbc_get_statistics/SQLAllocHandle(stmt)");
      return 0;
   }

   rc = SQLStatistics(imp_sth->hstmt,
		      CatalogName, (SQLSMALLINT)strlen(CatalogName),
		      SchemaName, (SQLSMALLINT)strlen(SchemaName),
		      TableName, (SQLSMALLINT)strlen(TableName),
		      (SQLUSMALLINT)Unique, (SQLUSMALLINT)0);
   if (!SQL_SUCCEEDED(rc)) {
      dbd_error(sth, rc, "odbc_get_statistics/SQLGetStatistics");
      return 0;
   }
   return build_results(sth, dbh, rc);
}
#endif /* THE_FOLLOWING_NO_LONGER_USED_REPLACE_BY_dbd_st_primary_keys */

#ifdef THE_FOLLOWING_NO_LONGER_USED_REPLACE_BY_dbd_st_primary_keys
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
   int dbh_active;

   imp_sth->henv = imp_dbh->henv;	/* needed for dbd_error */
   imp_sth->hdbc = imp_dbh->hdbc;

   imp_sth->done_desc = 0;

   if ((dbh_active = check_connection_active(dbh)) == 0) return 0;

   rc = SQLAllocHandle(SQL_HANDLE_STMT, imp_dbh->hdbc, &imp_sth->hstmt);
   if (rc != SQL_SUCCESS) {
      dbd_error(sth, rc, "odbc_get_primary_keys/SQLAllocHandle(stmt)");
      return 0;
   }

   rc = SQLPrimaryKeys(imp_sth->hstmt,
		       CatalogName, (SQLSMALLINT)strlen(CatalogName),
		       SchemaName, (SQLSMALLINT)strlen(SchemaName),
		       TableName, (SQLSMALLINT)strlen(TableName));
   if (DBIc_TRACE(imp_sth, 0, 0, 3))
       TRACE1(imp_dbh, "    SQLPrimaryKeys rc = %d\n", rc);
   if (!SQL_SUCCEEDED(rc)) {
      dbd_error(sth, rc, "odbc_get_primary_keys/SQLPrimaryKeys");
      return 0;
   }
   return build_results(sth, dbh, rc);
}
#endif /* THE_FOLLOWING_NO_LONGER_USED_REPLACE_BY_dbd_st_primary_keys */



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
   int dbh_active;

   imp_sth->henv = imp_dbh->henv;	/* needed for dbd_error */
   imp_sth->hdbc = imp_dbh->hdbc;

   imp_sth->done_desc = 0;

   if ((dbh_active = check_connection_active(dbh)) == 0) return 0;

   rc = SQLAllocHandle(SQL_HANDLE_STMT, imp_dbh->hdbc, &imp_sth->hstmt);
   if (rc != SQL_SUCCESS) {
      dbd_error(sth, rc, "odbc_get_special_columns/SQLAllocHandle(stmt)");
      return 0;
   }

   rc = SQLSpecialColumns(imp_sth->hstmt,
			  (SQLSMALLINT)Identifier,
			  CatalogName, (SQLSMALLINT)strlen(CatalogName),
			  SchemaName, (SQLSMALLINT)strlen(SchemaName),
			  TableName, (SQLSMALLINT)strlen(TableName),
			  (SQLSMALLINT)Scope, (SQLSMALLINT)Nullable);
   if (!SQL_SUCCEEDED(rc)) {
      dbd_error(sth, rc, "odbc_get_special_columns/SQLSpecialClumns");
      return 0;
   }
   return build_results(sth, dbh, rc);
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
   int dbh_active;

   imp_sth->henv = imp_dbh->henv;	/* needed for dbd_error */
   imp_sth->hdbc = imp_dbh->hdbc;

   imp_sth->done_desc = 0;

   if ((dbh_active = check_connection_active(dbh)) == 0) return 0;

   rc = SQLAllocHandle(SQL_HANDLE_STMT, imp_dbh->hdbc, &imp_sth->hstmt);
   if (rc != SQL_SUCCESS) {
      dbd_error(sth, rc, "odbc_get_foreign_keys/SQLAllocHandle(stmt)");
      return 0;
   }


   /* just for sanity, later.  Any internals that may rely on this (including */
   /* debugging) will have valid data */
   imp_sth->statement = (char *)safemalloc(strlen(cSqlForeignKeys)+
					   strlen(XXSAFECHAR(PK_CatalogName))+
					   strlen(XXSAFECHAR(PK_SchemaName))+
					   strlen(XXSAFECHAR(PK_TableName))+
					   strlen(XXSAFECHAR(FK_CatalogName))+
					   strlen(XXSAFECHAR(FK_SchemaName))+
					   strlen(XXSAFECHAR(FK_TableName))+
					   1);

   sprintf(imp_sth->statement,
	   cSqlForeignKeys,
	   XXSAFECHAR(PK_CatalogName), XXSAFECHAR(PK_SchemaName),XXSAFECHAR(PK_TableName),
	   XXSAFECHAR(FK_CatalogName), XXSAFECHAR(FK_SchemaName),XXSAFECHAR(FK_TableName)
	  );
   /* fix to handle "" (undef) calls -- thanks to Kevin Shepherd */
   rc = SQLForeignKeys(
       imp_sth->hstmt,
       (PK_CatalogName && *PK_CatalogName) ? PK_CatalogName : 0, SQL_NTS,
       (PK_SchemaName && *PK_SchemaName) ? PK_SchemaName : 0, SQL_NTS,
       (PK_TableName && *PK_TableName) ? PK_TableName : 0, SQL_NTS,
       (FK_CatalogName && *FK_CatalogName) ? FK_CatalogName : 0, SQL_NTS,
       (FK_SchemaName && *FK_SchemaName) ? FK_SchemaName : 0, SQL_NTS,
       (FK_TableName && *FK_TableName) ? FK_TableName : 0, SQL_NTS);
   if (!SQL_SUCCEEDED(rc)) {
      dbd_error(sth, rc, "odbc_get_foreign_keys/SQLForeignKeys");
      return 0;
   }
   return build_results(sth, dbh, rc);
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
   SQLULEN ColSize;
   RETCODE rc;
   rc = SQLDescribeCol(imp_sth->hstmt, (SQLSMALLINT)colno,
		       ColumnName, BufferLength, NameLength,
		       DataType, &ColSize, DecimalDigits, Nullable);
   if (!SQL_SUCCEEDED(rc)) {
      dbd_error(sth, rc, "DescribeCol/SQLDescribeCol");
      return 0;
   }
   *ColumnSize = (U32)ColSize;
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
   int dbh_active;

#if 0
   /* TBD: cursorname? */
   char cname[128];			/* cursorname */
#endif

   imp_sth->henv = imp_dbh->henv;	/* needed for dbd_error */
   imp_sth->hdbc = imp_dbh->hdbc;

   imp_sth->done_desc = 0;

   if ((dbh_active = check_connection_active(dbh)) == 0) return 0;

   rc = SQLAllocHandle(SQL_HANDLE_STMT, imp_dbh->hdbc, &imp_sth->hstmt);
   if (rc != SQL_SUCCESS) {
      dbd_error(sth, rc, "odbc_get_type_info/SQLAllocHandle(stmt)");
      return 0;
   }

   /* just for sanity, later. Any internals that may rely on this (including */
   /* debugging) will have valid data */
   imp_sth->statement = (char *)safemalloc(strlen(cSqlGetTypeInfo)+ftype/10+1);
   sprintf(imp_sth->statement, cSqlGetTypeInfo, ftype);

   rc = SQLGetTypeInfo(imp_sth->hstmt, (SQLSMALLINT)ftype);

   dbd_error(sth, rc, "odbc_get_type_info/SQLGetTypeInfo");
   if (!SQL_SUCCEEDED(rc)) {
      SQLFreeHandle(SQL_HANDLE_STMT,imp_sth->hstmt);
      /* SQLFreeStmt(imp_sth->hstmt, SQL_DROP);*/ /* TBD: 3.0 update */
      imp_sth->hstmt = SQL_NULL_HSTMT;
      return 0;
   }

   return build_results(sth, dbh, rc);
}



SV *
   odbc_cancel(sth)
   SV *sth;
{
   dTHR;
   D_imp_sth(sth);
   RETCODE rc;

   rc = SQLCancel(imp_sth->hstmt);
   if (!SQL_SUCCEEDED(rc)) {
      dbd_error(sth, rc, "odbc_cancel/SQLCancel");
      return Nullsv;
   }
   return newSViv(1);
}



/************************************************************************/
/*                                                                      */
/*  odbc_col_attributes                                                 */
/*  ===================                                                 */
/*                                                                      */
/************************************************************************/
SV *odbc_col_attributes(SV *sth, int colno, int desctype)
{
   dTHR;
   D_imp_sth(sth);
   RETCODE rc;
   SV *retsv = NULL;
   char str_attr[256];
   SWORD str_attr_len = 0;
   SQLLEN num_attr = 0;

   memset(str_attr, '\0', sizeof(str_attr));

   if ( !DBIc_ACTIVE(imp_sth) ) {
      dbd_error(sth, SQL_ERROR, "no statement executing");
      return Nullsv;
   }

   /*
    * At least on Win95, calling this with colno==0 would "core" dump/GPF.
    * protect, even though it's valid for some values of desctype
    * (e.g. SQL_COLUMN_COUNT, since it doesn't depend on the colcount)
    */
   if (colno == 0) {
      dbd_error(sth, SQL_ERROR,
		"cannot obtain SQLColAttributes for column 0");
      return Nullsv;
   }

   rc = SQLColAttributes(imp_sth->hstmt, (SQLUSMALLINT)colno,
			 (SQLUSMALLINT)desctype,
			 str_attr, sizeof(str_attr),
			 &str_attr_len, &num_attr);

   if (!SQL_SUCCEEDED(rc)) {
      dbd_error(sth, rc, "odbc_col_attributes/SQLColAttributes");
      return Nullsv;
   } else if (SQL_SUCCESS_WITH_INFO == rc) {
       warn("SQLColAttributes has truncated returned data");
   }


   if (DBIc_TRACE(imp_sth, 0, 0, 3)) {
      PerlIO_printf(
          DBIc_LOGPIO(imp_sth),
          "    SQLColAttributes: colno=%d, desctype=%d, str_attr=%s, "
          "str_attr_len=%d, num_attr=%ld",
          colno, desctype, str_attr, str_attr_len, (long)num_attr);
   }

   switch (desctype) {
     case SQL_COLUMN_AUTO_INCREMENT:
     case SQL_COLUMN_CASE_SENSITIVE:
     case SQL_COLUMN_COUNT:
     case SQL_COLUMN_DISPLAY_SIZE:
     case SQL_COLUMN_LENGTH:
     case SQL_COLUMN_MONEY:
     case SQL_COLUMN_NULLABLE:
     case SQL_COLUMN_PRECISION:
     case SQL_COLUMN_SCALE:
     case SQL_COLUMN_SEARCHABLE:
     case SQL_COLUMN_TYPE:
     case SQL_COLUMN_UNSIGNED:
     case SQL_COLUMN_UPDATABLE:
     {
         retsv = newSViv(num_attr);
         break;
     }
     case SQL_COLUMN_LABEL:
     case SQL_COLUMN_NAME:
     case SQL_COLUMN_OWNER_NAME:
     case SQL_COLUMN_QUALIFIER_NAME:
     case SQL_COLUMN_TABLE_NAME:
     case SQL_COLUMN_TYPE_NAME:
     {
         /*
          * NOTE: in unixODBC 2.2.11, if you called SQLDriverConnectW and
          * then called SQLColAttributes for a string type it would often
          * return half the number of characters it had written to
          * str_attr in str_attr_len.
          */
         retsv = newSVpv(str_attr, strlen(str_attr));
         break;
     }
     default:
     {
         dbd_error(sth, SQL_ERROR,
                   "driver-specific column attributes not supported");
         return Nullsv;
         break;
     }
   }


#ifdef OLD_STUFF_THAT_SEEMS_FLAWED
   /*
    * sigh...Oracle's ODBC driver version 8.0.4 resets str_attr_len to 0, when
    * putting a value in num_attr.  This is a change!
    *
    * double sigh.  SQL Server (and MySql under Unix) set str_attr_len
    * but use num_attr, not str_attr.  This change may be problematic
    * for other drivers. (the additional || num_attr != -2...)
    */
   if (str_attr_len == -2 || str_attr_len == 0 || num_attr != -2)
      retsv = newSViv(num_attr);
   else if (str_attr_len != 2 && str_attr_len != 4)
      retsv = newSVpv(str_attr, 0);
   else if (str_attr[str_attr_len] == '\0') /* fix for DBD::ODBC 0.39 thanks to Nicolas DeRico */
      retsv = newSVpv(str_attr, 0);
   else {
      if (str_attr_len == 2)
	 retsv = newSViv(*(short *)str_attr);
      else
	 retsv = newSViv(*(int *)str_attr);
   }
#endif

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
    int dbh_active;
    imp_sth->henv = imp_dbh->henv;	/* needed for dbd_error */
    imp_sth->hdbc = imp_dbh->hdbc;

    imp_sth->done_desc = 0;

    if ((dbh_active = check_connection_active(dbh)) == 0) return 0;

    rc = SQLAllocHandle(SQL_HANDLE_STMT, imp_dbh->hdbc, &imp_sth->hstmt);
    if (rc != SQL_SUCCESS) {
        dbd_error(sth, rc, "odbc_db_columns/SQLAllocHandle(stmt)");
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

    if (DBIc_TRACE(imp_sth, 0, 0, 3))
        PerlIO_printf(
            DBIc_LOGPIO(imp_dbh),
            "    SQLColumns call: cat = %s, schema = %s, table = %s, "
            "column = %s\n",
            XXSAFECHAR(catalog), XXSAFECHAR(schema), XXSAFECHAR(table),
            XXSAFECHAR(column));
    dbd_error(sth, rc, "odbc_columns/SQLColumns");

    if (!SQL_SUCCEEDED(rc)) {
        SQLFreeHandle(SQL_HANDLE_STMT,imp_sth->hstmt);
        /* SQLFreeStmt(imp_sth->hstmt, SQL_DROP);*/ /* TBD: 3.0 update */
        imp_sth->hstmt = SQL_NULL_HSTMT;
        return 0;
    }
    return build_results(sth, dbh, rc);
}



/*
 *  AllODBCErrors
 *  =============
 *
 *  Given ODBC environment, connection and statement handles (any of which may
 *  be null) this function will retrieve all ODBC errors recorded and
 *  optionally (if output is not 0) output then to the specified log handle.
 *
 */
static void AllODBCErrors(
    HENV henv, HDBC hdbc, HSTMT hstmt, int output, PerlIO *logfp)
{
    SQLRETURN rc;

    do {
        UCHAR sqlstate[SQL_SQLSTATE_SIZE+1];
        /* ErrorMsg must not be greater than SQL_MAX_MESSAGE_LENGTH */
        UCHAR ErrorMsg[SQL_MAX_MESSAGE_LENGTH];
        SWORD ErrorMsgLen;
        SDWORD NativeError;

        /* TBD: 3.0 update */
        rc=SQLError(henv, hdbc, hstmt,
                    sqlstate, &NativeError,
                    ErrorMsg, sizeof(ErrorMsg)-1, &ErrorMsgLen);

        if (output && SQL_SUCCEEDED(rc))
            PerlIO_printf(logfp, "%s %s\n", sqlstate, ErrorMsg);

    } while(SQL_SUCCEEDED(rc));
    return;
}



/************************************************************************/
/*                                                                      */
/*  check_connection_active                                             */
/*  =======================                                             */
/*                                                                      */
/************************************************************************/
static int check_connection_active(SV *h)
{
    D_imp_xxh(h);
    struct imp_dbh_st *imp_dbh = NULL;
    struct imp_sth_st *imp_sth = NULL;

    switch(DBIc_TYPE(imp_xxh)) {
      case DBIt_ST:
        imp_sth = (struct imp_sth_st *)imp_xxh;
        imp_dbh = (struct imp_dbh_st *)(DBIc_PARENT_COM(imp_sth));
        break;
      case DBIt_DB:
        imp_dbh = (struct imp_dbh_st *)imp_xxh;
        break;
      default:
        croak("panic: check_connection_active bad handle type");
    }

    if (!DBIc_ACTIVE(imp_dbh)) {
        DBIh_SET_ERR_CHAR(
            h, imp_xxh, Nullch, 1,
            "Cannot allocate statement when disconnected from the database",
            "08003", Nullch);
        return 0;
    }
    return 1;

}



/************************************************************************/
/*                                                                      */
/*  set_odbc_version                                                    */
/*  ================                                                    */
/*                                                                      */
/*  Set the ODBC version we require. This defaults to ODBC 3 but if     */
/*  attr contains the odbc_version atttribute this overrides it. If we  */
/*  fail for any reason the env handle is freed, the error reported and */
/*  0 is returned. If all ok, 1 is returned.                            */
/*                                                                      */
/************************************************************************/
static int set_odbc_version(
    SV *dbh,
    imp_dbh_t *imp_dbh,
    SV* attr)
{
   D_imp_drh_from_dbh;
   SV **svp;
   UV odbc_version = 0;
   SQLRETURN rc;


   DBD_ATTRIB_GET_IV(
       attr, "odbc_version", 12, svp, odbc_version);
   if (svp && odbc_version) {
       rc = SQLSetEnvAttr(imp_drh->henv, SQL_ATTR_ODBC_VERSION,
                          (SQLPOINTER)odbc_version, SQL_IS_INTEGER);
   } else {
       /* make sure we request a 3.0 version */
       rc = SQLSetEnvAttr(imp_drh->henv, SQL_ATTR_ODBC_VERSION,
                          (SQLPOINTER)SQL_OV_ODBC3, SQL_IS_INTEGER);
   }
   if (!SQL_SUCCEEDED(rc)) {
       dbd_error2(
           dbh, rc, "db_login/SQLSetEnvAttr", imp_drh->henv, 0, 0);
       if (imp_drh->connects == 0) {
           SQLFreeHandle(SQL_HANDLE_ENV, imp_drh->henv);
           imp_drh->henv = SQL_NULL_HENV;
       }
       return 0;
   }
   return 1;
}



/************************************************************************/
/*                                                                      */
/*  post_connect                                                        */
/*  ============                                                        */
/*                                                                      */
/*  Operations to perform immediately after we have connected.          */
/*                                                                      */
/*  NOTE: prior to DBI subversion version 11605 (fixed post 1.607)      */
/*    DBD_ATTRIB_DELETE segfaulted so instead of calling:               */
/*    DBD_ATTRIB_DELETE(attr, "odbc_cursortype",                        */
/*                      strlen("odbc_cursortype"));                     */
/*    we do the following:                                              */
/*      hv_delete((HV*)SvRV(attr), "odbc_cursortype",                   */
/*                strlen("odbc_cursortype"), G_DISCARD);                */
/*                                                                      */
/************************************************************************/
static int post_connect(
    SV *dbh,
    imp_dbh_t *imp_dbh,
    SV *attr)
{
   D_imp_drh_from_dbh;
   SQLRETURN rc;
   SWORD dbvlen;
   UWORD supported;

   if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
       TRACE0(imp_dbh, "Turning autocommit on\n");

   /* DBI spec requires AutoCommit on */
   rc = SQLSetConnectAttr(imp_dbh->hdbc, SQL_AUTOCOMMIT,
                          (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
   if (!SQL_SUCCEEDED(rc)) {
      dbd_error(dbh, rc, "post_connect/SQLSetConnectAttr(SQL_AUTOCOMMIT)");
      SQLFreeHandle(SQL_HANDLE_DBC, imp_dbh->hdbc);
      if (imp_drh->connects == 0) {
          SQLFreeHandle(SQL_HANDLE_ENV, imp_drh->henv);
          imp_drh->henv = SQL_NULL_HENV;
          imp_dbh->henv = SQL_NULL_HENV;    /* needed for dbd_error */
      }
      return 0;
   }
   DBIc_set(imp_dbh,DBIcf_AutoCommit, 1);

   /* get the ODBC compatibility level for this driver */
   rc = SQLGetInfo(imp_dbh->hdbc, SQL_DRIVER_ODBC_VER, &imp_dbh->odbc_ver,
		   (SWORD)sizeof(imp_dbh->odbc_ver), &dbvlen);
   if (!SQL_SUCCEEDED(rc)) {
      dbd_error(dbh, rc, "post_connect/SQLGetInfo(DRIVER_ODBC_VER)");
      strcpy(imp_dbh->odbc_ver, "01.00");
   }
   if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
       TRACE1(imp_dbh, "DRIVER_ODBC_VER = %s\n", imp_dbh->odbc_ver);

   /* get ODBC driver name and version */
   rc = SQLGetInfo(imp_dbh->hdbc, SQL_DRIVER_NAME, &imp_dbh->odbc_driver_name,
                   (SQLSMALLINT)sizeof(imp_dbh->odbc_driver_name), &dbvlen);
   if (!SQL_SUCCEEDED(rc)) {
       dbd_error(dbh, rc, "post_connect/SQLGetInfo(DRIVER_NAME)");
       strcpy(imp_dbh->odbc_driver_name, "unknown");
   }
   if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
       TRACE1(imp_dbh, "DRIVER_NAME = %s\n", imp_dbh->odbc_driver_name);

   rc = SQLGetInfo(imp_dbh->hdbc, SQL_DRIVER_VER,
                   &imp_dbh->odbc_driver_version,
                   (SQLSMALLINT)sizeof(imp_dbh->odbc_driver_version), &dbvlen);
   if (!SQL_SUCCEEDED(rc)) {
       dbd_error(dbh, rc, "post_connect/SQLGetInfo(DRIVER_VERSION)");
       strcpy(imp_dbh->odbc_driver_name, "unknown");
   }
   if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
       TRACE1(imp_dbh, "DRIVER_VERSION = %s\n", imp_dbh->odbc_driver_version);

   /* find maximum column name length */
   rc = SQLGetInfo(imp_dbh->hdbc, SQL_MAX_COLUMN_NAME_LEN,
                   &imp_dbh->max_column_name_len,
		   (SWORD) sizeof(imp_dbh->max_column_name_len), &dbvlen);
   if (!SQL_SUCCEEDED(rc)) {
      dbd_error(dbh, rc, "post_connect/SQLGetInfo(MAX_COLUMN_NAME_LEN)");
      imp_dbh->max_column_name_len = 256;
   } else {
       if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
           TRACE1(imp_dbh, "MAX_COLUMN_NAME_LEN = %d\n",
                  imp_dbh->max_column_name_len);
   }
   if (imp_dbh->max_column_name_len > 256) {
       imp_dbh->max_column_name_len = 256;
       DBIh_SET_ERR_CHAR(
           dbh, (imp_xxh_t*)imp_drh, "0", 1,
           "Max column name length pegged at 256", Nullch, Nullch);
   }

   /* default ignoring named parameters to false */
   imp_dbh->odbc_ignore_named_placeholders = 0;

#ifdef WITH_UNICODE
   imp_dbh->odbc_has_unicode = 1;
#else
   imp_dbh->odbc_has_unicode = 0;
#endif

   if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
       TRACE1(imp_dbh, "DBD::ODBC is unicode built : %s\n",
              imp_dbh->odbc_has_unicode ? "YES" : "NO");

   imp_dbh->odbc_default_bind_type = 0;
   /* flag to see if SQLDescribeParam is supported */
   imp_dbh->odbc_sqldescribeparam_supported = -1;
   /* flag to see if SQLDescribeParam is supported */
   imp_dbh->odbc_sqlmoreresults_supported = -1;
   imp_dbh->odbc_defer_binding = 0;
   imp_dbh->odbc_force_rebind = 0;
   /* default value for query timeout is -1 which means do not set the
      query timeout at all. */
   imp_dbh->odbc_query_timeout = -1;
   imp_dbh->odbc_putdata_start = 32768;
   imp_dbh->odbc_column_display_size = 2001;
   imp_dbh->odbc_exec_direct = 0; /* default to not having SQLExecDirect used */
   imp_dbh->RowCacheSize = 1;	/* default value for now */

   /* see if we're connected to MS SQL Server */
   memset(imp_dbh->odbc_dbname, 'z', sizeof(imp_dbh->odbc_dbname));
   rc = SQLGetInfo(imp_dbh->hdbc, SQL_DBMS_NAME, imp_dbh->odbc_dbname,
		   (SWORD)sizeof(imp_dbh->odbc_dbname), &dbvlen);
   if (SQL_SUCCEEDED(rc)) {
       /* can't find stricmp on my Linux, nor strcmpi. must be a
        * portable way to do this*/
      if (!strcmp(imp_dbh->odbc_dbname, "Microsoft SQL Server")) {
	 imp_dbh->odbc_defer_binding = 1;
      }
   } else {
      strcpy(imp_dbh->odbc_dbname, "Unknown/Unsupported");
   }
   if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
       TRACE1(imp_dbh, "SQL_DBMS_NAME = %s\n", imp_dbh->odbc_dbname);

   /* check to see if SQLMoreResults is supported */
   rc = SQLGetFunctions(imp_dbh->hdbc, SQL_API_SQLMORERESULTS, &supported);
   if (SQL_SUCCEEDED(rc)) {
       if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
           TRACE1(imp_dbh, "SQLMoreResults supported: %d\n", supported);
       imp_dbh->odbc_sqlmoreresults_supported = supported ? 1 : 0;
   } else {
      imp_dbh->odbc_sqlmoreresults_supported = 0;
      if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
          TRACE0(imp_dbh,
                 "    !!SQLGetFunctions(SQL_API_SQLMORERESULTS) failed:\n");
      AllODBCErrors(imp_dbh->henv, imp_dbh->hdbc, 0,
		    DBIc_TRACE(imp_dbh, 0, 0, 3), DBIc_LOGPIO(imp_dbh));
   }

   /* call only once per connection / DBH -- may want to do
    * this during the connect to avoid potential threading
    * issues */
   /* check to see if SQLDescribeParam is supported */
   rc = SQLGetFunctions(imp_dbh->hdbc, SQL_API_SQLDESCRIBEPARAM, &supported);
   if (SQL_SUCCEEDED(rc)) {
       imp_dbh->odbc_sqldescribeparam_supported = supported ? 1 : 0;
   } else {
      imp_dbh->odbc_sqldescribeparam_supported = 0;
       if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
           TRACE0(imp_dbh,
                 "    !!SQLGetFunctions(SQL_API_SQLDESCRIBEPARAM) failed:\n");
       AllODBCErrors(imp_dbh->henv, imp_dbh->hdbc, 0,
                     DBIc_TRACE(imp_dbh, 0, 0, 3), DBIc_LOGPIO(imp_dbh));
   }
   if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
       TRACE1(imp_dbh, "SQLDescribeParam supported: %d\n",
              imp_dbh->odbc_sqldescribeparam_supported);

   /* odbc_cursortype */
   {
       SV **svp;
       UV odbc_cursortype = 0;

       DBD_ATTRIB_GET_IV(attr, "odbc_cursortype", 15,
                         svp, odbc_cursortype);
       if (svp && odbc_cursortype) {
           if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
               TRACE1(imp_dbh,
                      "    Setting cursor type to: %d", odbc_cursortype);
           /* delete odbc_cursortype so we don't see it again via STORE */
           hv_delete((HV*)SvRV(attr), "odbc_cursortype",
                     strlen("odbc_cursortype"), G_DISCARD);

           rc = SQLSetConnectAttr(imp_dbh->hdbc,(SQLINTEGER)SQL_CURSOR_TYPE,
                                  (SQLPOINTER)odbc_cursortype,
                                  (SQLINTEGER)SQL_IS_INTEGER);
           if (!SQL_SUCCEEDED(rc) && (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0)))
               TRACE1(imp_dbh, "    !!Failed to set SQL_CURSORTYPE to %d\n",
                      (int)odbc_cursortype);
       }
   }

   /* odbc_query_timeout */
   {
       SV **svp;
       UV   odbc_timeout = 0;

       DBD_ATTRIB_GET_IV(
           attr, "odbc_query_timeout", strlen("odbc_query_timeout"),
           svp, odbc_timeout);
       if (svp && odbc_timeout) {
           imp_dbh->odbc_query_timeout = odbc_timeout;
           if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
               TRACE1(imp_dbh, "    Setting DBH query timeout to %d\n",
                      (int)odbc_timeout);
           /* delete odbc_cursortype so we don't see it again via STORE */
           hv_delete((HV*)SvRV(attr), "odbc_query_timeout",
                     strlen("odbc_query_timeout"), G_DISCARD);
       }
   }

   /* odbc_putdata_start */
   {
       SV **svp;
       IV putdata_start_value;

       DBD_ATTRIB_GET_IV(
           attr, "odbc_putdata_start", strlen("odbc_putdata_start"),
           svp, putdata_start_value);
       if (svp) {
           imp_dbh->odbc_putdata_start = putdata_start_value;
           if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
               TRACE1(imp_dbh, "    Setting DBH putdata_start to %d\n",
                      (int)putdata_start_value);
           /* delete odbc_putdata_start so we don't see it again via STORE */
           hv_delete((HV*)SvRV(attr), "odbc_putdata_start",
                     strlen("odbc_putdata_start"), G_DISCARD);
       }
   }

   /* odbc_column_display_size */
   {
       SV **svp;
       IV column_display_size_value;

       DBD_ATTRIB_GET_IV(
           attr, "odbc_column_display_size",
	   strlen("odbc_column_display_size"),
           svp, column_display_size_value);
       if (svp) {
           imp_dbh->odbc_column_display_size = column_display_size_value;
           if (DBIc_TRACE(imp_dbh, 0x04000000, 0, 0))
               TRACE1(imp_dbh,
		      "    Setting DBH default column display size to %d\n",
                      (int)column_display_size_value);
           /* delete odbc_column_display_size so we don't see it again via STORE */
           hv_delete((HV*)SvRV(attr), "odbc_column_display_size",
                     strlen("odbc_column_display_size"), G_DISCARD);
       }
   }

   return 1;

}



static SQLSMALLINT default_parameter_type(imp_sth_t *imp_sth, phs_t *phs)
{
    if (imp_sth->odbc_default_bind_type != 0) {
        return imp_sth->odbc_default_bind_type;
    } else {
        /* MS Access can return an invalid precision error in the 12blob
           test unless the large valud is bound as an SQL_LONGVARCHAR
           or SQL_WLONGVARCHAR. Who knows what large is, but for now it is
           4000 */
        if (!SvOK(phs->sv)) {
            return ODBC_BACKUP_BIND_TYPE_VALUE;
        } else if (SvCUR(phs->sv) > 4000) {
            return ODBC_BACKUP_LONG_BIND_TYPE_VALUE;
        } else {
            return ODBC_BACKUP_BIND_TYPE_VALUE;
        }
    }
}

/* end */
