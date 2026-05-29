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
#include <villagesql/preview/sql_query.h>
#include <villagesql/preview/thread_worker.h>
#include "CorruptionEngine.h"
#include <string>
#include <optional>
#include <unordered_map>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <shared_mutex>
#include <mutex>

using namespace vsql;

static vsql::preview_sql_query::SqlQueryCapability g_sql_query_cap;

namespace {

std::shared_mutex g_schema_cache_mutex;
std::unordered_map<std::string, schema::MySQLSchema> g_schema_cache;

void updateSchemaCache(struct vef_thread_handle_t *handle) {
    auto session = g_sql_query_cap.open(handle);
    if (!session) {
        return;
    }

    std::string sql = "SELECT TABLE_SCHEMA, TABLE_NAME, COLUMN_NAME, DATA_TYPE, COLUMN_KEY "
                      "FROM INFORMATION_SCHEMA.COLUMNS "
                      "ORDER BY TABLE_SCHEMA, TABLE_NAME, ORDINAL_POSITION";

    auto result = session.sql(sql).execute();
    if (!result || result.has_error()) {
        return;
    }

    std::unordered_map<std::string, std::vector<schema::MySQLTable>> temp_tables_by_db;

    std::string current_db = "";
    std::string current_table = "";
    std::vector<schema::MySQLColumn> current_columns;

    auto save_current_table = [&]() {
        if (!current_db.empty() && !current_table.empty() && !current_columns.empty()) {
            temp_tables_by_db[current_db].push_back({current_table, current_columns});
        }
        current_columns.clear();
    };

    while (result.next()) {
        std::string_view db_name_sv = result.column_str(0);
        std::string_view table_name_sv = result.column_str(1);
        std::string_view col_name_sv = result.column_str(2);
        std::string_view data_type_sv = result.column_str(3);
        std::string_view col_key_sv = result.column_str(4);

        if (db_name_sv.empty() || table_name_sv.empty() || col_name_sv.empty() || data_type_sv.empty()) {
            continue;
        }

        std::string db_name(db_name_sv);
        std::string table_name(table_name_sv);
        std::string col_name(col_name_sv);
        std::string data_type(data_type_sv);
        std::string col_key(col_key_sv);

        std::transform(data_type.begin(), data_type.end(), data_type.begin(), ::toupper);
        bool is_pri = (col_key == "PRI");

        if (db_name != current_db || table_name != current_table) {
            save_current_table();
            current_db = db_name;
            current_table = table_name;
        }
        current_columns.push_back({col_name, data_type, is_pri});
    }
    save_current_table();

    std::unique_lock<std::shared_mutex> lock(g_schema_cache_mutex);
    g_schema_cache.clear();
    for (auto& [db_name, tables] : temp_tables_by_db) {
        g_schema_cache[db_name] = schema::MySQLSchema(tables);
    }
}

vef_next_wakeup_t schema_cache_worker(vef_wakeup_reason_t reason,
                                      struct vef_thread_handle_t *handle,
                                      void *) {
    if (reason == VEF_WAKEUP_ENABLE) {
        return {1, -1};
    }
    if (reason == VEF_WAKEUP_DISABLE) {
        return {};
    }
    if (reason == VEF_WAKEUP_PERIODIC) {
        updateSchemaCache(handle);
        return {5000, -1};
    }
    return {};
}

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

schema::MySQLSchema getSchemaFromMySQL(std::string_view db_name, std::string& err_msg) {
    std::string db_str(db_name);
    // Secure input validation: database name must be alphanumeric or underscores
    if (db_str.empty() || db_str.length() > 64) {
        err_msg = "Invalid database name length";
        return schema::MySQLSchema();
    }
    for (char c : db_str) {
        if (!std::isalnum(c) && c != '_') {
            err_msg = "Invalid characters in database name";
            return schema::MySQLSchema();
        }
    }

    std::shared_lock<std::shared_mutex> lock(g_schema_cache_mutex);
    auto it = g_schema_cache.find(db_str);
    if (it != g_schema_cache.end()) {
        return it->second;
    }

    err_msg = "Database '" + db_str + "' does not exist";
    return schema::MySQLSchema();
}

schema::MySQLSchema resolveSchema(std::string_view schema_or_db, std::string& err_msg) {
    if (schema_or_db.find(':') != std::string::npos) {
        return parseSchema(schema_or_db);
    } else {
        return getSchemaFromMySQL(schema_or_db, err_msg);
    }
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

static vsql::preview_thread_worker::ThreadWorkerCapability<&schema_cache_worker>
    g_thread_worker_cap{"schema_cache"};

void vsql_schema_cache_ready_impl(IntResult out) {
    std::shared_lock<std::shared_mutex> lock(g_schema_cache_mutex);
    out.set(!g_schema_cache.empty() ? 1 : 0);
}

void vsql_corrupt_impl(StringArg query, StringArg corruption_type, StringArg schema_or_db, StringResult out) {
    if (query.is_null() || schema_or_db.is_null()) {
        out.set_null();
        return;
    }

    std::string query_val(query.value());
    std::string schema_or_db_val(schema_or_db.value());
    std::string corruption_type_val = corruption_type.is_null() ? "" : std::string(corruption_type.value());

    // Determine corruption type
    CorruptionEngine::CorruptionType type = getRandomCorruptionType();
    if (!corruption_type_val.empty()) {
        if (corruption_type_val != "RANDOM") {
            auto parsed_type = parseCorruptionType(corruption_type_val);
            if (!parsed_type) {
                out.error("Invalid corruption type specified");
                return;
            }
            type = *parsed_type;
        }
    }

    // Determine schema
    std::string err_msg;
    schema::MySQLSchema schemaObj = resolveSchema(schema_or_db_val, err_msg);
    if (!err_msg.empty()) {
        out.error(err_msg.c_str());
        return;
    }

    CorruptionEngine engine(schemaObj);
    std::string corrupted = engine.applyCorruption(query_val, type);

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
    .with(g_sql_query_cap)
    .with(g_thread_worker_cap)
    .func(make_func<&vsql_corrupt_impl>("vsql_corrupt")
      .returns(STRING)
      .param(STRING)
      .param(STRING)
      .param(STRING)
      .buffer_size(65535)
      .build())
    .func(make_func<&vsql_schema_cache_ready_impl>("vsql_schema_cache_ready")
      .returns(INT)
      .no_params()
      .build())
)

