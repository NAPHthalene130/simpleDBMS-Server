# Storage Full Chain Test Report

- Overall Result: FAIL
- Test Scope: CREATE DATABASE / CREATE TABLE / INSERT full chain
- Report File: `H:/CODE/DBMS/simpleDBMS-Server\src\test\StorageFullChainTestReport.md`

## Step Results

| Step | Result | Detail |
|---|---|---|
| CREATE DATABASE (basic) | PASS | type=SQL_EXEC_RESPONSE, success=true, message=Create database succeeded. |
| CREATE DATABASE (duplicate) | PASS | type=SQL_EXEC_RESPONSE, success=false, message=Database already exists. |
| USE DATABASE | PASS | dbName=test_fullchain_db, message=Use database succeeded. |
| CREATE TABLE (with PK + NOT NULL + DEFAULT) | PASS | message=Create table succeeded., tdfExists=true, trdExists=true |
| CREATE TABLE (duplicate table) | PASS | type=SQL_EXEC_RESPONSE, success=false, message=Create table failed in storage layer. |
| CREATE TABLE (no database) | PASS | type=SQL_EXEC_RESPONSE, success=false, message=No database is selected. |
| INSERT (full columns) | FAIL | message=Insert failed., success=false, affectedRows=0 |
| INSERT (partial columns) | FAIL | message=Insert failed: invalid stoi argument, success=false, affectedRows=0 |
| INSERT (duplicate primary key) | PASS | type=SQL_EXEC_RESPONSE, success=false, message=Insert failed. |
| INSERT (column count mismatch) | PASS | type=SQL_EXEC_RESPONSE, success=false, message=Insert failed. |
| INSERT (non-existent table) | PASS | type=SQL_EXEC_RESPONSE, success=false, message=Insert failed. |
| INSERT (no database selected) | PASS | type=SQL_EXEC_RESPONSE, success=false, message=Insert failed. |
| INSERT (all columns specified) | FAIL | message=Insert failed: invalid stoi argument, success=false, affectedRows=0 |
