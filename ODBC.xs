#include "ODBC.h"

DBISTATE_DECLARE;

MODULE = DBD::ODBC    PACKAGE = DBD::ODBC

INCLUDE: ODBC.xsi

MODULE = DBD::ODBC    PACKAGE = DBD::ODBC::st

SV *
odbc_lob_read(sth, colno, bufsv, length, attr = NULL)
     SV  *sth
     int colno
     SV  *bufsv
     UV  length
     SV  *attr;
    PROTOTYPE: $$$$;$
    PREINIT:
     char *buf;
     IV   ret_len;
     IV   sql_type = 0;
    INIT:
     if (length == 0) {
         croak("Cannot retrieve 0 length lob");
     }
    CODE:
     if (attr) {
         SV **svp;
         DBD_ATTRIBS_CHECK("odbc_lob_read", sth, attr);
         DBD_ATTRIB_GET_IV(attr, "Type", 4, svp, sql_type);
     }
     if (SvROK(bufsv)) {
        bufsv = SvRV(bufsv);
     }
     sv_setpvn(bufsv, "", 0);                   /* ensure we can grow ok */

     buf = SvGROW(bufsv, length + 1);
     ret_len = odbc_st_lob_read(sth, colno, bufsv, length, sql_type);
     if (ret_len >= 0) {
         SvCUR_set(bufsv, ret_len);      /* set length in SV */
         *SvEND(bufsv) = '\0';           /* NUL terminate */
         SvSETMAGIC(bufsv);
         RETVAL = newSViv(ret_len);
     } else {
         XSRETURN_UNDEF;
     }
     OUTPUT:
        RETVAL

void
_ColAttributes(sth, colno, ftype)
	SV *	sth
	int		colno
	int		ftype
	CODE:
	ST(0) = odbc_col_attributes(sth, colno, ftype);

void
_Cancel(sth)
    SV *	sth

    CODE:
	ST(0) = odbc_cancel(sth);

void
_tables(dbh, sth, catalog, schema, table, type)
	SV *	dbh
	SV *	sth
	char *	catalog
	char *	schema
	char *  table
	char *	type
	CODE:
	/* list all tables and views (0 as last parameter) */
	ST(0) = dbd_st_tables(dbh, sth, catalog, schema, table, type) ? &PL_sv_yes : &PL_sv_no;

void
_primary_keys(dbh, sth, catalog, schema, table)
    SV * 	dbh
    SV *	sth
    char *	catalog
    char *	schema
    char *	table
    CODE:
    ST(0) = dbd_st_primary_keys(dbh, sth, catalog, schema, table) ? &PL_sv_yes : &PL_sv_no;

void
_statistics(dbh, sth, catalog, schema, table, unique, quick)
    SV * 	dbh
    SV *	sth
    char *	catalog
    char *	schema
    char *	table
    int         unique
    int         quick

    CODE:
    ST(0) = dbd_st_statistics(dbh, sth, catalog, schema, table,
                              unique, quick) ? &PL_sv_yes : &PL_sv_no;

void
DescribeCol(sth, colno)
	SV *sth
	int colno

	PPCODE:

	char ColumnName[SQL_MAX_COLUMN_NAME_LEN];
	I16 NameLength;
	I16 DataType;
	U32 ColumnSize;
	I16 DecimalDigits;
	I16 Nullable;
	int rc;

	rc = odbc_describe_col(sth, colno, ColumnName, sizeof(ColumnName), &NameLength,
			&DataType, &ColumnSize, &DecimalDigits, &Nullable);
	if (rc) {
		XPUSHs(newSVpv(ColumnName, 0));
		XPUSHs(newSViv(DataType));
		XPUSHs(newSViv(ColumnSize));
		XPUSHs(newSViv(DecimalDigits));
		XPUSHs(newSViv(Nullable));
	}

# ------------------------------------------------------------
# database level interface
# ------------------------------------------------------------
MODULE = DBD::ODBC    PACKAGE = DBD::ODBC::db

void
_ExecDirect( dbh, stmt )
SV *        dbh
SV *        stmt
CODE:
{
   STRLEN lna;
   /*char *pstmt = SvOK(stmt) ? SvPV(stmt,lna) : "";*/
   ST(0) = sv_2mortal(newSViv( (IV)dbd_db_execdirect( dbh, stmt ) ) );
}


void
_columns(dbh, sth, catalog, schema, table, column)
	SV *	dbh
	SV *	sth
	char *	catalog
	char *	schema
	char *	table
	char *	column
	CODE:
	ST(0) = odbc_db_columns(dbh, sth, catalog, schema, table, column) ? &PL_sv_yes : &PL_sv_no;

void
_GetInfo(dbh, ftype)
	SV *	dbh
	int		ftype
	CODE:
	ST(0) = odbc_get_info(dbh, ftype);

void
_GetTypeInfo(dbh, sth, ftype)
	SV *	dbh
	SV *	sth
	int		ftype
	CODE:
	ST(0) = odbc_get_type_info(dbh, sth, ftype) ? &PL_sv_yes : &PL_sv_no;

void
_GetStatistics(dbh, sth, CatalogName, SchemaName, TableName, Unique)
	SV *	dbh
	SV *	sth
	char *	CatalogName
	char *	SchemaName
	char *	TableName
	int     Unique
	CODE:
        ST(0) = dbd_st_statistics(dbh, sth, CatalogName, SchemaName,
                                  TableName, Unique, 0) ? &PL_sv_yes : &PL_sv_no;

void
_GetPrimaryKeys(dbh, sth, CatalogName, SchemaName, TableName)
	SV *	dbh
	SV *	sth
	char *	CatalogName
	char *	SchemaName
	char *	TableName
	CODE:
        /* the following will end up in dbdimp.c/dbd_st_primary_keys */
	ST(0) = odbc_st_primary_keys(dbh, sth, CatalogName, SchemaName, TableName) ? &PL_sv_yes : &PL_sv_no;

void
_GetSpecialColumns(dbh, sth, Identifier, CatalogName, SchemaName, TableName, Scope, Nullable)
	SV *	dbh
	SV *	sth
	int     Identifier
	char *	CatalogName
	char *	SchemaName
	char *	TableName
    int     Scope
    int     Nullable
	CODE:
	ST(0) = odbc_get_special_columns(dbh, sth, Identifier, CatalogName, SchemaName, TableName, Scope, Nullable) ? &PL_sv_yes : &PL_sv_no;

void
_GetForeignKeys(dbh, sth, PK_CatalogName, PK_SchemaName, PK_TableName, FK_CatalogName, FK_SchemaName, FK_TableName)
	SV *	dbh
	SV *	sth
	char *	PK_CatalogName
	char *	PK_SchemaName
	char *	PK_TableName
	char *	FK_CatalogName
	char *	FK_SchemaName
	char *	FK_TableName
	CODE:
	ST(0) = odbc_get_foreign_keys(dbh, sth, PK_CatalogName, PK_SchemaName, PK_TableName, FK_CatalogName, FK_SchemaName, FK_TableName) ? &PL_sv_yes : &PL_sv_no;

#
# Corresponds to ODBC 2.0.  3.0's SQL_API_ODBC3_ALL_FUNCTIONS is handled also
# scheme
void
GetFunctions(dbh, func)
	SV *	dbh
	unsigned short func
	PPCODE:
	UWORD pfExists[SQL_API_ODBC3_ALL_FUNCTIONS_SIZE];
	RETCODE rc;
	int i;
	int j;
	D_imp_dbh(dbh);
	rc = SQLGetFunctions(imp_dbh->hdbc, func, pfExists);
	if (SQL_ok(rc)) {
	   switch (func) {
	      case SQL_API_ALL_FUNCTIONS:
			for (i = 0; i < 100; i++) {
				XPUSHs(pfExists[i] ? &PL_sv_yes : &PL_sv_no);
			}
			break;
	      case SQL_API_ODBC3_ALL_FUNCTIONS:
		 for (i = 0; i < SQL_API_ODBC3_ALL_FUNCTIONS_SIZE; i++) {
		    for (j = 0; j < 8 * sizeof(pfExists[i]); j++) {
		       XPUSHs((pfExists[i] & (1 << j)) ? &PL_sv_yes : &PL_sv_no);
		    }
		 }
		 break;
	      default:
		XPUSHs(pfExists[0] ? &PL_sv_yes : &PL_sv_no);
	   }
	}

MODULE = DBD::ODBC    PACKAGE = DBD::ODBC::db

MODULE = DBD::ODBC    PACKAGE = DBD::ODBC::dr

void
data_sources(drh, attr = NULL)
	SV* drh;
    SV* attr;
  PROTOTYPE: $;$
  PPCODE:
    {
#ifdef DBD_ODBC_NO_DATASOURCES
		/*	D_imp_drh(drh);
			imp_drh->henv = SQL_NULL_HENV;
			dbd_error(drh, (RETCODE) SQL_ERROR, "data_sources: SOLID doesn't implement SQLDataSources()");*/
		XSRETURN(0);
#else
	int numDataSources = 0;
	UWORD fDirection = SQL_FETCH_FIRST;
	RETCODE rc;
        UCHAR dsn[SQL_MAX_DSN_LENGTH+1+9 /* strlen("DBI:ODBC:") */];
        SWORD dsn_length;
        UCHAR description[256];
        SWORD description_length;
	D_imp_drh(drh);

	if (!imp_drh->connects) {
	    rc = SQLAllocEnv(&imp_drh->henv);
	    if (!SQL_ok(rc)) {
		imp_drh->henv = SQL_NULL_HENV;
		dbd_error(drh, rc, "data_sources/SQLAllocEnv");
		XSRETURN(0);
	    }
	}
	strcpy(dsn, "dbi:ODBC:");
	while (1) {
            rc = SQLDataSources(imp_drh->henv, fDirection,
                                dsn+9, /* strlen("DBI:ODBC:") */
                                SQL_MAX_DSN_LENGTH,
								&dsn_length,
                                description, sizeof(description),
                                &description_length);
       	    if (!SQL_ok(rc)) {
                if (rc != SQL_NO_DATA_FOUND) {
		    /*
		     *  Temporarily increment imp_drh->connects, so
		     *  that dbd_error uses our henv.
		     */
		    imp_drh->connects++;
		    dbd_error(drh, rc, "data_sources/SQLDataSources");
		    imp_drh->connects--;
                }
                break;
            }
            ST(numDataSources++) = newSVpv(dsn, dsn_length+9 /* strlen("dbi:ODBC:") */ );
	    fDirection = SQL_FETCH_NEXT;
	}
	if (!imp_drh->connects) {
	    SQLFreeEnv(imp_drh->henv);
	    imp_drh->henv = SQL_NULL_HENV;
	}
	XSRETURN(numDataSources);
#endif /* no data sources */
    }

