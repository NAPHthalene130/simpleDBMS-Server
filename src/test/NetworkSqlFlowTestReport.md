# Network SQL Flow Test Report

- Overall Result: FAIL
- Test Scope: NetworkTransferData SQL request/response full flow in server network layer
- Report File: `F:/20570/DBMS1/simpleDBMS-Server\src\test\NetworkSqlFlowTestReport.md`

## Step Results

| Step | Result | Detail |
|---|---|---|
| CREATE DATABASE | PASS | type=SQL_EXEC_RESPONSE, message=Create database succeeded. |
| USE DATABASE | PASS | type=SQL_EXEC_RESPONSE, message=Use database succeeded. |
| SHOW DATABASE | PASS | type=SQL_EXEC_RESPONSE, success=true, message=SHOW DATABASE <name> executed in stub mode., columns=name, rowCount=1 |
| CREATE TABLE | PASS | type=SQL_EXEC_RESPONSE, message=Create table succeeded., tdfExists=true |
| INSERT ROW 1 | FAIL | type=SQL_EXEC_RESPONSE, message=InsertExecutor is registered, but execution logic is not implemented yet. |
| INSERT ROW 2 | FAIL | type=SQL_EXEC_RESPONSE, message=InsertExecutor is registered, but execution logic is not implemented yet. |
| SELECT ALL | FAIL | type=SQL_EXEC_RESPONSE, success=false, columns=col1,col2, rowCount=0, message=Select statement target fields are invalid. |
| SELECT WHERE | FAIL | type=SQL_EXEC_RESPONSE, success=false, columns=col1, rowCount=0, message=Select statement target fields are invalid. |
| SELECT LIKE | FAIL | type=SQL_EXEC_RESPONSE, success=false, columns=, rowCount=0, message=Parse failed at token 6: Illegal or missing comparison operator in predicate. |
