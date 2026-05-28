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

#ifndef VSQL_CORRUPTOR_CORRUPTION_ENGINE_H
#define VSQL_CORRUPTOR_CORRUPTION_ENGINE_H

#include <string>
#include <vector>
#include <unordered_map>
#include "Schema.h"
#include "SQLParser.h"

class CorruptionEngine {
public:
    enum class CorruptionType {
        WRONG_JOIN_KEY,
        MISSING_GROUP_BY,
        HALLUCINATED_COLUMN,
        AGGREGATE_MISUSE,
        ALIAS_SHADOWING,
        INVALID_NESTING,
        TYPE_INCOMPATIBILITY,
        COMPARISON_WITH_NULL,
        NON_BOOLEAN_WHERE,
        JOIN_ON_TRUE,
        WRONG_AGGREGATION,
        JOIN_TYPE_MUTATION,
        LOGICAL_OPERATOR_SWAP,
        COMPARISON_OPERATOR_SWAP,
        UNNECESSARY_JOIN,
        WILDCARD_HALLUCINATION,
        DISTINCT_MUTATION,
        HAVING_CLAUSE_MUTATION,
        ORDER_BY_DIRECTION_SWAP,
        MISSING_WHERE_CLAUSE,
        LIMIT_MUTATION,
        MATH_OPERATOR_SWAP,
        LIKE_TO_EQUALS_SWAP,
        UNION_ALL_MUTATION,
        IN_TO_EQUALS,
        IS_NULL_INVERSION,
        BETWEEN_REVERSAL,
        EXISTS_INVERSION,
        STRING_FUNCTION_MUTATION,
        IN_INVERSION,
        OUTER_JOIN_DIRECTION_SWAP,
        AGGREGATE_DISTINCT_MUTATION,
        OFFSET_MUTATION,
        SET_OPERATION_SWAP,
        CASE_CONDITION_SWAP
    };

    explicit CorruptionEngine(schema::MySQLSchema schema);

    std::string applyCorruption(const std::string& validSql, CorruptionType type);

private:
    schema::MySQLSchema schema_;

    // Helpers to visit and mutate the AST
    void visitStatement(hsql::SQLStatement* stmt, CorruptionType type);
    void visitSelect(hsql::SelectStatement* select, CorruptionType type);
    void visitExpr(hsql::Expr* expr, CorruptionType type);
    void visitTableRef(hsql::TableRef* table, CorruptionType type);

    // Casing restorer
    std::string restoreOriginalCasing(const std::string& sql, const std::string& validSql);

    // Helper generation methods
    std::string getAliasOrColumnName(const hsql::Expr* expr);
    std::string generateTypo(const std::string& original);
};

#endif // VSQL_CORRUPTOR_CORRUPTION_ENGINE_H
