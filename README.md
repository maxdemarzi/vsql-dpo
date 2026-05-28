# vsql-corruptor

`vsql-corruptor` is a SQL Query Corruption Engine extension for VillageSQL. It parses standard SQL queries and systematically injects syntactical and logical mutations/errors. This is useful for fuzzing SQL engines, validating optimizer correctness, testing client-side query validation, or verification of database resilience.

## Features

- **AST-level Corruption**: Uses `hyrise/sql-parser` to parse input SQL into an Abstract Syntax Tree (AST), mutates specific parts, and unparses it back to a valid/invalid query string.
- **35+ Built-in Corruption Types**: Support for a wide variety of mutation rules, ranging from operator swaps to schema-aware type mismatches and group-by omissions.
- **Schema-Aware Injections**: Ability to pass custom schemas to make corruptions (e.g. type incompatibilities or join keys) realistic and targeted.

## Registered Functions

The extension exposes two main query corruption functions:

### 1. `vsql_corrupt(query [, corruption_type [, schema]])`
Applies a corruption to the specified SQL query.
- **`query`** (STRING): The SQL query string to corrupt.
- **`corruption_type`** (STRING, Optional): The specific type of corruption to apply (e.g., `'COMPARISON_OPERATOR_SWAP'`). If set to `'RANDOM'`, `NULL`, or omitted, a random corruption from all supported types will be selected.
- **`schema`** (STRING, Optional): A custom table/column schema string. If omitted, a default schema is used.
- **Returns**: A corrupted SQL query string.

### 2. `vsql_corrupt_with_schema(query, corruption_type, schema)`
An explicit version that requires the corruption type and a schema string.
- **Returns**: A corrupted SQL query string.

---

## Schema Format

Schemas are defined using a semicolon-separated list of tables. Each table lists its columns and types after a colon:
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

### Default Schema
If no custom schema is provided, the extension defaults to a schema with:
- **`users`** (id INT, name VARCHAR, age INT)
- **`orders`** (id INT, user_id INT, amount DECIMAL, order_date DATE)
- **`order_items`** (id INT, order_id INT, product_name VARCHAR, quantity INT)

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
