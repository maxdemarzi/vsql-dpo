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

### 2. `vsql_schema_cache_ready()`
Checks if the background schema cache has been populated.
- **Returns**: `1` (INT) if the schema cache contains at least one database, or `0` (INT) otherwise.

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
   
   You can verify if the background worker has finished fetching and populating the schema cache by calling:
   ```sql
   SELECT vsql_schema_cache_ready(); -- Returns 1 if populated, 0 otherwise
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

| Type | Description |
|---|---|
| `WRONG_JOIN_KEY` | Swaps or modifies join keys |
| `MISSING_GROUP_BY` | Omit GROUP BY clause or columns |
| `HALLUCINATED_COLUMN` | Injects a non-existent column name |
| `AGGREGATE_MISUSE` | Incorrect aggregate functions application |
| `ALIAS_SHADOWING` | Shadow variables or introduce duplicate aliases |
| `INVALID_NESTING` | Invalidate subquery structure or nesting |
| `TYPE_INCOMPATIBILITY` | Forces operation between mismatched types (e.g. VARCHAR = INT) |
| `COMPARISON_WITH_NULL` | Mutates comparison operators against NULL |
| `NON_BOOLEAN_WHERE` | Inserts non-boolean expression in WHERE |
| `JOIN_ON_TRUE` | Injects `ON TRUE` / `ON 1=1` for joins |
| `WRONG_AGGREGATION` | Swaps aggregator functions (e.g., `SUM` $\rightarrow$ `AVG`) |
| `JOIN_TYPE_MUTATION` | Changes join type (e.g., `LEFT` $\rightarrow$ `INNER`) |
| `LOGICAL_OPERATOR_SWAP` | Swaps `AND` $\leftrightarrow$ `OR`, or `NOT` |
| `COMPARISON_OPERATOR_SWAP`| Swaps comparison operators (e.g. `=` $\leftrightarrow$ `!=`) |
| `UNNECESSARY_JOIN` | Injects an unrelated table join |
| `WILDCARD_HALLUCINATION` | Injects incorrect wildcards into queries |
| `DISTINCT_MUTATION` | Adds or removes the `DISTINCT` keyword |
| `HAVING_CLAUSE_MUTATION` | Modifies `HAVING` clause expressions |
| `ORDER_BY_DIRECTION_SWAP` | Inverts `ASC` $\leftrightarrow$ `DESC` |
| `MISSING_WHERE_CLAUSE` | Removes `WHERE` clauses from queries |
| `LIMIT_MUTATION` | Modifies or deletes `LIMIT` values |
| `MATH_OPERATOR_SWAP` | Swaps math operators (e.g. `+` $\leftrightarrow$ `-`) |
| `LIKE_TO_EQUALS_SWAP` | Swaps `LIKE` operator to `=` |
| `UNION_ALL_MUTATION` | Mutates `UNION` $\leftrightarrow$ `UNION ALL` |
| `IN_TO_EQUALS` | Converts `IN (...)` expression to `=` |
| `IS_NULL_INVERSION` | Swaps `IS NULL` $\leftrightarrow$ `IS NOT NULL` |
| `BETWEEN_REVERSAL` | Reverses bounds in `BETWEEN` statements |
| `EXISTS_INVERSION` | Inverts `EXISTS` to `NOT EXISTS` |
| `STRING_FUNCTION_MUTATION`| Mutates string manipulation functions |
| `IN_INVERSION` | Swaps `IN` $\leftrightarrow$ `NOT IN` |
| `OUTER_JOIN_DIRECTION_SWAP`| Swaps `LEFT` $\leftrightarrow$ `RIGHT` join direction |
| `AGGREGATE_DISTINCT_MUTATION`| Injects or removes `DISTINCT` within aggregates |
| `OFFSET_MUTATION` | Modifies or deletes `OFFSET` values |
| `SET_OPERATION_SWAP` | Swaps set operators (`UNION`, `EXCEPT`, `INTERSECT`) |
| `CASE_CONDITION_SWAP` | Mutates conditions inside `CASE WHEN` clauses |

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
