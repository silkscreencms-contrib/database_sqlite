<?php

/**
 * @file
 * Select builder for SQLite embedded database engine.
 */

/**
 * @addtogroup database
 * @{
 */

/**
 * SQLite specific query builder for SELECT statements.
 */
class SelectQuery_sqlite extends SelectQuery {
  public function forUpdate($set = TRUE) {
    // SQLite does not support FOR UPDATE so nothing to do.
    return $this;
  }
}

class_alias('SelectQuery_sqlite', 'SelectQuery_sqlite3');

/**
 * @} End of "addtogroup database".
 */


