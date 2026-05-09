# Executor Full Test Report

- Overall: FAIL
- Report: `ExecutorFullTestReport.md`

## Steps

| Step | Result | Detail |
|---|---|---|
| 1-CREATE DATABASE | PASS | Create database succeeded. |
| 2-USE DATABASE | PASS | Use database succeeded. |
| 3-CREATE TABLE | FAIL | Create table succeeded. |
| 4-SHOW DATABASES | PASS | rows=1 |
| 5-SHOW TABLES | PASS | rows=1 |
| 6-INSERT full | FAIL | Insert failed. |
| 7-INSERT partial | FAIL | Insert failed: invalid stoi argument |
| 8-INSERT NOT NULL fail | PASS | Insert failed: invalid stoi argument |
| 9-INSERT dup PK fail | PASS | Insert failed. |
| 10-DROP TABLE | PASS | Drop table succeeded. |
| 11-SHOW TABLES empty | PASS | rows=0 |
| 12-DROP DATABASE | PASS | Drop database succeeded. |
| 13-SHOW DATABASES after drop | PASS | rows=0 |
