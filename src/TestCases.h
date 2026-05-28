/* Copyright (c) 2026 VillageSQL Contributors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef REBAD_TEST_CASES_H
#define REBAD_TEST_CASES_H

#include <string>
#include <vector>
#include "CorruptionEngine.h"

struct TestCase {
    std::string query;
    CorruptionEngine::CorruptionType type;
    std::string description;
};

inline const std::vector<TestCase>& getTestCases() {
    static const std::vector<TestCase> testCases = {
        {
            "SELECT name, age FROM users WHERE age = 30",
            CorruptionEngine::CorruptionType::COMPARISON_OPERATOR_SWAP,
            "COMPARISON_OPERATOR_SWAP (swap = to !=)"
        },
        {
            "SELECT name, age FROM users GROUP BY name",
            CorruptionEngine::CorruptionType::MISSING_GROUP_BY,
            "MISSING_GROUP_BY (drop GROUP BY clause)"
        },
        {
            "SELECT name, COUNT(id) FROM users GROUP BY name",
            CorruptionEngine::CorruptionType::INVALID_NESTING,
            "INVALID_NESTING (wrap COUNT in MAX)"
        },
        {
            "SELECT name FROM users WHERE name = 'John'",
            CorruptionEngine::CorruptionType::TYPE_INCOMPATIBILITY,
            "TYPE_INCOMPATIBILITY (compare string with random int)"
        },
        {
            "SELECT name FROM users WHERE age = 30",
            CorruptionEngine::CorruptionType::COMPARISON_WITH_NULL,
            "COMPARISON_WITH_NULL (change age = 30 to age = NULL)"
        },
        {
            "SELECT name FROM users WHERE age = 30",
            CorruptionEngine::CorruptionType::NON_BOOLEAN_WHERE,
            "NON_BOOLEAN_WHERE (replace WHERE expression with column literal)"
        },
        {
            "SELECT users.name, orders.amount FROM users INNER JOIN orders ON users.id = orders.user_id",
            CorruptionEngine::CorruptionType::WRONG_JOIN_KEY,
            "WRONG_JOIN_KEY (replace JOIN condition with randomized table/column match)"
        },
        {
            "SELECT users.name, orders.amount FROM users INNER JOIN orders ON users.id = orders.user_id",
            CorruptionEngine::CorruptionType::JOIN_ON_TRUE,
            "JOIN_ON_TRUE (replace JOIN condition with ON TRUE)"
        },
        {
            "SELECT users.name, orders.amount FROM users INNER JOIN orders ON users.id = orders.user_id",
            CorruptionEngine::CorruptionType::JOIN_TYPE_MUTATION,
            "JOIN_TYPE_MUTATION (toggle INNER to LEFT join)"
        },
        {
            "SELECT MAX(age), SUM(id) FROM users",
            CorruptionEngine::CorruptionType::WRONG_AGGREGATION,
            "WRONG_AGGREGATION (swap MAX to MIN, SUM to AVG)"
        },
        {
            "SELECT name FROM users WHERE age > 20 AND name = 'John'",
            CorruptionEngine::CorruptionType::LOGICAL_OPERATOR_SWAP,
            "LOGICAL_OPERATOR_SWAP (flip AND to OR)"
        },
        {
            "SELECT name FROM users WHERE age + 5 = 30",
            CorruptionEngine::CorruptionType::MATH_OPERATOR_SWAP,
            "MATH_OPERATOR_SWAP (swap + to -)"
        },
        {
            "SELECT name FROM users WHERE name LIKE 'J%'",
            CorruptionEngine::CorruptionType::LIKE_TO_EQUALS_SWAP,
            "LIKE_TO_EQUALS_SWAP (swap LIKE to =)"
        },
        {
            "SELECT name FROM users UNION SELECT name FROM orders",
            CorruptionEngine::CorruptionType::UNION_ALL_MUTATION,
            "UNION_ALL_MUTATION (toggle UNION to UNION ALL)"
        },
        {
            "SELECT name FROM users WHERE id IN (SELECT user_id FROM orders)",
            CorruptionEngine::CorruptionType::IN_TO_EQUALS,
            "IN_TO_EQUALS (change IN subquery to =)"
        },
        {
            "SELECT name FROM users WHERE age IS NULL",
            CorruptionEngine::CorruptionType::IS_NULL_INVERSION,
            "IS_NULL_INVERSION (flip IS NULL to IS NOT NULL)"
        },
        {
            "SELECT name FROM users WHERE age IS NOT NULL",
            CorruptionEngine::CorruptionType::IS_NULL_INVERSION,
            "IS_NULL_INVERSION (flip IS NOT NULL to IS NULL)"
        },
        {
            "SELECT name FROM users WHERE age BETWEEN 10 AND 50",
            CorruptionEngine::CorruptionType::BETWEEN_REVERSAL,
            "BETWEEN_REVERSAL (swap BETWEEN bounds)"
        },
        {
            "SELECT name FROM users WHERE EXISTS (SELECT user_id FROM orders WHERE orders.user_id = users.id)",
            CorruptionEngine::CorruptionType::EXISTS_INVERSION,
            "EXISTS_INVERSION (wrap EXISTS with NOT)"
        },
        {
            "SELECT UPPER(name) FROM users",
            CorruptionEngine::CorruptionType::STRING_FUNCTION_MUTATION,
            "STRING_FUNCTION_MUTATION (UPPER to LOWER)"
        },
        {
            "SELECT name FROM users WHERE id IN (1, 2, 3)",
            CorruptionEngine::CorruptionType::IN_INVERSION,
            "IN_INVERSION (change IN to NOT IN)"
        },
        {
            "SELECT users.name, orders.amount FROM users LEFT JOIN orders ON users.id = orders.user_id",
            CorruptionEngine::CorruptionType::OUTER_JOIN_DIRECTION_SWAP,
            "OUTER_JOIN_DIRECTION_SWAP (LEFT to RIGHT)"
        },
        {
            "SELECT COUNT(DISTINCT name) FROM users",
            CorruptionEngine::CorruptionType::AGGREGATE_DISTINCT_MUTATION,
            "AGGREGATE_DISTINCT_MUTATION (toggle DISTINCT inside function)"
        },
        {
            "SELECT name FROM users LIMIT 5",
            CorruptionEngine::CorruptionType::LIMIT_MUTATION,
            "LIMIT_MUTATION (drop LIMIT)"
        },
        {
            "SELECT name FROM users LIMIT 5 OFFSET 2",
            CorruptionEngine::CorruptionType::OFFSET_MUTATION,
            "OFFSET_MUTATION (drop OFFSET)"
        },
        {
            "SELECT name FROM users INTERSECT SELECT name FROM orders",
            CorruptionEngine::CorruptionType::SET_OPERATION_SWAP,
            "SET_OPERATION_SWAP (INTERSECT to EXCEPT)"
        },
        {
            "SELECT CASE age WHEN 18 THEN name ELSE 'adult' END FROM users",
            CorruptionEngine::CorruptionType::CASE_CONDITION_SWAP,
            "CASE_CONDITION_SWAP (swap THEN and ELSE branches)"
        },
        {
            "SELECT name, age FROM users",
            CorruptionEngine::CorruptionType::HALLUCINATED_COLUMN,
            "HALLUCINATED_COLUMN (inject typo column)"
        },
        {
            "SELECT name AS username, age FROM users",
            CorruptionEngine::CorruptionType::ALIAS_SHADOWING,
            "ALIAS_SHADOWING (alias age as username to shadow)"
        },
        {
            "SELECT name, age FROM users",
            CorruptionEngine::CorruptionType::WILDCARD_HALLUCINATION,
            "WILDCARD_HALLUCINATION (replace columns with *)"
        },
        {
            "SELECT name FROM users",
            CorruptionEngine::CorruptionType::DISTINCT_MUTATION,
            "DISTINCT_MUTATION (add DISTINCT)"
        },
        {
            "SELECT name FROM users WHERE age = 30",
            CorruptionEngine::CorruptionType::MISSING_WHERE_CLAUSE,
            "MISSING_WHERE_CLAUSE (drop WHERE clause)"
        },
        {
            "SELECT name FROM users",
            CorruptionEngine::CorruptionType::UNNECESSARY_JOIN,
            "UNNECESSARY_JOIN (inject random table join)"
        }
    };
    return testCases;
}

#endif // REBAD_TEST_CASES_H
