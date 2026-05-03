# Network SQL Flow Test Report

- Overall Result: FAIL
- Test Scope: NetworkTransferData SQL request/response full flow in server network layer
- Report File: `H:/CODE/DBMS/simpleDBMS-Server\src\test\NetworkSqlFlowTestReport.md`

## Step Results

| Step | Result | Detail |
|---|---|---|
| CREATE DATABASE | PASS | type=SQL_EXEC_RESPONSE, message=Create database succeeded. |
| USE DATABASE | PASS | type=SQL_EXEC_RESPONSE, message=Use database succeeded. |
| SHOW DATABASE | PASS | type=SQL_QUERY_RESPONSE, success=true, message=SHOW DATABASE <name> executed in stub mode., columns=name, rowCount=1 |
| CREATE TABLE | FAIL | type=SQL_EXEC_RESPONSE, message=Create table failed in storage layer., tdfExists=false |
