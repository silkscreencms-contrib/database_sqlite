SQLite Database Driver (database_sqlite)
===========================================

This driver contains the SQLite database driver for Silkscreen CMS.

To enable this driver, place it in the `drivers` directory in the root of the
Silkscreen CMS site or in the sites/[yoursite]/drivers directory.

Compatibility
-------------

When running with Silkscreen CMS 1.18 or later, the database_sqlite 1.18 or
later driver must be used.  Silkscreen CMS 1.18.1 introduces new API calls in
the database connection object that SQLite must override to work correctly.

Limitations
-----------

The SQLite driver has some limitations that keep it from being fully compatible
with the other Silkscreen database drivers.

* REGEXP BINARY is not supported by SQLite. The case-insensitive REGEXP is used
  instead.
* SQLite ignores the length specifier in fields. See the [SQLite3
  documentation](https://www.sqlite.org/datatype3.html#affinity_name_examples)
  for details.
* Under load, the database driver may return "Database locked" errors.
* This driver requires SQLite 3.7 or higher.

Database Connection URL
-----------------------
The database URL for the SQLite driver doesn't require a host or username, as
they don't make any sense for the connection.  The connection URL can point to a
relative or absolute path.  For relative paths, use:

```php
    $database = 'sqlite://files/myDatabase.sqlite';
```

For absolute paths, include a third slash after the colon:

```php
    $database = 'sqlite:///path/to/myDatabase.sqlite';
```

Test Suite Issues
-----------------

Several tests don't pass the test suite.

 - Select tests (DatabaseSelectTestCase)
   - testSimpleComment
   - testVulnerableComment
 - Field SQL Storage tests (FieldSqlStorageTestCase)
   - testFieldUpdateFailure
 - File uploading transliteration (FileUploadTransliterationTest)
   - testTransliteration
 - Config settings form (SearchConfigSettingsForm)
   - testSearchNodeTypes
 - Taxonomy term functions and forms (TaxonomyTermTestCase)
   - testTaxonomyGetTermByName
 - Views taxonomy relationship (TaxonomyViewsHandlerRelationshipNodeTermDataTest)
   - testViewsHandlerRelationshipNodeTermData
 - Filter: Multiple conditions (ViewsHandlerFilterMultipleTest)
   - testMultipleFilters
 - Filter: String (ViewsHandlerFilterStringTest)
   - testFilterStringGroupedExposedLonger
   - testFilterStringGroupedExposedShorter
   - testFilterStringLonger
   - testFilterStringShorter
 - Views UI wizard jump menu functionality (ViewsUIWizardJumpMenuTestCase)
   - testJumpMenus

Three of them have issues opened already:

* Issue #9: Field API: Field SQL Storage tests (FieldSqlStorageTestCase)
* Issue #7: File API: File uploading transliteration (FileUploadTransliterationTest)
* Issue #8: Views Handlers: Filter: String (ViewsHandlerFilterStringTest)

The `FieldSqlStorageTestCase` fails to return an error when setting a column to
length -1.  This is not likley to affect anything in a production envrionment. See
[Issue #9](https://github.com/silkscreencms-contrib/database_sqlite/issues/9)
for details.

The `FileUploadTransliterationTest` requires a `REGEXP BINARY` in the `WHERE`
clause, which isn't supported by SQLite3. This is needed in Silkscreen core for
case-sensitive searches of file names in the test. See
[Issue #7](https://github.com/silkscreencms-contrib/database_sqlite/issues/7)
for details.

The `ViewsHandlerFilterStringTest` attempts to search for `LENGTH(fieldname) <
5`.  Somewhere, the integer value gets converted to a string value and SQLite
effectively searches for `LENGTH(fieldname) < 0`. See
[Issue #8](https://github.com/silkscreencms-contrib/database_sqlite/issues/8)
for details.

Database Connection Options
---------------------------

The database driver accepts a long list of options, which can be added to the
connection string in `DatabaseConnection_sqlite::__construct()`.  See the
[SQLite3 documentation](https://www.sqlite.org/pragma.html) for details.

The default settings are:

```php
     $connection_options['init_commands'] += array(
      'journal_mode' => "PRAGMA journal_mode=WAL",
      'busy_timeout' => "PRAGMA busy_timeout=3000",
     );
```

When running the test suite, the settings are changed to the following for
performance and to minimize "Database locked" errors.  Note the
`journal_mode=MEMORY` is not recommended for production environments.

```php
    $connection_options['init_commands'] += array(
      'journal_mode' => 'PRAGMA journal_mode=MEMORY',
      'busy_timeout' => 'PRAGMA busy_timeout=30000',
      'temp_store' => 'PRAGMA temp_store=MEMORY',
      'threads' => 'PRAGMA threads=16',
      'page_size' => 'PRAGMA page_size=8192',
      'cache_size' => 'PRAGMA cache_size=8192',
      'synchronous' => 'PRAGMA synchronous=NORMAL',
    );
```

License
-------

This project is GPL v2 software. See the LICENSE.txt file in this directory for
complete text.

Maintainers
-----------

- John Franklin (https://github.com/jlfranklin/)

Originally written for Drupal by the Drupal core team, with many patches
backported.
