SQLite Database Driver (database_sqlite)
===========================================

This driver contains the SQLite database driver for Silkscreen CMS.

To enable this driver, place it in the `drivers` directory in the root of the
Silkscreen CMS site or in the sites/[yoursite]/drivers directory.

Limitations
-----------

The SQLite driver has some limitations that keep it from being fully compatible
with the other Silkscreen database drivers.

* REGEXP BINARY is not supported by SQLite, so the case-insensitive REGEXP is
  used instead.

License
-------

This project is GPL v2 software. See the LICENSE.txt file in this directory for
complete text.

Maintainers
-----------

- John Franklin (https://github.com/jlfranklin/)

Originally written for Drupal by the Drupal core team.
