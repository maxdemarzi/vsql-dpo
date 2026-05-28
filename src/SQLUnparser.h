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

#ifndef VSQL_CORRUPTOR_SQL_UNPARSER_H
#define VSQL_CORRUPTOR_SQL_UNPARSER_H

#include <string>
#include <vector>
#include "SQLParser.h"

class SQLUnparser {
public:
    static std::string toString(const hsql::SQLStatement* stmt);
    static std::string selectToString(const hsql::SelectStatement* select);
    static std::string exprToString(const hsql::Expr* expr);
    static std::string tableRefToString(const hsql::TableRef* table);
    static std::string joinToString(const hsql::JoinDefinition* join);

private:
    static std::string exprListToString(const std::vector<hsql::Expr*>* list, const std::string& sep);
    static std::string opToString(const hsql::Expr* expr);
};

#endif // VSQL_CORRUPTOR_SQL_UNPARSER_H
