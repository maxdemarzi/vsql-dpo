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

#ifndef REBAD_SCHEMA_H
#define REBAD_SCHEMA_H

#include <string>
#include <vector>
#include <algorithm>
#include <random>
#include <cctype>

namespace schema {

inline bool caseInsensitiveCompare(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), [](char char_a, char char_b) {
        return std::tolower(char_a) == std::tolower(char_b);
    });
}

struct MySQLColumn {
    std::string name;
    std::string type; // e.g. "INT", "VARCHAR", etc.
    bool primaryKey = false;

    std::string getName() const { return name; }
    std::string getType() const { return type; }
    bool isPrimaryKey() const { return primaryKey; }
};

struct MySQLTable {
    std::string name;
    std::vector<MySQLColumn> columns;

    std::string getName() const { return name; }
    const std::vector<MySQLColumn>& getColumns() const { return columns; }
};

class MySQLSchema {
private:
    std::vector<MySQLTable> tables;

public:
    MySQLSchema() = default;
    explicit MySQLSchema(std::vector<MySQLTable> t) : tables(std::move(t)) {}

    const std::vector<MySQLTable>& getDatabaseTables() const {
        return tables;
    }

    const MySQLTable* getRandomTable() const {
        if (tables.empty()) return nullptr;
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, tables.size() - 1);
        return &tables[dis(gen)];
    }

    const MySQLColumn* getRandomColumn(const MySQLTable* table) const {
        if (!table || table->columns.empty()) return nullptr;
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, table->columns.size() - 1);
        return &table->columns[dis(gen)];
    }

    std::string getColumnTypeName(const std::string& colName) const {
        for (const auto& table : tables) {
            for (const auto& col : table.columns) {
                if (caseInsensitiveCompare(col.name, colName)) {
                    return col.type;
                }
            }
        }
        return "";
    }

    bool isStringColumn(const std::string& colName) const {
        std::string typeName = getColumnTypeName(colName);
        if (typeName.empty()) return false;
        std::string upper = typeName;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        return upper.find("CHAR") != std::string::npos || 
               upper.find("TEXT") != std::string::npos || 
               upper.find("STRING") != std::string::npos;
    }
};

} // namespace schema

#endif // REBAD_SCHEMA_H
