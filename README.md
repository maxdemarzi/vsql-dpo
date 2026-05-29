# vsql-corruptor

`vsql-corruptor` is a SQL Query Corruption Engine extension for VillageSQL. It parses standard SQL queries and systematically injects syntactical and logical mutations/errors. This is used to generate negative training examples for DPO (Direct Preference Optimization) fine-tuning of text-to-SQL models.

## Features

- **AST-level Corruption**: Uses `hyrise/sql-parser` to parse input SQL into an Abstract Syntax Tree (AST), mutates specific parts, and unparses it back to a valid/invalid query string.
- **35+ Built-in Corruption Types**: Support for a wide variety of mutation rules, ranging from operator swaps to schema-aware type mismatches and group-by omissions.
- **Schema-Aware Injections**: Ability to pass custom schemas to make corruptions (e.g. type incompatibilities or join keys) realistic and targeted.

## Registered Functions

The extension exposes the following functions:

### 1. `vsql_corrupt(query, corruption_type, schema_or_db_name)`
Applies a corruption to the specified SQL query. All three arguments are required.
- **`query`** (STRING): The SQL query string to corrupt.
- **`corruption_type`** (STRING): The specific type of corruption to apply (e.g., `'COMPARISON_OPERATOR_SWAP'`). If set to `'RANDOM'` or `NULL`, a random corruption from all supported types will be selected.
- **`schema_or_db_name`** (STRING): Either a schema definition string (containing a `:` character) or a database name.
- **Returns**: A corrupted SQL query string.

### 2. `vsql_schema_cache_ready(db_name)`
Checks if the background schema cache has been populated for the specified database name.
- **`db_name`** (STRING): The name of the database to check.
- **Returns**: `1` (INT) if the database schema is cached, or `0` (INT) otherwise.

---

## Schema and Database Resolution

The extension resolves the database schema structure from the third parameter `schema_or_db_name`:

1. **Explicit Schema Format**:
   If the parameter contains a colon `:`, it is parsed as a semicolon-separated list of table definitions. Each table lists its columns and types:
   ```
   table1:col1 TYPE,col2 TYPE;table2:col1 TYPE
   ```
   Example:
   ```sql
   SELECT vsql_corrupt(
     'SELECT first_name FROM customers WHERE first_name = \'Alice\'',
     'TYPE_INCOMPATIBILITY',
     'customers:first_name VARCHAR,id INT'
   );
   -- Returns: SELECT first_name FROM customers WHERE (first_name = 1234)
   ```

2. **Dynamic Database Lookup**:
   If the parameter does *not* contain a colon `:`, it is treated as a MySQL database name. The database schema must be fetched and cached by a background worker thread.
   
   To enable the background worker thread, set the global variable:
   ```sql
   SET GLOBAL vsql_corruptor.schema_cache_enabled = ON;
   ```
   
   You can verify if the background worker has finished fetching and populating the schema cache for your database by calling:
   ```sql
   SELECT vsql_schema_cache_ready('test_corrupt_db'); -- Returns 1 if populated, 0 otherwise
   ```
   
   Example:
   ```sql
   SELECT vsql_corrupt(
     'SELECT first_name FROM customers WHERE id = 30',
     'COMPARISON_OPERATOR_SWAP',
     'test_corrupt_db'
   );
   ```

---

## Supported Corruption Types

| Type | Description | Original Query | Corrupted Query |
|---|---|---|---|
| `WRONG_JOIN_KEY` | Swaps or modifies join keys | `SELECT users.name, orders.amount FROM users INNER JOIN orders ON users.id = orders.user_id` | `SELECT users.name, orders.amount FROM users INNER JOIN orders ON (orders.order_date = orders.amount)` |
| `MISSING_GROUP_BY` | Omit GROUP BY clause or columns | `SELECT name, age FROM users GROUP BY name` | `SELECT name, age FROM users` |
| `HALLUCINATED_COLUMN` | Injects a non-existent column name | `SELECT name, age FROM users` | `SELECT name, age, aeg FROM users` |
| `AGGREGATE_MISUSE` | Incorrect aggregate functions application | `SELECT SUM(name) FROM users` | `SELECT MAX(SUM(name)) FROM users` |
| `ALIAS_SHADOWING` | Shadow variables or introduce duplicate aliases | `SELECT name AS username, age FROM users` | `SELECT name AS username, age AS username FROM users` |
| `INVALID_NESTING` | Invalidate subquery structure or nesting | `SELECT name, COUNT(id) FROM users GROUP BY name` | `SELECT name, MAX(COUNT(id)) FROM users GROUP BY name` |
| `TYPE_INCOMPATIBILITY` | Forces operation between mismatched types (e.g. VARCHAR = INT) | `SELECT name FROM users WHERE name = 'John'` | `SELECT name FROM users WHERE (name = 1234)` |
| `COMPARISON_WITH_NULL` | Mutates comparison operators against NULL | `SELECT name FROM users WHERE age = 30` | `SELECT name FROM users WHERE (age = NULL)` |
| `NON_BOOLEAN_WHERE` | Inserts non-boolean expression in WHERE | `SELECT name FROM users WHERE age = 30` | `SELECT name FROM users WHERE 'id_value'` |
| `JOIN_ON_TRUE` | Injects `ON TRUE` / `ON 1=1` for joins | `SELECT users.name, orders.amount FROM users INNER JOIN orders ON users.id = orders.user_id` | `SELECT users.name, orders.amount FROM users INNER JOIN orders ON TRUE` |
| `WRONG_AGGREGATION` | Swaps aggregator functions (e.g., `SUM` $\rightarrow$ `AVG`) | `SELECT MAX(age), SUM(id) FROM users` | `SELECT MIN(age), AVG(id) FROM users` |
| `JOIN_TYPE_MUTATION` | Changes join type (e.g., `INNER` $\rightarrow$ `LEFT`) | `SELECT users.name, orders.amount FROM users INNER JOIN orders ON users.id = orders.user_id` | `SELECT users.name, orders.amount FROM users LEFT JOIN orders ON (users.id = orders.user_id)` |
| `LOGICAL_OPERATOR_SWAP` | Swaps `AND` $\leftrightarrow$ `OR`, or `NOT` | `SELECT name FROM users WHERE age > 20 AND name = 'John'` | `SELECT name FROM users WHERE ((age > 20) OR (name = 'John'))` |
| `COMPARISON_OPERATOR_SWAP`| Swaps comparison operators (e.g. `=` $\leftrightarrow$ `!=`) | `SELECT name, age FROM users WHERE age = 30` | `SELECT name, age FROM users WHERE (age != 30)` |
| `UNNECESSARY_JOIN` | Injects an unrelated table join | `SELECT name FROM users` | `SELECT name FROM users INNER JOIN orders ON TRUE` |
| `WILDCARD_HALLUCINATION` | Injects incorrect wildcards into queries | `SELECT name, age FROM users` | `SELECT * FROM users` |
| `DISTINCT_MUTATION` | Adds or removes the `DISTINCT` keyword | `SELECT name FROM users` | `SELECT DISTINCT name FROM users` |
| `HAVING_CLAUSE_MUTATION` | Modifies `HAVING` clause expressions | `SELECT name, COUNT(id) FROM users GROUP BY name HAVING COUNT(id) > 5` | `SELECT name, COUNT(id) FROM users WHERE (COUNT(id) > 5) GROUP BY name` |
| `ORDER_BY_DIRECTION_SWAP` | Inverts `ASC` $\leftrightarrow$ `DESC` | `SELECT name FROM users ORDER BY age ASC` | `SELECT name FROM users ORDER BY age DESC` |
| `MISSING_WHERE_CLAUSE` | Removes `WHERE` clauses from queries | `SELECT name FROM users WHERE age = 30` | `SELECT name FROM users` |
| `LIMIT_MUTATION` | Modifies or deletes `LIMIT` values | `SELECT name FROM users LIMIT 5` | `SELECT name FROM users` |
| `MATH_OPERATOR_SWAP` | Swaps math operators (e.g. `+` $\leftrightarrow$ `-`) | `SELECT name FROM users WHERE age + 5 = 30` | `SELECT name FROM users WHERE ((age - 5) = 30)` |
| `LIKE_TO_EQUALS_SWAP` | Swaps `LIKE` operator to `=` | `SELECT name FROM users WHERE name LIKE 'J%'` | `SELECT name FROM users WHERE (name = 'J%')` |
| `UNION_ALL_MUTATION` | Mutates `UNION` $\leftrightarrow$ `UNION ALL` | `SELECT name FROM users UNION SELECT name FROM orders` | `SELECT name FROM users UNION ALL SELECT name FROM orders` |
| `IN_TO_EQUALS` | Converts `IN (...)` expression to `=` | `SELECT name FROM users WHERE id IN (SELECT user_id FROM orders)` | `SELECT name FROM users WHERE (id = (SELECT user_id FROM orders))` |
| `IS_NULL_INVERSION` | Swaps `IS NULL` $\leftrightarrow$ `IS NOT NULL` | `SELECT name FROM users WHERE age IS NULL` | `SELECT name FROM users WHERE age IS NOT NULL` |
| `BETWEEN_REVERSAL` | Reverses bounds in `BETWEEN` statements | `SELECT name FROM users WHERE age BETWEEN 10 AND 50` | `SELECT name FROM users WHERE age BETWEEN 50 AND 10` |
| `EXISTS_INVERSION` | Inverts `EXISTS` to `NOT EXISTS` | `SELECT name FROM users WHERE EXISTS (SELECT user_id FROM orders WHERE orders.user_id = users.id)` | `SELECT name FROM users WHERE NOT (EXISTS (SELECT user_id FROM orders WHERE (orders.user_id = users.id)))` |
| `STRING_FUNCTION_MUTATION`| Mutates string manipulation functions | `SELECT UPPER(name) FROM users` | `SELECT LOWER(name) FROM users` |
| `IN_INVERSION` | Swaps `IN` $\leftrightarrow$ `NOT IN` | `SELECT name FROM users WHERE id IN (1, 2, 3)` | `SELECT name FROM users WHERE NOT (id IN (1, 2, 3))` |
| `OUTER_JOIN_DIRECTION_SWAP`| Swaps `LEFT` $\leftrightarrow$ `RIGHT` join direction | `SELECT users.name, orders.amount FROM users LEFT JOIN orders ON users.id = orders.user_id` | `SELECT users.name, orders.amount FROM users RIGHT JOIN orders ON (users.id = orders.user_id)` |
| `AGGREGATE_DISTINCT_MUTATION`| Injects or removes `DISTINCT` within aggregates | `SELECT COUNT(DISTINCT name) FROM users` | `SELECT COUNT(name) FROM users` |
| `OFFSET_MUTATION` | Modifies or deletes `OFFSET` values | `SELECT name FROM users LIMIT 5 OFFSET 2` | `SELECT name FROM users LIMIT 5` |
| `SET_OPERATION_SWAP` | Swaps set operators (`UNION`, `EXCEPT`, `INTERSECT`) | `SELECT name FROM users INTERSECT SELECT name FROM orders` | `SELECT name FROM users EXCEPT SELECT name FROM orders` |
| `CASE_CONDITION_SWAP` | Mutates conditions inside `CASE WHEN` clauses | `SELECT CASE age WHEN 18 THEN name ELSE 'adult' END FROM users` | `SELECT CASE age WHEN 18 THEN 'adult' ELSE name END FROM users` |

---

## Build System

To build the extension, you need:
- VillageSQL build directory (with completed build)
- CMake 3.18 or higher
- C++ compiler with C++17 support

### Building the Extension
Run `cmake` and specify `VillageSQL_BUILD_DIR`:
```bash
mkdir build
cd build
cmake .. -DVillageSQL_BUILD_DIR=/path/to/villagesql/build-debug
make -j$(nproc)
```
This produces the `vsql_corruptor.veb` package file inside the `build/` directory.

---

## Testing

A local CI script is provided to automate build and execution of MTR (MySQL Test Runner) test suites against the extension.

### Running Local CI Tests
Export the `VILLAGESQL_BUILD_DIR` environment variable and run `./local-ci.sh`:
```bash
export VILLAGESQL_BUILD_DIR=/home/maxdemarzi/build/villagesql
./local-ci.sh
```

### Recording Expected Results
To record or update test results in `mysql-test/r/`:
```bash
./local-ci.sh --record
```

---

## Installing & Loading the Extension

After compiling and generating `vsql_corruptor.veb`, load the extension in VillageSQL:

```sql
INSTALL EXTENSION vsql_corruptor;
```

Verify your loaded functions:
```sql
SELECT vsql_corrupt('SELECT name, age FROM users WHERE age = 30', 'COMPARISON_OPERATOR_SWAP') AS comparison_swap;
-- Returns: SELECT name, age FROM users WHERE (age != 30)
```

To clean up:
```sql
UNINSTALL EXTENSION vsql_corruptor;
```

---

## License

This project is licensed under the GPL-2.0 License.
