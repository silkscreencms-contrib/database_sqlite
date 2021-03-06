<?php

/**
 * @file
 * Query code for SQLite embedded database engine.
 */

/**
 * @addtogroup database
 * @{
 */

/**
 * SQLite specific implementation of InsertQuery.
 *
 * We ignore all the default fields and use the clever SQLite syntax:
 *   INSERT INTO table DEFAULT VALUES
 * for degenerated "default only" queries.
 */
class InsertQuery_sqlite extends InsertQuery {

  public function execute() {
    if (!$this->preExecute()) {
      return NULL;
    }
    if (count($this->insertFields) || !empty($this->fromQuery)) {
      return parent::execute();
    }
    else {
      return $this->connection->query('INSERT INTO {' . $this->table . '} DEFAULT VALUES', array(), $this->queryOptions);
    }
  }

  public function __toString() {
    // Create a sanitized comment string to prepend to the query.
    $comments = $this->connection->makeComment($this->comments);

    // Produce as many generic placeholders as necessary.
    $placeholders = array();
    if (!empty($this->insertFields)) {
      $placeholders = array_fill(0, count($this->insertFields), '?');
    }

    // If we're selecting from a SelectQuery, finish building the query and
    // pass it back, as any remaining options are irrelevant.
    if (!empty($this->fromQuery)) {
      $insert_fields_string = $this->insertFields ? ' (' . implode(', ', $this->insertFields) . ') ' : ' ';
      return $comments . 'INSERT INTO {' . $this->table . '}' . $insert_fields_string . $this->fromQuery;
    }

    return $comments . 'INSERT INTO {' . $this->table . '} (' . implode(', ', $this->insertFields) . ') VALUES (' . implode(', ', $placeholders) . ')';
  }

}

class_alias('InsertQuery_sqlite', 'InsertQuery_sqlite3');

/**
 * SQLite specific implementation of DeleteQuery.
 */
class DeleteQuery_sqlite extends DeleteQuery {
  // When the WHERE is omitted from a DELETE statement and the table being
  // deleted has no triggers, SQLite uses an optimization to erase the entire
  // table content without having to visit each row of the table individually.
  // Prior to SQLite 3.6.5, SQLite does not return the actual number of rows
  // deleted by that optimized "truncate" optimization. But we want to return
  // the number of rows affected, so we calculate it directly.
  public function execute() {
    if (!count($this->condition)) {
      $total_rows = $this->connection->query('SELECT COUNT(*) FROM {' . $this->connection->escapeTable($this->table) . '}')->fetchField();
      parent::execute();
      return $total_rows;
    }
    else {
      return parent::execute();
    }
  }
}

class_alias('DeleteQuery_sqlite', 'DeleteQuery_sqlite3');

/**
 * SQLite specific implementation of TruncateQuery.
 *
 * SQLite doesn't support TRUNCATE, but a DELETE query with no condition has
 * exactly the effect (it is implemented by DROPing the table).
 */
class TruncateQuery_sqlite extends TruncateQuery {
  public function __toString() {
    // Create a sanitized comment string to prepend to the query.
    $comments = $this->connection->makeComment($this->comments);

    return $comments . 'DELETE FROM {' . $this->connection->escapeTable($this->table) . '} ';
  }
}

class_alias('TruncateQuery_sqlite', 'TruncateQuery_sqlite3');

/**
 * @} End of "addtogroup database".
 */
