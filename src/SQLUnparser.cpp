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

#include "SQLUnparser.h"
#include <sstream>
#include <algorithm>

std::string SQLUnparser::toString(const hsql::SQLStatement* stmt) {
    if (!stmt) return "";
    if (stmt->isType(hsql::kStmtSelect)) {
        return selectToString(static_cast<const hsql::SelectStatement*>(stmt));
    }
    // Simple fallback if it's not a select statement
    return "";
}

std::string SQLUnparser::exprToString(const hsql::Expr* expr) {
    if (!expr) return "";

    std::string res;
    switch (expr->type) {
        case hsql::kExprStar:
            res = "*";
            break;
        case hsql::kExprColumnRef:
            res = (expr->table ? std::string(expr->table) + "." : "") + expr->name;
            break;
        case hsql::kExprLiteralString:
            res = "'" + std::string(expr->name) + "'";
            break;
        case hsql::kExprLiteralInt:
            res = expr->isBoolLiteral ? (expr->ival ? "TRUE" : "FALSE") : std::to_string(expr->ival);
            break;
        case hsql::kExprLiteralFloat:
            res = std::to_string(expr->fval);
            break;
        case hsql::kExprLiteralNull:
            res = "NULL";
            break;
        case hsql::kExprFunctionRef: {
            res = std::string(expr->name) + "(";
            if (expr->distinct) {
                res += "DISTINCT ";
            }
            if (expr->exprList) {
                res += exprListToString(expr->exprList, ", ");
            }
            res += ")";
            break;
        }
        case hsql::kExprOperator:
            res = opToString(expr);
            break;
        case hsql::kExprSelect:
            res = "(" + selectToString(expr->select) + ")";
            break;
        case hsql::kExprParameter:
            res = "?";
            break;
        default:
            res = "";
            break;
    }
    if (expr->alias) {
        res += " AS " + std::string(expr->alias);
    }
    return res;
}

std::string SQLUnparser::tableRefToString(const hsql::TableRef* table) {
    if (!table) return "";
    std::string res;
    switch (table->type) {
        case hsql::kTableName:
            res = table->name;
            if (table->schema) {
                res = std::string(table->schema) + "." + res;
            }
            break;
        case hsql::kTableSelect:
            res = "(" + selectToString(table->select) + ")";
            break;
        case hsql::kTableJoin:
            res = joinToString(table->join);
            break;
        case hsql::kTableCrossProduct: {
            if (table->list) {
                for (size_t i = 0; i < table->list->size(); ++i) {
                    if (i > 0) res += ", ";
                    res += tableRefToString(table->list->at(i));
                }
            }
            break;
        }
    }
    if (table->alias) {
        res += " AS " + std::string(table->alias->name);
    }
    return res;
}

std::string SQLUnparser::joinToString(const hsql::JoinDefinition* join) {
    if (!join) return "";
    std::string res = tableRefToString(join->left);
    switch (join->type) {
        case hsql::kJoinInner: res += " INNER JOIN "; break;
        case hsql::kJoinLeft:  res += " LEFT JOIN "; break;
        case hsql::kJoinRight: res += " RIGHT JOIN "; break;
        case hsql::kJoinFull: res += " FULL OUTER JOIN "; break;
        case hsql::kJoinCross: res += " CROSS JOIN "; break;
        case hsql::kJoinNatural: res += " NATURAL JOIN "; break;
        default:               res += " JOIN "; break;
    }
    res += tableRefToString(join->right);
    if (join->condition) {
        res += " ON " + exprToString(join->condition);
    }
    return res;
}

std::string SQLUnparser::exprListToString(const std::vector<hsql::Expr*>* list, const std::string& sep) {
    if (!list) return "";
    std::string res;
    for (size_t i = 0; i < list->size(); ++i) {
        if (i > 0) res += sep;
        res += exprToString(list->at(i));
    }
    return res;
}

std::string SQLUnparser::opToString(const hsql::Expr* expr) {
    if (!expr) return "";

    // Special cases for CASE WHEN
    if (expr->opType == hsql::kOpCase) {
        std::string res = "CASE ";
        if (expr->expr) {
            res += exprToString(expr->expr) + " ";
        }
        if (expr->exprList) {
            res += exprListToString(expr->exprList, " ");
        }
        if (expr->expr2) {
            res += " ELSE " + exprToString(expr->expr2);
        }
        res += " END";
        return res;
    }
    if (expr->opType == hsql::kOpCaseListElement) {
        return "WHEN " + exprToString(expr->expr) + " THEN " + exprToString(expr->expr2);
    }
    if (expr->opType == hsql::kOpBetween) {
        if (expr->expr && expr->exprList && expr->exprList->size() >= 2) {
            return exprToString(expr->expr) + " BETWEEN " + exprToString(expr->exprList->at(0)) + " AND " + exprToString(expr->exprList->at(1));
        }
        if (expr->expr && expr->expr2 && expr->exprList && !expr->exprList->empty()) {
            return exprToString(expr->expr) + " BETWEEN " + exprToString(expr->expr2) + " AND " + exprToString(expr->exprList->at(0));
        }
        return exprToString(expr->expr);
    }
    if (expr->opType == hsql::kOpIn) {
        return exprToString(expr->expr) + " IN (" + 
               (expr->select ? selectToString(expr->select) : exprListToString(expr->exprList, ", ")) + ")";
    }

    // Unary operators
    switch (expr->opType) {
        case hsql::kOpNot:
            // Try to render NOT (x IS NULL) as x IS NOT NULL
            if (expr->expr && expr->expr->type == hsql::kExprOperator && expr->expr->opType == hsql::kOpIsNull) {
                return exprToString(expr->expr->expr) + " IS NOT NULL";
            }
            return "NOT (" + exprToString(expr->expr) + ")";
        case hsql::kOpIsNull:
            return exprToString(expr->expr) + " IS NULL";
        case hsql::kOpExists:
            return "EXISTS (" + selectToString(expr->select) + ")";
        case hsql::kOpUnaryMinus:
            return "-" + exprToString(expr->expr);
        default:
            break;
    }

    // Binary operators
    std::string opStr;
    switch (expr->opType) {
        case hsql::kOpEquals: opStr = "="; break;
        case hsql::kOpNotEquals: opStr = "!="; break;
        case hsql::kOpLess: opStr = "<"; break;
        case hsql::kOpLessEq: opStr = "<="; break;
        case hsql::kOpGreater: opStr = ">"; break;
        case hsql::kOpGreaterEq: opStr = ">="; break;
        case hsql::kOpLike: opStr = "LIKE"; break;
        case hsql::kOpNotLike: opStr = "NOT LIKE"; break;
        case hsql::kOpAnd: opStr = "AND"; break;
        case hsql::kOpOr: opStr = "OR"; break;
        case hsql::kOpPlus: opStr = "+"; break;
        case hsql::kOpMinus: opStr = "-"; break;
        case hsql::kOpAsterisk: opStr = "*"; break;
        case hsql::kOpSlash: opStr = "/"; break;
        default: opStr = "?"; break;
    }
    return "(" + exprToString(expr->expr) + " " + opStr + " " + exprToString(expr->expr2) + ")";
}

std::string SQLUnparser::selectToString(const hsql::SelectStatement* stmt) {
    if (!stmt) return "";
    std::string sql = "SELECT ";
    if (stmt->selectDistinct) sql += "DISTINCT ";
    if (stmt->selectList) sql += exprListToString(stmt->selectList, ", ");
    if (stmt->fromTable) sql += " FROM " + tableRefToString(stmt->fromTable);
    if (stmt->whereClause) sql += " WHERE " + exprToString(stmt->whereClause);
    
    if (stmt->groupBy) {
        sql += " GROUP BY " + exprListToString(stmt->groupBy->columns, ", ");
        if (stmt->groupBy->having) {
            sql += " HAVING " + exprToString(stmt->groupBy->having);
        }
    }
    
    if (stmt->setOperations) {
        for (const auto* setOp : *stmt->setOperations) {
            switch (setOp->setType) {
                case hsql::kSetUnion:
                    sql += setOp->isAll ? " UNION ALL " : " UNION ";
                    break;
                case hsql::kSetIntersect:
                    sql += " INTERSECT ";
                    break;
                case hsql::kSetExcept:
                    sql += " EXCEPT ";
                    break;
            }
            sql += selectToString(setOp->nestedSelectStatement);
        }
    }

    if (stmt->order) {
        sql += " ORDER BY ";
        for (size_t i = 0; i < stmt->order->size(); ++i) {
            if (i > 0) sql += ", ";
            auto* ord = stmt->order->at(i);
            sql += exprToString(ord->expr) + (ord->type == hsql::kOrderDesc ? " DESC" : " ASC");
        }
    }
    
    if (stmt->limit) {
        sql += " LIMIT " + exprToString(stmt->limit->limit);
        if (stmt->limit->offset) {
            sql += " OFFSET " + exprToString(stmt->limit->offset);
        }
    }
    return sql;
}
