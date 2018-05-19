<?php

/**
 * @file
 * Database interface code for SQLite embedded database engine.
 */

/**
 * @addtogroup database
 * @{
 */

include_once BACKDROP_ROOT . '/core/includes/database/prefetch.inc';

/**
 * Specific SQLite implementation of DatabaseConnection.
 */
class DatabaseConnection_sqlite extends DatabaseConnection {

  /**
   * Whether this database connection supports savepoints.
   *
   * Version of sqlite lower then 3.6.8 can't use savepoints.
   * See http://www.sqlite.org/releaselog/3_6_8.html
   *
   * @var boolean
   */
  protected $savepointSupport = FALSE;

  /**
   * Whether or not the active transaction (if any) will be rolled back.
   *
   * @var boolean
   */
  protected $willRollback;

  /**
   * All databases attached to the current database. This is used to allow
   * prefixes to be safely handled without locking the table
   *
   * @var array
   */
  protected $attachedDatabases = array();

  /**
   * Local timezone
   */
  protected $local_tz = 'utc';

  /**
   * Whether or not a table has been dropped this request: the destructor will
   * only try to get rid of unnecessary databases if there is potential of them
   * being empty.
   *
   * This variable is set to public because DatabaseSchema_sqlite needs to
   * access it. However, it should not be manually set.
   *
   * @var boolean
   */
  var $tableDropped = FALSE;

  public function __construct(array $connection_options = array()) {
    // We don't need a specific PDOStatement class here, we simulate it below.
    $this->statementClass = NULL;

    // This driver defaults to transaction support, except if explicitly passed FALSE.
    $this->transactionSupport = $this->transactionalDDLSupport = !isset($connection_options['transactions']) || $connection_options['transactions'] !== FALSE;

    $this->connectionOptions = $connection_options;

    // Allow PDO options to be overridden.
    $connection_options += array(
      'pdo' => array(),
    );
    $connection_options['pdo'] += array(
      // Convert numeric values to strings when fetching.
      PDO::ATTR_STRINGIFY_FETCHES => TRUE,
    );
    parent::__construct('sqlite:' . $connection_options['database'], '', '', $connection_options['pdo']);

    // Attach one database for each registered prefix.
    $prefixes = $this->prefixes;
    foreach ($prefixes as $table => &$prefix) {
      // Empty prefix means query the main database -- no need to attach anything.
      if (!empty($prefix)) {
        // Only attach the database once.
        if (!isset($this->attachedDatabases[$prefix])) {
          $this->attachedDatabases[$prefix] = $prefix;
          $this->query('ATTACH DATABASE :database AS :prefix', array(':database' => $connection_options['database'] . '-' . $prefix, ':prefix' => $prefix));
        }

        // Add a ., so queries become prefix.table, which is proper syntax for
        // querying an attached database.
        $prefix .= '.';
      }
    }
    // Regenerate the prefixes replacement table.
    $this->setPrefix($prefixes);

    // Detect support for SAVEPOINT.
    $version = $this->query('SELECT sqlite_version()')->fetchField();
    $this->savepointSupport = (version_compare($version, '3.6.8') >= 0);

    // Create functions needed by SQLite.
    $this->sqliteCreateFunction('if', array($this, 'sqlFunctionIf'));
    $this->sqliteCreateFunction('greatest', array($this, 'sqlFunctionGreatest'));
    $this->sqliteCreateFunction('pow', 'pow', 2);
    $this->sqliteCreateFunction('length', 'strlen', 1);
    $this->sqliteCreateFunction('md5', 'md5', 1);
    $this->sqliteCreateFunction('concat', array($this, 'sqlFunctionConcat'));
    $this->sqliteCreateFunction('concat_ws', array($this, 'sqlFunctionConcatWs'));
    $this->sqliteCreateFunction('substring', array($this, 'sqlFunctionSubstring'), 3);
    $this->sqliteCreateFunction('substring_index', array($this, 'sqlFunctionSubstringIndex'), 3);
    $this->sqliteCreateFunction('rand', array($this, 'sqlFunctionRand'));
    $this->sqliteCreateFunction('regexp', array($this, 'sqlFunctionRegexp'));

    // SQLite does not support the LIKE BINARY operator, so we overload the
    // non-standard GLOB operator for case-sensitive matching. Another option
    // would have been to override another non-standard operator, MATCH, but
    // that does not support the NOT keyword prefix.
    $this->sqliteCreateFunction('glob', array(__CLASS__, 'sqlFunctionLikeBinary'));

    // Set SQLite init_commands if not already defined. Enable the Write-Ahead
    // Logging (WAL) for SQLite. See https://www.drupal.org/node/2348137 and
    // https://www.sqlite.org/wal.html.
    $connection_options += array(
      'init_commands' => array(),
    );
    $connection_options['init_commands'] += array(
      'wal' => "PRAGMA journal_mode=WAL",
      'timeout' => "PRAGMA busy_timeout=3000",
    );

    // Execute sqlite init_commands.
    if (isset($connection_options['init_commands'])) {
      $this->exec(implode('; ', $connection_options['init_commands']));
    }
  }

  /**
   * Destructor for the SQLite connection.
   *
   * We prune empty databases on destruct, but only if tables have been
   * dropped. This is especially needed when running the test suite, which
   * creates and destroy databases several times in a row.
   */
  public function __destruct() {
    if ($this->tableDropped && !empty($this->attachedDatabases)) {
      foreach ($this->attachedDatabases as $prefix) {
        // Check if the database is now empty, ignore the internal SQLite tables.
        try {
          $count = $this->query('SELECT COUNT(*) FROM ' . $prefix . '.sqlite_master WHERE type = :type AND name NOT LIKE :pattern', array(':type' => 'table', ':pattern' => 'sqlite_%'))->fetchField();

          // We can prune the database file if it doesn't have any tables.
          if ($count == 0) {
            // Detach the database.
            $this->query('DETACH DATABASE :schema', array(':schema' => $prefix));
            // Destroy the database file.
            unlink($this->connectionOptions['database'] . '-' . $prefix);
          }
        }
        catch (Exception $e) {
          // Ignore the exception and continue. There is nothing we can do here
          // to report the error or fail safe.
        }
      }
    }
  }

  /**
   * Gets all the attached databases.
   *
   * @return array
   *   An array of attached database names.
   *
   * @see DatabaseConnection_sqlite::__construct().
   */
  public function getAttachedDatabases() {
    return $this->attachedDatabases;
  }

  /**
   * SQLite compatibility implementation for the IF() SQL function.
   */
  public function sqlFunctionIf($condition, $expr1, $expr2 = NULL) {
    return $condition ? $expr1 : $expr2;
  }

  /**
   * SQLite compatibility implementation for the GREATEST() SQL function.
   */
  public function sqlFunctionGreatest() {
    $args = func_get_args();
    foreach ($args as $k => $v) {
      if (!isset($v)) {
        unset($args);
      }
    }
    if (count($args)) {
      return max($args);
    }
    else {
      return NULL;
    }
  }

  /**
   * SQLite compatibility implementation for the CONCAT() SQL function.
   */
  public function sqlFunctionConcat() {
    $args = func_get_args();
    return implode('', $args);
  }

  /**
   * SQLite compatibility implementation for the CONCAT_WS() SQL function.
   *
   * @see http://dev.mysql.com/doc/refman/5.6/en/string-functions.html#function_concat-ws
   */
  public static function sqlFunctionConcatWs() {
    $args = func_get_args();
    $separator = array_shift($args);
    // If the separator is NULL, the result is NULL.
    if ($separator === FALSE || is_null($separator)) {
      return NULL;
    }
    // Skip any NULL values after the separator argument.
    $args = array_filter($args, function ($value) {
      return !is_null($value);
    });
    return implode($separator, $args);
  }

  /**
   * SQLite compatibility implementation for the SUBSTRING() SQL function.
   */
  public function sqlFunctionSubstring($string, $from, $length) {
    return substr($string, $from - 1, $length);
  }

  /**
   * SQLite compatibility implementation for the SUBSTRING_INDEX() SQL function.
   */
  public function sqlFunctionSubstringIndex($string, $delimiter, $count) {
    // If string is empty, simply return an empty string.
    if (empty($string)) {
      return '';
    }
    $end = 0;
    for ($i = 0; $i < $count; $i++) {
      $end = strpos($string, $delimiter, $end + 1);
      if ($end === FALSE) {
        $end = strlen($string);
      }
    }
    return substr($string, 0, $end);
  }

  /**
   * SQLite compatibility implementation for the RAND() SQL function.
   */
  public function sqlFunctionRand($seed = NULL) {
    if (isset($seed)) {
      mt_srand($seed);
    }
    return mt_rand() / mt_getrandmax();
  }

  /**
   * SQLite compatibility implementation for the REGEXP SQL operator.
   *
   * The REGEXP operator is natively known, but not implemented by default.
   *
   * @see http://www.sqlite.org/lang_expr.html#regexp
   */
  public static function sqlFunctionRegexp($string, $pattern) {
    // preg_quote() cannot be used here, since $pattern may contain reserved
    // regular expression characters already (such as ^, $, etc). Therefore,
    // use a rare character as PCRE delimiter.
    $pattern = '#' . addcslashes($pattern, '#') . '#i';
    return preg_match($pattern, $string);
  }

   /**
    * SQLite compatibility implementation for the LIKE BINARY SQL operator.
    *
    * SQLite supports case-sensitive LIKE operations through the
    * 'case_sensitive_like' PRAGMA statement, but only for ASCII characters, so
    * we have to provide our own implementation with UTF-8 support.
    *
    * @see https://sqlite.org/pragma.html#pragma_case_sensitive_like
    * @see https://sqlite.org/lang_expr.html#like
    */
  public static function sqlFunctionLikeBinary($pattern, $subject) {
    // Replace the SQL LIKE wildcard meta-characters with the equivalent regular
    // expression meta-characters and escape the delimiter that will be used for
    // matching.
    $pattern = str_replace(array('%', '_'), array('.*?', '.'), preg_quote($pattern, '/'));
    return preg_match('/^' . $pattern . '$/', $subject);
  }

  /**
   * SQLite-specific implementation of DatabaseConnection::prepare().
   *
   * We don't use prepared statements at all at this stage. We just create
   * a DatabaseStatement_sqlite object, that will create a PDOStatement
   * using the semi-private PDOPrepare() method below.
   */
  public function prepare($query, $options = array()) {
    return new DatabaseStatement_sqlite($this, $query, $options);
  }

  /**
   * NEVER CALL THIS FUNCTION: YOU MIGHT DEADLOCK YOUR PHP PROCESS.
   *
   * This is a wrapper around the parent PDO::prepare method. However, as
   * the PDO SQLite driver only closes SELECT statements when the PDOStatement
   * destructor is called and SQLite does not allow data change (INSERT,
   * UPDATE etc) on a table which has open SELECT statements, you should never
   * call this function and keep a PDOStatement object alive as that can lead
   * to a deadlock. This really, really should be private, but as
   * DatabaseStatement_sqlite needs to call it, we have no other choice but to
   * expose this function to the world.
   */
  public function PDOPrepare($query, array $options = array()) {
    return parent::prepare($query, $options);
  }

  public function queryRange($query, $from, $count, array $args = array(), array $options = array()) {
    return $this->query($query . ' LIMIT ' . (int) $from . ', ' . (int) $count, $args, $options);
  }

  public function queryTemporary($query, array $args = array(), array $options = array()) {
    // Generate a new temporary table name and protect it from prefixing.
    // SQLite requires that temporary tables to be non-qualified.
    $tablename = $this->generateTemporaryTableName();
    $prefixes = $this->prefixes;
    $prefixes[$tablename] = '';
    $this->setPrefix($prefixes);

    $this->query('CREATE TEMPORARY TABLE ' . $tablename . ' AS ' . $query, $args, $options);
    return $tablename;
  }

  public function driver() {
    return 'sqlite';
  }

  public function databaseType() {
    return 'sqlite';
  }

  /**
   * Overrides DatabaseConnection::createDatabase().
   *
   * @param string $database
   *   The name of the database to create.databaseType
   *
   * @throws DatabaseNotFoundException
   */
  public function createDatabase($database) {
    throw new DatabaseOperationNotSupported('Unsupported operation.');
  }

  public function mapConditionOperator($operator) {
    // We don't want to override any of the defaults.
    static $specials = array(
      'LIKE' => array('postfix' => " ESCAPE '\\'"),
      'NOT LIKE' => array('postfix' => " ESCAPE '\\'"),
      'LIKE BINARY' => array('postfix' => " ESCAPE '\\'", 'operator' => 'GLOB'),
      'NOT LIKE BINARY' => array('postfix' => " ESCAPE '\\'", 'operator' => 'NOT GLOB'),
    );
    return isset($specials[$operator]) ? $specials[$operator] : NULL;
  }

  public function prepareQuery($query) {
    return $this->prepare($this->prefixTables($query));
  }

  public function nextId($existing_id = 0) {
    $transaction = $this->startTransaction();
    // We can safely use literal queries here instead of the slower query
    // builder because if a given database breaks here then it can simply
    // override nextId. However, this is unlikely as we deal with short strings
    // and integers and no known databases require special handling for those
    // simple cases. If another transaction wants to write the same row, it will
    // wait until this transaction commits.
    $stmt = $this->query('UPDATE {sequences} SET value = GREATEST(value, :existing_id) + 1', array(
      ':existing_id' => $existing_id,
    ));
    if (!$stmt->rowCount()) {
      $this->query('INSERT INTO {sequences} (value) VALUES (:existing_id + 1)', array(
        ':existing_id' => $existing_id,
      ));
    }
    // The transaction gets committed when the transaction object gets destroyed
    // because it gets out of scope.
    return $this->query('SELECT value FROM {sequences}')->fetchField();
  }

  public function rollback($savepoint_name = 'drupal_transaction') {
    if ($this->savepointSupport) {
      return parent::rollBack($savepoint_name);
    }

    if (!$this->inTransaction()) {
      throw new DatabaseTransactionNoActiveException();
    }
    // A previous rollback to an earlier savepoint may mean that the savepoint
    // in question has already been rolled back.
    if (!in_array($savepoint_name, $this->transactionLayers)) {
      return;
    }

    // We need to find the point we're rolling back to, all other savepoints
    // before are no longer needed.
    while ($savepoint = array_pop($this->transactionLayers)) {
      if ($savepoint == $savepoint_name) {
        // Mark whole stack of transactions as needed roll back.
        $this->willRollback = TRUE;
        // If it is the last the transaction in the stack, then it is not a
        // savepoint, it is the transaction itself so we will need to roll back
        // the transaction rather than a savepoint.
        if (empty($this->transactionLayers)) {
          break;
        }
        return;
      }
    }
    if ($this->supportsTransactions()) {
      PDO::rollBack();
    }
  }

  public function pushTransaction($name) {
    if ($this->savepointSupport) {
      return parent::pushTransaction($name);
    }
    if (!$this->supportsTransactions()) {
      return;
    }
    if (isset($this->transactionLayers[$name])) {
      throw new DatabaseTransactionNameNonUniqueException($name . " is already in use.");
    }
    if (!$this->inTransaction()) {
      PDO::beginTransaction();
    }
    $this->transactionLayers[$name] = $name;
  }

  public function popTransaction($name) {
    if ($this->savepointSupport) {
      return parent::popTransaction($name);
    }
    if (!$this->supportsTransactions()) {
      return;
    }
    if (!$this->inTransaction()) {
      throw new DatabaseTransactionNoActiveException();
    }

    // Commit everything since SAVEPOINT $name.
    while($savepoint = array_pop($this->transactionLayers)) {
      if ($savepoint != $name) continue;

      // If there are no more layers left then we should commit or rollback.
      if (empty($this->transactionLayers)) {
        // If there was any rollback() we should roll back whole transaction.
        if ($this->willRollback) {
          $this->willRollback = FALSE;
          PDO::rollBack();
        }
        elseif (!PDO::commit()) {
          throw new DatabaseTransactionCommitFailedException();
        }
      }
      else {
        break;
      }
    }
  }

  public function utf8mb4IsActive() {
    return TRUE;
  }

  public function utf8mb4IsSupported() {
    return TRUE;
  }

  /**
   * Set the session timezone by offset.
   */
  public function setSessionTimezoneOffset($offset) {
    if (!in_array($offset, array('utc', 'UTC')) && (!preg_match('/[+-]?\d\d(:\d\d)?/', $offset))) {
      return FALSE;
    }
    $this->local_tz = $offset;
  }

  public function hasTimezoneSupport() {
    return TRUE;
  }

  public function dateFieldSql($field, $field_type = DATE_UNIX) {
    $local_tz = $this->local_tz;
    switch ($field_type) {
      case DATE_UNIX:
        $field = "datetime($field, 'unixepoch', '$local_tz')";
        break;

      case DATE_ISO:
      case DATE_DATETIME:
        $field = "datetime($field, '$local_tz')";
        break;
    }

    return $field;
  }

//  abstract public function dateOffsetSql($field, $offset);

  public function dateMathSql($field, $direction, $count, $granularity) {
    $granularity .= 'S';
    switch ($direction) {
      case 'ADD':
        return "datetime($field, '+$count $granularity')";

      case 'SUB':
        return "datetime($field, '-$count $granularity')";
    }
    return $field;
  }

  public function dateConvertTimezoneSql($field, $source, $target) {
    throw new DatabaseOperationNotSupported('Database does not convert timezones.');
  }

  /**
   * Format a date field.
   *
   * @param string $format
   *   A format string for the result, like 'Y-m-d H:i:s' .
   * @param string $field
   *   The real table and field name, like 'tablename.fieldname' .
   *
   * @return string
   *   An appropriate SQL string for the db type and field type.
   */
  public function dateFormatSql($field, $format) {
    $replace = array(
      // 4 digit year number.
      'Y' => '%Y',
      // No format for 2 digit year number.
      'y' => '%Y',
      // No format for 3 letter month name.
      'M' => '%m',
      // Month number with leading zeros.
      'm' => '%m',
      // No format for month number without leading zeros.
      'n' => '%m',
      // No format for full month name.
      'F' => '%m',
      // No format for 3 letter day name.
      'D' => '%d',
      // Day of month number with leading zeros.
      'd' => '%d',
      // No format for full day name.
      'l' => '%d',
      // No format for day of month number without leading zeros.
      'j' => '%d',
      // ISO week number.
      'W' => '%W',
      // 24 hour hour with leading zeros.
      'H' => '%H',
      // No format for 12 hour hour with leading zeros.
      'h' => '%H',
      // Minutes with leading zeros.
      'i' => '%M',
      // Seconds with leading zeros.
      's' => '%S',
      // No format for AM/PM.
      'A' => '',
      // Week number.
      '\WW' => '',
    );
    $format = strtr($format, $replace);
    return "strftime('$format', $field)";
  }

  /**
   * Extract part of a date from a date field.
   *
   * @param string $extract_type
   *   The type of value to extract from the date, like 'MONTH'.
   * @param string $field
   *   The real table and field name, like 'tablename.fieldname'.
   *
   * @return string
   *   An appropriate SQL string for the db type and field type.
   */
  public function dateExtractSql($field, $extract_type) {
//    throw new DatabaseOperationNotSupported('dateExtractSql not yet implemented.');
    switch (strtoupper($extract_type)) {
      case 'DATE':
        return $field;
      case 'YEAR':
        return "strftime('%Y', datetime($field, 'unixepoch'))";
      case 'MONTH':
        return "strftime('%m', datetime($field, 'unixepoch'))";
      case 'DAY':
        return "strftime('%d', datetime($field, 'unixepoch'))";
      case 'HOUR':
        return "strftime('%H', datetime($field, 'unixepoch')')";
      case 'MINUTE':
        return "strftime('%M', datetime($field, 'unixepoch'))";
      case 'SECOND':
        return "strftime('%S', datetime($field, 'unixepoch'))";

      // ISO week number for date.
      case 'WEEK':
        return "strftime('%W', datetime($field, 'unixepoch'))";
      case 'DOW':
        return "cast(strftime('%w', $field) as INTEGER)";
      case 'DOY':
        return "julianday(datetime($field, 'startofday'))";
    }

    // An unknown function.
    return FALSE;
  }

}

/**
 * Specific SQLite implementation of DatabaseConnection.
 *
 * See DatabaseConnection_sqlite::PDOPrepare() for reasons why we must prefetch
 * the data instead of using PDOStatement.
 *
 * @see DatabaseConnection_sqlite::PDOPrepare()
 */
class DatabaseStatement_sqlite extends DatabaseStatementPrefetch implements Iterator, DatabaseStatementInterface {

  /**
   * SQLite specific implementation of getStatement().
   *
   * The PDO SQLite layer doesn't replace numeric placeholders in queries
   * correctly, and this makes numeric expressions (such as COUNT(*) >= :count)
   * fail. We replace numeric placeholders in the query ourselves to work
   * around this bug.
   *
   * See http://bugs.php.net/bug.php?id=45259 for more details.
   */
  protected function getStatement($query, &$args = array()) {
    if (count($args)) {
      // Check if $args is a simple numeric array.
      if (range(0, count($args) - 1) === array_keys($args)) {
        // In that case, we have unnamed placeholders.
        $count = 0;
        $new_args = array();
        foreach ($args as $value) {
          if (is_float($value) || is_int($value)) {
            if (is_float($value)) {
              // Force the conversion to float so as not to loose precision
              // in the automatic cast.
              $value = sprintf('%F', $value);
            }
            $query = substr_replace($query, $value, strpos($query, '?'), 1);
          }
          else {
            $placeholder = ':db_statement_placeholder_' . $count++;
            $query = substr_replace($query, $placeholder, strpos($query, '?'), 1);
            $new_args[$placeholder] = $value;
          }
        }
        $args = $new_args;
      }
      else {
        // Else, this is using named placeholders.
        foreach ($args as $placeholder => $value) {
          if (is_float($value) || is_int($value)) {
            if (is_float($value)) {
              // Force the conversion to float so as not to loose precision
              // in the automatic cast.
              $value = sprintf('%F', $value);
            }

            // We will remove this placeholder from the query as PDO throws an
            // exception if the number of placeholders in the query and the
            // arguments does not match.
            unset($args[$placeholder]);
            // PDO allows placeholders to not be prefixed by a colon. See
            // http://marc.info/?l=php-internals&m=111234321827149&w=2 for
            // more.
            if ($placeholder[0] != ':') {
              $placeholder = ":$placeholder";
            }
            // When replacing the placeholders, make sure we search for the
            // exact placeholder. For example, if searching for
            // ':db_placeholder_1', do not replace ':db_placeholder_11'.
            $query = preg_replace('/' . preg_quote($placeholder) . '\b/', $value, $query);
          }
        }
      }
    }

    return $this->dbh->PDOPrepare($query);
  }

  public function execute($args = array(), $options = array()) {
    try {
      $return = parent::execute($args, $options);
    }
    catch (PDOException $e) {
      if (!empty($e->errorInfo[1]) && $e->errorInfo[1] === 17) {
        // The schema has changed. SQLite specifies that we must resend the query.
        $return = parent::execute($args, $options);
      }
      else {
        // Rethrow the exception.
        throw $e;
      }
    }

    // In some weird cases, SQLite will prefix some column names by the name
    // of the table. We post-process the data, by renaming the column names
    // using the same convention as MySQL and PostgreSQL.
    $rename_columns = array();
    foreach ($this->columnNames as $k => $column) {
      // In some SQLite versions, SELECT DISTINCT(field) will return "(field)"
      // instead of "field".
      if (preg_match("/^\((.*)\)$/", $column, $matches)) {
        $rename_columns[$column] = $matches[1];
        $this->columnNames[$k] = $matches[1];
        $column = $matches[1];
      }

      // Remove "table." prefixes.
      if (preg_match("/^.*\.(.*)$/", $column, $matches)) {
        $rename_columns[$column] = $matches[1];
        $this->columnNames[$k] = $matches[1];
      }
    }
    if ($rename_columns) {
      // DatabaseStatementPrefetch already extracted the first row,
      // put it back into the result set.
      if (isset($this->currentRow)) {
        $this->data[0] = &$this->currentRow;
      }

      // Then rename all the columns across the result set.
      foreach ($this->data as $k => $row) {
        foreach ($rename_columns as $old_column => $new_column) {
          $this->data[$k][$new_column] = $this->data[$k][$old_column];
          unset($this->data[$k][$old_column]);
        }
      }

      // Finally, extract the first row again.
      $this->currentRow = $this->data[0];
      unset($this->data[0]);
    }

    return $return;
  }
}

/**
 * @} End of "addtogroup database".
 */
