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

#include <villagesql/vsql.h>
#include "CorruptionEngine.h"
#include <string>
#include <optional>
#include <unordered_map>
#include <sstream>
#include <cstring>
#include <algorithm>

using namespace vsql;

namespace {

std::optional<CorruptionEngine::CorruptionType> parseCorruptionType(std::string_view name) {
    static const std::unordered_map<std::string_view, CorruptionEngine::CorruptionType> mapping = {
        {"WRONG_JOIN_KEY", CorruptionEngine::CorruptionType::WRONG_JOIN_KEY},
        {"MISSING_GROUP_BY", CorruptionEngine::CorruptionType::MISSING_GROUP_BY},
        {"HALLUCINATED_COLUMN", CorruptionEngine::CorruptionType::HALLUCINATED_COLUMN},
        {"AGGREGATE_MISUSE", CorruptionEngine::CorruptionType::AGGREGATE_MISUSE},
        {"ALIAS_SHADOWING", CorruptionEngine::CorruptionType::ALIAS_SHADOWING},
        {"INVALID_NESTING", CorruptionEngine::CorruptionType::INVALID_NESTING},
        {"TYPE_INCOMPATIBILITY", CorruptionEngine::CorruptionType::TYPE_INCOMPATIBILITY},
        {"COMPARISON_WITH_NULL", CorruptionEngine::CorruptionType::COMPARISON_WITH_NULL},
        {"NON_BOOLEAN_WHERE", CorruptionEngine::CorruptionType::NON_BOOLEAN_WHERE},
        {"JOIN_ON_TRUE", CorruptionEngine::CorruptionType::JOIN_ON_TRUE},
        {"WRONG_AGGREGATION", CorruptionEngine::CorruptionType::WRONG_AGGREGATION},
        {"JOIN_TYPE_MUTATION", CorruptionEngine::CorruptionType::JOIN_TYPE_MUTATION},
        {"LOGICAL_OPERATOR_SWAP", CorruptionEngine::CorruptionType::LOGICAL_OPERATOR_SWAP},
        {"COMPARISON_OPERATOR_SWAP", CorruptionEngine::CorruptionType::COMPARISON_OPERATOR_SWAP},
        {"UNNECESSARY_JOIN", CorruptionEngine::CorruptionType::UNNECESSARY_JOIN},
        {"WILDCARD_HALLUCINATION", CorruptionEngine::CorruptionType::WILDCARD_HALLUCINATION},
        {"DISTINCT_MUTATION", CorruptionEngine::CorruptionType::DISTINCT_MUTATION},
        {"HAVING_CLAUSE_MUTATION", CorruptionEngine::CorruptionType::HAVING_CLAUSE_MUTATION},
        {"ORDER_BY_DIRECTION_SWAP", CorruptionEngine::CorruptionType::ORDER_BY_DIRECTION_SWAP},
        {"MISSING_WHERE_CLAUSE", CorruptionEngine::CorruptionType::MISSING_WHERE_CLAUSE},
        {"LIMIT_MUTATION", CorruptionEngine::CorruptionType::LIMIT_MUTATION},
        {"MATH_OPERATOR_SWAP", CorruptionEngine::CorruptionType::MATH_OPERATOR_SWAP},
        {"LIKE_TO_EQUALS_SWAP", CorruptionEngine::CorruptionType::LIKE_TO_EQUALS_SWAP},
        {"UNION_ALL_MUTATION", CorruptionEngine::CorruptionType::UNION_ALL_MUTATION},
        {"IN_TO_EQUALS", CorruptionEngine::CorruptionType::IN_TO_EQUALS},
        {"IS_NULL_INVERSION", CorruptionEngine::CorruptionType::IS_NULL_INVERSION},
        {"BETWEEN_REVERSAL", CorruptionEngine::CorruptionType::BETWEEN_REVERSAL},
        {"EXISTS_INVERSION", CorruptionEngine::CorruptionType::EXISTS_INVERSION},
        {"STRING_FUNCTION_MUTATION", CorruptionEngine::CorruptionType::STRING_FUNCTION_MUTATION},
        {"IN_INVERSION", CorruptionEngine::CorruptionType::IN_INVERSION},
        {"OUTER_JOIN_DIRECTION_SWAP", CorruptionEngine::CorruptionType::OUTER_JOIN_DIRECTION_SWAP},
        {"AGGREGATE_DISTINCT_MUTATION", CorruptionEngine::CorruptionType::AGGREGATE_DISTINCT_MUTATION},
        {"OFFSET_MUTATION", CorruptionEngine::CorruptionType::OFFSET_MUTATION},
        {"SET_OPERATION_SWAP", CorruptionEngine::CorruptionType::SET_OPERATION_SWAP},
        {"CASE_CONDITION_SWAP", CorruptionEngine::CorruptionType::CASE_CONDITION_SWAP}
    };
    auto it = mapping.find(name);
    if (it != mapping.end()) {
        return it->second;
    }
    return std::nullopt;
}

schema::MySQLSchema parseSchema(std::string_view schema_str) {
    std::vector<schema::MySQLTable> tables;
    std::string s(schema_str);
    std::stringstream ss(s);
    std::string table_def;
    while (std::getline(ss, table_def, ';')) {
        if (table_def.empty()) continue;
        size_t colon = table_def.find(':');
        if (colon == std::string::npos) continue;
        std::string table_name = table_def.substr(0, colon);
        table_name.erase(0, table_name.find_first_not_of(" \t\r\n"));
        table_name.erase(table_name.find_last_not_of(" \t\r\n") + 1);
        
        std::string columns_def = table_def.substr(colon + 1);
        std::stringstream css(columns_def);
        std::string col_def;
        std::vector<schema::MySQLColumn> columns;
        while (std::getline(css, col_def, ',')) {
            col_def.erase(0, col_def.find_first_not_of(" \t\r\n"));
            col_def.erase(col_def.find_last_not_of(" \t\r\n") + 1);
            if (col_def.empty()) continue;
            
            size_t space = col_def.find_first_of(" \t");
            std::string col_name;
            std::string col_type = "INT";
            if (space != std::string::npos) {
                col_name = col_def.substr(0, space);
                col_type = col_def.substr(space + 1);
                col_type.erase(0, col_type.find_first_not_of(" \t"));
                col_type.erase(col_type.find_last_not_of(" \t") + 1);
            } else {
                col_name = col_def;
            }
            columns.push_back({col_name, col_type, false});
        }
        tables.push_back({table_name, columns});
    }
    return schema::MySQLSchema(tables);
}

schema::MySQLSchema getDefaultSchema() {
    schema::MySQLTable usersTable{
        "users",
        {
            {"id", "INT", true},
            {"name", "VARCHAR", false},
            {"age", "INT", false}
        }
    };
    schema::MySQLTable ordersTable{
        "orders",
        {
            {"id", "INT", true},
            {"user_id", "INT", false},
            {"amount", "DECIMAL", false},
            {"order_date", "DATE", false}
        }
    };
    schema::MySQLTable itemsTable{
        "order_items",
        {
            {"id", "INT", true},
            {"order_id", "INT", false},
            {"product_name", "VARCHAR", false},
            {"quantity", "INT", false}
        }
    };
    return schema::MySQLSchema{{usersTable, ordersTable, itemsTable}};
}

CorruptionEngine::CorruptionType getRandomCorruptionType() {
    static const std::vector<CorruptionEngine::CorruptionType> allTypes = {
        CorruptionEngine::CorruptionType::WRONG_JOIN_KEY,
        CorruptionEngine::CorruptionType::MISSING_GROUP_BY,
        CorruptionEngine::CorruptionType::HALLUCINATED_COLUMN,
        CorruptionEngine::CorruptionType::AGGREGATE_MISUSE,
        CorruptionEngine::CorruptionType::ALIAS_SHADOWING,
        CorruptionEngine::CorruptionType::INVALID_NESTING,
        CorruptionEngine::CorruptionType::TYPE_INCOMPATIBILITY,
        CorruptionEngine::CorruptionType::COMPARISON_WITH_NULL,
        CorruptionEngine::CorruptionType::NON_BOOLEAN_WHERE,
        CorruptionEngine::CorruptionType::JOIN_ON_TRUE,
        CorruptionEngine::CorruptionType::WRONG_AGGREGATION,
        CorruptionEngine::CorruptionType::JOIN_TYPE_MUTATION,
        CorruptionEngine::CorruptionType::LOGICAL_OPERATOR_SWAP,
        CorruptionEngine::CorruptionType::COMPARISON_OPERATOR_SWAP,
        CorruptionEngine::CorruptionType::UNNECESSARY_JOIN,
        CorruptionEngine::CorruptionType::WILDCARD_HALLUCINATION,
        CorruptionEngine::CorruptionType::DISTINCT_MUTATION,
        CorruptionEngine::CorruptionType::HAVING_CLAUSE_MUTATION,
        CorruptionEngine::CorruptionType::ORDER_BY_DIRECTION_SWAP,
        CorruptionEngine::CorruptionType::MISSING_WHERE_CLAUSE,
        CorruptionEngine::CorruptionType::LIMIT_MUTATION,
        CorruptionEngine::CorruptionType::MATH_OPERATOR_SWAP,
        CorruptionEngine::CorruptionType::LIKE_TO_EQUALS_SWAP,
        CorruptionEngine::CorruptionType::UNION_ALL_MUTATION,
        CorruptionEngine::CorruptionType::IN_TO_EQUALS,
        CorruptionEngine::CorruptionType::IS_NULL_INVERSION,
        CorruptionEngine::CorruptionType::BETWEEN_REVERSAL,
        CorruptionEngine::CorruptionType::EXISTS_INVERSION,
        CorruptionEngine::CorruptionType::STRING_FUNCTION_MUTATION,
        CorruptionEngine::CorruptionType::IN_INVERSION,
        CorruptionEngine::CorruptionType::OUTER_JOIN_DIRECTION_SWAP,
        CorruptionEngine::CorruptionType::AGGREGATE_DISTINCT_MUTATION,
        CorruptionEngine::CorruptionType::OFFSET_MUTATION,
        CorruptionEngine::CorruptionType::SET_OPERATION_SWAP,
        CorruptionEngine::CorruptionType::CASE_CONDITION_SWAP
    };
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, allTypes.size() - 1);
    return allTypes[dis(gen)];
}

} // namespace

void vsql_corrupt_impl(VarArgs args, StringResult out) {
    if (args.size() < 1 || args.size() > 3) {
        out.error("vsql_corrupt expects between 1 and 3 arguments: query, [corruption_type], [schema]");
        return;
    }

    if (args[0].is_null()) {
        out.set_null();
        return;
    }

    if (!args[0].is_str()) {
        out.error("First argument to vsql_corrupt must be a query string");
        return;
    }
    std::string_view query = args[0].as_str();

    // Determine corruption type
    CorruptionEngine::CorruptionType type = getRandomCorruptionType();
    if (args.size() >= 2) {
        if (!args[1].is_null()) {
            if (!args[1].is_str()) {
                out.error("Second argument (corruption type) must be a string");
                return;
            }
            std::string_view type_str = args[1].as_str();
            if (type_str != "RANDOM" && !type_str.empty()) {
                auto parsed_type = parseCorruptionType(type_str);
                if (!parsed_type) {
                    out.error("Invalid corruption type specified");
                    return;
                }
                type = *parsed_type;
            }
        }
    }

    // Determine schema
    schema::MySQLSchema schemaObj = getDefaultSchema();
    if (args.size() >= 3) {
        if (!args[2].is_null()) {
            if (!args[2].is_str()) {
                out.error("Third argument (schema) must be a string");
                return;
            }
            schemaObj = parseSchema(args[2].as_str());
        }
    }

    CorruptionEngine engine(schemaObj);
    std::string corrupted = engine.applyCorruption(std::string(query), type);

    auto buf = out.buffer();
    if (corrupted.length() > buf.size()) {
        out.error("Resulting query exceeds buffer size");
        return;
    }

    memcpy(buf.data(), corrupted.c_str(), corrupted.length());
    out.set_length(corrupted.length());
}

void vsql_corrupt_with_schema_impl(StringArg query, StringArg corruption_type, StringArg schema_str, StringResult out) {
    if (query.is_null() || schema_str.is_null()) {
        out.set_null();
        return;
    }

    CorruptionEngine::CorruptionType type = getRandomCorruptionType();
    if (!corruption_type.is_null()) {
        std::string_view type_str = corruption_type.value();
        if (type_str != "RANDOM" && !type_str.empty()) {
            auto parsed_type = parseCorruptionType(type_str);
            if (!parsed_type) {
                out.error("Invalid corruption type specified");
                return;
            }
            type = *parsed_type;
        }
    }

    schema::MySQLSchema schemaObj = parseSchema(schema_str.value());
    CorruptionEngine engine(schemaObj);
    std::string corrupted = engine.applyCorruption(std::string(query.value()), type);

    auto buf = out.buffer();
    if (corrupted.length() > buf.size()) {
        out.error("Resulting query exceeds buffer size");
        return;
    }

    memcpy(buf.data(), corrupted.c_str(), corrupted.length());
    out.set_length(corrupted.length());
}

VEF_GENERATE_ENTRY_POINTS(
  make_extension()
    .func(make_func<&vsql_corrupt_impl>("vsql_corrupt")
      .returns(STRING)
      .varargs()
      .buffer_size(65535)
      .build())
    .func(make_func<&vsql_corrupt_with_schema_impl>("vsql_corrupt_with_schema")
      .returns(STRING)
      .param(STRING)
      .param(STRING)
      .param(STRING)
      .buffer_size(65535)
      .build())
)

