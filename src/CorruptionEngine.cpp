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

#include "CorruptionEngine.h"
#include "SQLUnparser.h"
#include <iostream>
#include <regex>
#include <unordered_set>
#include <algorithm>
#include <random>
#include <cstring>

CorruptionEngine::CorruptionEngine(schema::MySQLSchema schema) : schema_(std::move(schema)) {}

std::string CorruptionEngine::applyCorruption(const std::string& validSql, CorruptionType type) {
    hsql::SQLParserResult result;
    hsql::SQLParser::parse(validSql, &result);

    if (!result.isValid() || result.size() == 0) {
        std::cerr << "Parser failed to parse query for corruption: " << validSql << std::endl;
        return validSql;
    }

    hsql::SQLStatement* stmt = result.getMutableStatement(0);
    visitStatement(stmt, type);

    std::string corruptedSql = SQLUnparser::toString(stmt);
    if (corruptedSql.empty()) {
        return validSql;
    }

    return restoreOriginalCasing(corruptedSql, validSql);
}

void CorruptionEngine::visitStatement(hsql::SQLStatement* stmt, CorruptionType type) {
    if (!stmt) return;

    if (stmt->isType(hsql::kStmtSelect)) {
        auto* select = static_cast<hsql::SelectStatement*>(stmt);

        // Apply statement/select-level corruptions
        if (type == CorruptionType::MISSING_GROUP_BY) {
            if (select->groupBy) {
                delete select->groupBy;
                select->groupBy = nullptr;
            }
        } else if (type == CorruptionType::HALLUCINATED_COLUMN) {
            if (select->selectList && !select->selectList->empty()) {
                std::string originalCol = getAliasOrColumnName(select->selectList->back());
                std::string hallCol = generateTypo(originalCol);
                select->selectList->push_back(hsql::Expr::makeColumnRef(strdup(hallCol.c_str())));
            }
        } else if (type == CorruptionType::ALIAS_SHADOWING) {
            if (select->selectList && select->selectList->size() > 1) {
                std::vector<std::string> existingNames;
                for (const auto* node : *select->selectList) {
                    std::string name = getAliasOrColumnName(node);
                    if (!name.empty()) {
                        existingNames.push_back(name);
                    }
                }
                if (!existingNames.empty()) {
                    std::string targetName = getAliasOrColumnName(select->selectList->back());
                    std::string shadowAlias = "";
                    for (const auto& name : existingNames) {
                        if (!schema::caseInsensitiveCompare(name, targetName)) {
                            shadowAlias = name;
                            break;
                        }
                    }
                    if (shadowAlias.empty()) {
                        for (const auto& t : schema_.getDatabaseTables()) {
                            for (const auto& c : t.columns) {
                                if (!schema::caseInsensitiveCompare(c.name, targetName)) {
                                    shadowAlias = c.name;
                                    break;
                                }
                            }
                            if (!shadowAlias.empty()) break;
                        }
                    }
                    if (shadowAlias.empty()) {
                        shadowAlias = "shadowed_alias";
                    }

                    auto* lastNode = select->selectList->back();
                    if (lastNode->alias) {
                        free(lastNode->alias);
                    }
                    lastNode->alias = strdup(shadowAlias.c_str());
                }
            }
        } else if (type == CorruptionType::AGGREGATE_MISUSE) {
            if (select->whereClause) {
                auto* original = select->whereClause;
                hsql::Expr* wrapped = hsql::Expr::makeFunctionRef(strdup("MAX"), new std::vector<hsql::Expr*>{original}, false, nullptr);
                select->whereClause = wrapped;
            } else if (select->selectList && !select->selectList->empty()) {
                auto* original = select->selectList->at(0);
                hsql::Expr* wrapped = hsql::Expr::makeFunctionRef(strdup("MAX"), new std::vector<hsql::Expr*>{original}, false, nullptr);
                select->selectList->at(0) = wrapped;
            }
        } else if (type == CorruptionType::NON_BOOLEAN_WHERE) {
            std::string val = "not_a_boolean";
            const auto* t = schema_.getRandomTable();
            if (t) {
                const auto* c = schema_.getRandomColumn(t);
                if (c) {
                    val = (rand() % 2 == 0) ? c->name : c->name + "_value";
                }
            }
            delete select->whereClause;
            select->whereClause = hsql::Expr::makeLiteral(strdup(val.c_str()));
        } else if (type == CorruptionType::UNNECESSARY_JOIN) {
            if (select->fromTable) {
                std::string joinTable = "unnecessary_table";
                const auto* t = schema_.getRandomTable();
                if (t) {
                    joinTable = t->name;
                }
                auto* left = select->fromTable;
                auto* right = new hsql::TableRef(hsql::kTableName);
                right->name = strdup(joinTable.c_str());
                right->schema = nullptr;
                right->alias = nullptr;

                auto* join = new hsql::JoinDefinition();
                join->left = left;
                join->right = right;
                join->condition = hsql::Expr::makeLiteral(true);
                join->type = hsql::kJoinInner;

                auto* newFrom = new hsql::TableRef(hsql::kTableJoin);
                newFrom->join = join;
                newFrom->alias = nullptr;
                select->fromTable = newFrom;
            }
        } else if (type == CorruptionType::WILDCARD_HALLUCINATION) {
            if (select->selectList) {
                for (auto* expr : *select->selectList) {
                    delete expr;
                }
                select->selectList->clear();
                select->selectList->push_back(new hsql::Expr(hsql::kExprStar));
            }
        } else if (type == CorruptionType::DISTINCT_MUTATION) {
            select->selectDistinct = !select->selectDistinct;
        } else if (type == CorruptionType::HAVING_CLAUSE_MUTATION) {
            if (select->whereClause && select->groupBy && !select->groupBy->having) {
                select->groupBy->having = select->whereClause;
                select->whereClause = nullptr;
            } else if (select->groupBy && select->groupBy->having && !select->whereClause) {
                select->whereClause = select->groupBy->having;
                select->groupBy->having = nullptr;
            }
        } else if (type == CorruptionType::ORDER_BY_DIRECTION_SWAP) {
            if (select->order && !select->order->empty()) {
                auto* ord = select->order->at(0);
                ord->type = (ord->type == hsql::kOrderDesc) ? hsql::kOrderAsc : hsql::kOrderDesc;
            }
        } else if (type == CorruptionType::MISSING_WHERE_CLAUSE) {
            delete select->whereClause;
            select->whereClause = nullptr;
        } else if (type == CorruptionType::LIMIT_MUTATION) {
            if (select->limit) {
                delete select->limit;
                select->limit = nullptr;
            } else {
                select->limit = new hsql::LimitDescription(hsql::Expr::makeLiteral(1LL), nullptr);
            }
        } else if (type == CorruptionType::OFFSET_MUTATION) {
            if (select->limit) {
                if (select->limit->offset) {
                    delete select->limit->offset;
                    select->limit->offset = nullptr;
                } else {
                    select->limit->offset = hsql::Expr::makeLiteral(1LL);
                }
            } else {
                select->limit = new hsql::LimitDescription(nullptr, hsql::Expr::makeLiteral(1LL));
            }
        } else if (type == CorruptionType::UNION_ALL_MUTATION) {
            if (select->setOperations) {
                for (auto* setOp : *select->setOperations) {
                    if (setOp->setType == hsql::kSetUnion) {
                        setOp->isAll = !setOp->isAll;
                    }
                }
            }
        } else if (type == CorruptionType::SET_OPERATION_SWAP) {
            if (select->setOperations) {
                for (auto* setOp : *select->setOperations) {
                    if (setOp->setType == hsql::kSetIntersect) {
                        setOp->setType = hsql::kSetExcept;
                    } else if (setOp->setType == hsql::kSetExcept) {
                        setOp->setType = hsql::kSetIntersect;
                    }
                }
            }
        }

        // Recurse sub-clauses
        visitSelect(select, type);
    }
}

void CorruptionEngine::visitSelect(hsql::SelectStatement* select, CorruptionType type) {
    if (!select) return;

    if (select->fromTable) {
        visitTableRef(select->fromTable, type);
    }

    if (select->selectList) {
        for (auto* expr : *select->selectList) {
            visitExpr(expr, type);
        }
    }

    if (select->whereClause) {
        visitExpr(select->whereClause, type);
    }

    if (select->groupBy) {
        if (select->groupBy->columns) {
            for (auto* expr : *select->groupBy->columns) {
                visitExpr(expr, type);
            }
        }
        if (select->groupBy->having) {
            visitExpr(select->groupBy->having, type);
        }
    }

    if (select->setOperations) {
        for (auto* setOp : *select->setOperations) {
            if (setOp->nestedSelectStatement) {
                visitSelect(setOp->nestedSelectStatement, type);
            }
        }
    }

    if (select->order) {
        for (auto* ord : *select->order) {
            visitExpr(ord->expr, type);
        }
    }
}

void CorruptionEngine::visitExpr(hsql::Expr* expr, CorruptionType type) {
    if (!expr) return;

    // Apply expression level corruptions
    if (type == CorruptionType::INVALID_NESTING) {
        if (expr->type == hsql::kExprFunctionRef && schema::caseInsensitiveCompare(expr->name, "COUNT")) {
            hsql::Expr* inner = new hsql::Expr(*expr);
            expr->type = hsql::kExprFunctionRef;
            // Note: do not free(expr->name) here because inner->name points to the same memory and now owns it
            expr->name = strdup("MAX");
            expr->exprList = new std::vector<hsql::Expr*>{inner};
            expr->distinct = false;
            expr->expr = nullptr;
            expr->expr2 = nullptr;
            expr->select = nullptr;
            expr->alias = nullptr;
            expr->windowDescription = nullptr;
            return;
        }
    } else if (type == CorruptionType::TYPE_INCOMPATIBILITY) {
        if (expr->type == hsql::kExprOperator && expr->opType == hsql::kOpEquals) {
            bool isStr = false;
            if (expr->expr && expr->expr->type == hsql::kExprColumnRef) {
                isStr = schema_.isStringColumn(expr->expr->name);
            }
            delete expr->expr2;
            if (isStr) {
                expr->expr2 = hsql::Expr::makeLiteral(1234LL);
            } else {
                std::string val = "mismatch";
                const auto* table = schema_.getRandomTable();
                if (table) {
                    const auto* col = schema_.getRandomColumn(table);
                    if (col) val = col->name;
                }
                expr->expr2 = hsql::Expr::makeLiteral(strdup(val.c_str()));
            }
            return;
        }
    } else if (type == CorruptionType::COMPARISON_WITH_NULL) {
        if (expr->type == hsql::kExprOperator && expr->opType == hsql::kOpEquals) {
            delete expr->expr2;
            expr->expr2 = hsql::Expr::makeNullLiteral();
            return;
        }
    } else if (type == CorruptionType::WRONG_AGGREGATION) {
        if (expr->type == hsql::kExprFunctionRef) {
            if (schema::caseInsensitiveCompare(expr->name, "MAX")) {
                free(expr->name);
                expr->name = strdup("MIN");
            } else if (schema::caseInsensitiveCompare(expr->name, "MIN")) {
                free(expr->name);
                expr->name = strdup("MAX");
            } else if (schema::caseInsensitiveCompare(expr->name, "SUM")) {
                free(expr->name);
                expr->name = strdup("AVG");
            } else if (schema::caseInsensitiveCompare(expr->name, "AVG")) {
                free(expr->name);
                expr->name = strdup("SUM");
            }
        }
    } else if (type == CorruptionType::LOGICAL_OPERATOR_SWAP) {
        if (expr->type == hsql::kExprOperator) {
            if (expr->opType == hsql::kOpAnd) {
                expr->opType = hsql::kOpOr;
            } else if (expr->opType == hsql::kOpOr) {
                expr->opType = hsql::kOpAnd;
            }
        }
    } else if (type == CorruptionType::COMPARISON_OPERATOR_SWAP) {
        if (expr->type == hsql::kExprOperator) {
            if (expr->opType == hsql::kOpEquals) {
                expr->opType = hsql::kOpNotEquals;
            } else if (expr->opType == hsql::kOpNotEquals) {
                expr->opType = hsql::kOpEquals;
            } else if (expr->opType == hsql::kOpGreater) {
                expr->opType = hsql::kOpLessEq;
            } else if (expr->opType == hsql::kOpLess) {
                expr->opType = hsql::kOpGreaterEq;
            } else if (expr->opType == hsql::kOpGreaterEq) {
                expr->opType = hsql::kOpLess;
            } else if (expr->opType == hsql::kOpLessEq) {
                expr->opType = hsql::kOpGreater;
            }
        }
    } else if (type == CorruptionType::MATH_OPERATOR_SWAP) {
        if (expr->type == hsql::kExprOperator) {
            if (expr->opType == hsql::kOpPlus) {
                expr->opType = hsql::kOpMinus;
            } else if (expr->opType == hsql::kOpMinus) {
                expr->opType = hsql::kOpPlus;
            } else if (expr->opType == hsql::kOpAsterisk) {
                expr->opType = hsql::kOpSlash;
            } else if (expr->opType == hsql::kOpSlash) {
                expr->opType = hsql::kOpAsterisk;
            }
        }
    } else if (type == CorruptionType::LIKE_TO_EQUALS_SWAP) {
        if (expr->type == hsql::kExprOperator) {
            if (expr->opType == hsql::kOpLike) {
                expr->opType = hsql::kOpEquals;
            } else if (expr->opType == hsql::kOpEquals) {
                expr->opType = hsql::kOpLike;
            }
        }
    } else if (type == CorruptionType::IN_TO_EQUALS) {
        if (expr->type == hsql::kExprOperator && expr->opType == hsql::kOpIn) {
            if (expr->select) {
                expr->expr2 = hsql::Expr::makeSelect(expr->select);
                expr->select = nullptr;
            } else if (expr->exprList && !expr->exprList->empty()) {
                expr->expr2 = expr->exprList->at(0);
                expr->exprList->erase(expr->exprList->begin());
            }
            expr->opType = hsql::kOpEquals;
        }
    } else if (type == CorruptionType::IS_NULL_INVERSION) {
        if (expr->type == hsql::kExprOperator && expr->opType == hsql::kOpIsNull) {
            hsql::Expr* inner = new hsql::Expr(*expr);
            expr->type = hsql::kExprOperator;
            expr->opType = hsql::kOpNot;
            expr->expr = inner;
            expr->expr2 = nullptr;
            expr->name = nullptr;
            expr->table = nullptr;
            expr->schema = nullptr;
            expr->alias = nullptr;
            expr->exprList = nullptr;
            expr->select = nullptr;
            expr->windowDescription = nullptr;
            return;
        }
        if (expr->type == hsql::kExprOperator && expr->opType == hsql::kOpNot 
            && expr->expr && expr->expr->type == hsql::kExprOperator && expr->expr->opType == hsql::kOpIsNull) {
            hsql::Expr* inner = expr->expr;
            expr->type = inner->type;
            expr->opType = inner->opType;
            expr->expr = inner->expr;
            expr->expr2 = inner->expr2;
            inner->expr = nullptr;
            delete inner;
            return;
        }
    } else if (type == CorruptionType::BETWEEN_REVERSAL) {
        if (expr->type == hsql::kExprOperator && expr->opType == hsql::kOpBetween) {
            if (expr->exprList && expr->exprList->size() >= 2) {
                hsql::Expr* temp = expr->exprList->at(0);
                expr->exprList->at(0) = expr->exprList->at(1);
                expr->exprList->at(1) = temp;
            }
        }
    } else if (type == CorruptionType::EXISTS_INVERSION) {
        if (expr->type == hsql::kExprOperator && expr->opType == hsql::kOpExists) {
            hsql::Expr* inner = new hsql::Expr(*expr);
            expr->type = hsql::kExprOperator;
            expr->opType = hsql::kOpNot;
            expr->expr = inner;
            expr->expr2 = nullptr;
            expr->select = nullptr;
            expr->name = nullptr;
            expr->table = nullptr;
            expr->schema = nullptr;
            expr->alias = nullptr;
            expr->exprList = nullptr;
            expr->windowDescription = nullptr;
            return;
        }
        if (expr->type == hsql::kExprOperator && expr->opType == hsql::kOpNot 
            && expr->expr && expr->expr->type == hsql::kExprOperator && expr->expr->opType == hsql::kOpExists) {
            hsql::Expr* inner = expr->expr;
            expr->type = inner->type;
            expr->opType = inner->opType;
            expr->select = inner->select;
            expr->expr = nullptr;
            delete inner;
            return;
        }
    } else if (type == CorruptionType::STRING_FUNCTION_MUTATION) {
        if (expr->type == hsql::kExprFunctionRef) {
            if (schema::caseInsensitiveCompare(expr->name, "UPPER")) {
                free(expr->name);
                expr->name = strdup("LOWER");
            } else if (schema::caseInsensitiveCompare(expr->name, "LOWER")) {
                free(expr->name);
                expr->name = strdup("UPPER");
            }
        }
    } else if (type == CorruptionType::IN_INVERSION) {
        if (expr->type == hsql::kExprOperator && expr->opType == hsql::kOpIn) {
            hsql::Expr* inner = new hsql::Expr(*expr);
            expr->type = hsql::kExprOperator;
            expr->opType = hsql::kOpNot;
            expr->expr = inner;
            expr->expr2 = nullptr;
            expr->select = nullptr;
            expr->exprList = nullptr;
            expr->name = nullptr;
            expr->table = nullptr;
            expr->schema = nullptr;
            expr->alias = nullptr;
            expr->windowDescription = nullptr;
            return;
        }
        if (expr->type == hsql::kExprOperator && expr->opType == hsql::kOpNot 
            && expr->expr && expr->expr->type == hsql::kExprOperator && expr->expr->opType == hsql::kOpIn) {
            hsql::Expr* inner = expr->expr;
            expr->type = inner->type;
            expr->opType = inner->opType;
            expr->expr = inner->expr;
            expr->expr2 = inner->expr2;
            expr->exprList = inner->exprList;
            expr->select = inner->select;
            
            inner->expr = nullptr;
            inner->expr2 = nullptr;
            inner->exprList = nullptr;
            inner->select = nullptr;
            delete inner;
            return;
        }
    } else if (type == CorruptionType::AGGREGATE_DISTINCT_MUTATION) {
        if (expr->type == hsql::kExprFunctionRef) {
            expr->distinct = !expr->distinct;
        }
    } else if (type == CorruptionType::CASE_CONDITION_SWAP) {
        if (expr->type == hsql::kExprOperator && expr->opType == hsql::kOpCase) {
            if (expr->exprList && !expr->exprList->empty() && expr->expr2) {
                auto* firstCase = expr->exprList->at(0);
                if (firstCase && firstCase->expr2) {
                    hsql::Expr* temp = firstCase->expr2;
                    firstCase->expr2 = expr->expr2;
                    expr->expr2 = temp;
                }
            }
        }
    }

    // Recurse
    visitExpr(expr->expr, type);
    visitExpr(expr->expr2, type);
    if (expr->exprList) {
        for (auto* child : *expr->exprList) {
            visitExpr(child, type);
        }
    }
    if (expr->select) {
        visitSelect(expr->select, type);
    }
}

void CorruptionEngine::visitTableRef(hsql::TableRef* table, CorruptionType type) {
    if (!table) return;

    // Apply table-level corruptions
    if (type == CorruptionType::WRONG_JOIN_KEY) {
        if (table->type == hsql::kTableJoin) {
            auto* join = table->join;
            std::string leftTable = "fake_table";
            std::string leftCol = "fake_col";
            std::string rightTable = "other_table";
            std::string rightCol = "wrong_col";

            const auto* t1 = schema_.getRandomTable();
            if (t1) {
                leftTable = t1->name;
                const auto* c1 = schema_.getRandomColumn(t1);
                if (c1) leftCol = c1->name;
            }
            const auto* t2 = schema_.getRandomTable();
            if (t2) {
                rightTable = t2->name;
                const auto* c2 = schema_.getRandomColumn(t2);
                if (c2) rightCol = c2->name;
            }

            delete join->condition;
            join->condition = hsql::Expr::makeOpBinary(
                hsql::Expr::makeColumnRef(strdup(leftTable.c_str()), strdup(leftCol.c_str())),
                hsql::kOpEquals,
                hsql::Expr::makeColumnRef(strdup(rightTable.c_str()), strdup(rightCol.c_str()))
            );
        }
    } else if (type == CorruptionType::JOIN_ON_TRUE) {
        if (table->type == hsql::kTableJoin) {
            auto* join = table->join;
            delete join->condition;
            join->condition = hsql::Expr::makeLiteral(true);
        }
    } else if (type == CorruptionType::JOIN_TYPE_MUTATION) {
        if (table->type == hsql::kTableJoin) {
            auto* join = table->join;
            join->type = (join->type == hsql::kJoinInner) ? hsql::kJoinLeft : hsql::kJoinInner;
        }
    } else if (type == CorruptionType::OUTER_JOIN_DIRECTION_SWAP) {
        if (table->type == hsql::kTableJoin) {
            auto* join = table->join;
            if (join->type == hsql::kJoinLeft) {
                join->type = hsql::kJoinRight;
            } else if (join->type == hsql::kJoinRight) {
                join->type = hsql::kJoinLeft;
            }
        }
    }

    // Recurse
    switch (table->type) {
        case hsql::kTableName:
            break;
        case hsql::kTableSelect:
            if (table->select) visitSelect(table->select, type);
            break;
        case hsql::kTableJoin:
            if (table->join) {
                visitTableRef(table->join->left, type);
                visitTableRef(table->join->right, type);
            }
            break;
        case hsql::kTableCrossProduct:
            if (table->list) {
                for (auto* child : *table->list) {
                    visitTableRef(child, type);
                }
            }
            break;
    }
}

std::string CorruptionEngine::restoreOriginalCasing(const std::string& sql, const std::string& validSql) {
    std::unordered_map<std::string, std::string> originalCasingMap;
    std::regex wordPattern("\\b[a-zA-Z_0-9]+\\b");
    
    // Capture original casing
    auto validBegin = std::sregex_iterator(validSql.begin(), validSql.end(), wordPattern);
    auto validEnd = std::sregex_iterator();
    for (auto i = validBegin; i != validEnd; ++i) {
        std::string word = i->str();
        std::string lowerWord = word;
        std::transform(lowerWord.begin(), lowerWord.end(), lowerWord.begin(), ::tolower);
        originalCasingMap[lowerWord] = word;
    }
    
    // Reconstruct corrupted query with original casing
    std::string result = sql;
    auto sqlBegin = std::sregex_iterator(sql.begin(), sql.end(), wordPattern);
    auto sqlEnd = std::sregex_iterator();
    
    ptrdiff_t offset = 0;
    for (auto i = sqlBegin; i != sqlEnd; ++i) {
        std::string word = i->str();
        std::string lowerWord = word;
        std::transform(lowerWord.begin(), lowerWord.end(), lowerWord.begin(), ::tolower);
        
        auto it = originalCasingMap.find(lowerWord);
        if (it != originalCasingMap.end() && it->second != word) {
            size_t matchPos = i->position() + offset;
            result.replace(matchPos, word.length(), it->second);
            offset += (ptrdiff_t)it->second.length() - (ptrdiff_t)word.length();
        }
    }
    return result;
}

std::string CorruptionEngine::getAliasOrColumnName(const hsql::Expr* expr) {
    if (!expr) return "";
    if (expr->alias) {
        return expr->alias;
    }
    if (expr->type == hsql::kExprColumnRef) {
        return expr->name;
    }
    return "";
}

std::string CorruptionEngine::generateTypo(const std::string& original) {
    if (original.empty()) return "fake_column";
    
    std::unordered_set<std::string> existingNames;
    for (const auto& table : schema_.getDatabaseTables()) {
        for (const auto& col : table.columns) {
            std::string lower = col.name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            existingNames.insert(lower);
        }
    }
    
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> mutationDist(0, 2);
    
    for (int attempt = 0; attempt < 10; ++attempt) {
        std::string candidate = original;
        size_t len = candidate.length();
        int mutationType = mutationDist(gen);
        
        if (mutationType == 0 && len > 1) {
            // Swap adjacent characters
            std::uniform_int_distribution<> idxDist(0, len - 2);
            int idx = idxDist(gen);
            std::swap(candidate[idx], candidate[idx + 1]);
        } else if (mutationType == 1 && len > 2) {
            // Delete a character
            std::uniform_int_distribution<> idxDist(0, len - 1);
            int idx = idxDist(gen);
            candidate.erase(idx, 1);
        } else {
            // Duplicate a character
            std::uniform_int_distribution<> idxDist(0, len - 1);
            int idx = idxDist(gen);
            candidate.insert(idx, 1, candidate[idx]);
        }
        
        std::string lowerCand = candidate;
        std::transform(lowerCand.begin(), lowerCand.end(), lowerCand.begin(), ::tolower);
        
        if (candidate != original && existingNames.find(lowerCand) == existingNames.end()) {
            return candidate;
        }
    }
    
    return original + "x";
}
