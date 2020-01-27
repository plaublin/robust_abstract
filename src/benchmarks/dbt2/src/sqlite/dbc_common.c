/*
 * This file is released under the terms of the Artistic License.  Please see
 * the file LICENSE, included in this package, for details.
 *
 * Copyright (C) 2002 Mark Wong & Open Source Development Labs, Inc.
 * Copyright (C) 2004 Alexey Stroganov & MySQL AB.
 * Copyright (C) 2008 Steve VanDeBogart & UC Regents.
 *
 */
       
#include "common.h"
#include "logging.h"
#include "sqlite_common.h"
#include <stdio.h>
#define _GNU_SOURCE
#include <string.h>


char sqlite_dbname[256] = "./dbt2.sqlite";

int commit_transaction(struct db_context_t *dbc)
{
  if (dbc->inTransaction)
  {
    if (sqlite3_exec(dbc->db, "COMMIT TRANSACTION;", NULL, NULL, NULL) != SQLITE_OK)
    {
      LOG_ERROR_MESSAGE("COMMIT TRANSACTION;\nsqlite reports: %d %s", sqlite3_errcode(dbc->db), sqlite3_errmsg(dbc->db));
      return ERROR;
    }
	dbc->inTransaction = 0;
#ifdef DEBUG_QUERY
    LOG_ERROR_MESSAGE("COMMIT TRANSACTION;\n");
#endif
  }

  return OK;
}

// This is the code for loading on-disk db into in-memory db.

int process_ddl_row(void * pData, int nColumns, char **values, char **columns);
int process_dml_row(void *pData, int nColumns, char **values, char **columns);

int dbt2_import_sqlite3_in_memory(struct db_context_t *dbc)
{
        sqlite3* budb;

        int rc = sqlite3_open(":memory:", &dbc->db);
	if (rc)
	    return rc;
        // Looks for backup.db in pwd.  For testing, you may want to
        // initialize the database to a known state.
	rc = sqlite3_open(sqlite_dbname, &budb);
	if (rc)
	    return rc;

        // Create the in-memory schema from the backup
        sqlite3_exec(budb, "BEGIN", NULL, NULL, NULL);
        sqlite3_exec(budb, "SELECT sql FROM sqlite_master WHERE sql NOT NULL",
                &process_ddl_row, dbc->db, NULL);
        sqlite3_exec(budb, "COMMIT", NULL, NULL, NULL);
        sqlite3_close(budb);

        // Attach the backup to the in memory
	char attach_str[1024];
	sprintf(attach_str, "ATTACH DATABASE '%s' as backup", sqlite_dbname);
        sqlite3_exec(dbc->db, attach_str, NULL, NULL, NULL);

        // Copy the data from the backup to the in memory
        sqlite3_exec(dbc->db, "BEGIN", NULL, NULL, NULL);
        sqlite3_exec(dbc->db, "SELECT name FROM backup.sqlite_master WHERE type='table'",
                &process_dml_row, dbc->db, NULL);
        sqlite3_exec(dbc->db, "COMMIT", NULL, NULL, NULL);

        sqlite3_exec(dbc->db, "DETACH DATABASE backup", NULL, NULL,
NULL);
	strcpy(sqlite_dbname, ":memory:");
}

/**
 * Exec an sql statement in values[0] against
 * the database in pData.
 */
int process_ddl_row(void * pData, int nColumns, char **values, char **columns)
{
        if (nColumns != 1)
                return 1; // Error

        sqlite3* db = (sqlite3*)pData;
        sqlite3_exec(db, values[0], NULL, NULL, NULL);

        return 0;
}

/**
 * Insert from a table named by backup.{values[0]}
 * into main.{values[0]} in database pData.
 */
int process_dml_row(void *pData, int nColumns, char **values, char **columns)
{
        if (nColumns != 1)
                return 1; // Error

        sqlite3* db = (sqlite3*)pData;

        char *stmt = sqlite3_mprintf("insert into main.%q "
                "select * from backup.%q", values[0], values[0]);
        sqlite3_exec(db, stmt, NULL, NULL, NULL);
        sqlite3_free(stmt);

        return 0;
}

/* Open a connection to the database. */
int _connect_to_db(struct db_context_t *dbc)
{
	int rc;

	rc = dbt2_import_sqlite3_in_memory(dbc);
	dbc->inTransaction = 0;
	if (rc) {
        LOG_ERROR_MESSAGE("Connection to database '%s' failed (error code %d).", sqlite_dbname, rc);
		sqlite3_close(dbc->db);
		return ERROR;
	}

    return OK;
}

/* Disconnect from the database and free the connection handle. */
int _disconnect_from_db(struct db_context_t *dbc)
{
	sqlite3_close(dbc->db);
	return OK;
}

int _db_init(char * _dbname)
{
	/* Copy values only if it's not NULL. */
	if (_dbname != NULL) {
		strcpy(sqlite_dbname, _dbname);
	}
	return OK;
}

int rollback_transaction(struct db_context_t *dbc)
{
  LOG_ERROR_MESSAGE("ROLLBACK INITIATED\n");

  if (dbc->inTransaction)
  {
    if (sqlite3_exec(dbc->db, "ROLLBACK TRANSACTION;", NULL, NULL, NULL) != SQLITE_OK)
    {
      LOG_ERROR_MESSAGE("ROLLBACK TRANSACTION;\nsqlite reports: %d %s", sqlite3_errcode(dbc->db), sqlite3_errmsg(dbc->db));
      return ERROR;
    }
	dbc->inTransaction = 0;
  }

  return STATUS_ROLLBACK;
}

int dbt2_sql_execute(struct db_context_t *dbc, char * query, struct sql_result_t * sql_result, 
                       char * query_name)
{
  sql_result->error = 0;
  sql_result->query_running = 0;
  sql_result->prefetched = 0;

  if (!dbc->inTransaction)
  {
    if (sqlite3_exec(dbc->db, "BEGIN TRANSACTION;", NULL, NULL, NULL) != SQLITE_OK)
    {
      LOG_ERROR_MESSAGE("%s: BEGIN TRANSACTION;\nsqlite reports: %d %s", query_name, sqlite3_errcode(dbc->db), sqlite3_errmsg(dbc->db));
      return 0;
    }
	dbc->inTransaction = 1;
  }

  if (sqlite3_prepare_v2(dbc->db, query, strlen(query)+1, &sql_result->pStmt, NULL) != SQLITE_OK)
  {
    LOG_ERROR_MESSAGE("%s: %s\nsqlite reports: %d %s",query_name, query,
							sqlite3_errcode(dbc->db), sqlite3_errmsg(dbc->db));
    sqlite3_finalize(sql_result->pStmt);
    return 0;
  }
  sql_result->num_fields = sqlite3_column_count(sql_result->pStmt);
  sql_result->query_running = 1;
  dbt2_sql_fetchrow(dbc, sql_result);
  if (sql_result->error)
  {
    return 0;
  }
  sql_result->prefetched = 1;
  return 1;
}

int dbt2_sql_fetchrow(struct db_context_t *dbc, struct sql_result_t * sql_result)
{
  int rc;

  if (!sql_result->query_running)
  {
    return 0;
  }

  if (sql_result->prefetched)
  {
    sql_result->prefetched = 0;
    return 1;
  }

  rc = sqlite3_step(sql_result->pStmt);

  if (rc == SQLITE_ROW)
  {
    return 1;
  }
  else if (rc == SQLITE_DONE)
  {
    rc = sqlite3_finalize(sql_result->pStmt);
	if (rc != SQLITE_OK)
	{
      sql_result->error = 1;
	}
    sql_result->query_running = 0;
    return 0;
  }
  else
  {
    LOG_ERROR_MESSAGE("SQLITE error %d: %s\n", rc, sqlite3_errmsg(dbc->db));
    sql_result->error = 1;
    sqlite3_finalize(sql_result->pStmt);
    sql_result->query_running = 0;
	return 0;
  }
}

int dbt2_sql_close_cursor(struct db_context_t *dbc, struct sql_result_t * sql_result)
{
  int rc;

  if (sql_result->query_running)
  {
    sql_result->query_running = 0;
    rc = sqlite3_finalize(sql_result->pStmt);
	if (rc != SQLITE_OK || sql_result->error)
	{
      return 0;
    }
  }

  return 1;
}


char * dbt2_sql_getvalue(struct db_context_t *dbc, struct sql_result_t * sql_result, int field)
{
  char * tmp;
  
  tmp= NULL;
  if (sql_result->query_running && field < sql_result->num_fields)
  {
    tmp = strndup(sqlite3_column_text(sql_result->pStmt, field), sqlite3_column_bytes(sql_result->pStmt, field));
    if (!tmp)
    {
#ifdef DEBUG_QUERY
      LOG_ERROR_MESSAGE("dbt2_sql_getvalue: var[%d]=NULL\n", field);
#endif
    }
  }
  else
  {
#ifdef DEBUG_QUERY
    LOG_ERROR_MESSAGE("dbt2_sql_getvalue: POSSIBLE NULL VALUE or ERROR\n\Query: %s\nField: %d from %d", 
                       sqlite3_sql(sql_result->pStmt), field, sql_result->num_fields);
#endif
  }

  return tmp;
}

