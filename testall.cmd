
setlocal
set DBI_DSN=
nmake test
if errorlevel 1 goto errs

set DBI_DSN=dbi:ODBC:PERL_TEST_SQLSERVER
nmake test
if errorlevel 1 goto errs

set DBI_DSN=dbi:ODBC:PERL_TEST_DB2
nmake test
if errorlevel 1 goto errs

set DBI_DSN=dbi:ODBC:PERL_TEST_ACCESS
nmake test
if errorlevel 1 goto errs

set DBI_DSN=dbi:ODBC:PERL_TEST_ORACLE
nmake test
if errorlevel 1 goto errs

goto end
:errs
echo Errors running test for %DBI_DSN
:end
endlocal
