//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nlj_as_hash_join.cpp
//
// Identification: src/optimizer/nlj_as_hash_join.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <memory>
#include "catalog/column.h"
#include "catalog/schema.h"
#include "common/exception.h"
#include "common/macros.h"
#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/expressions/logic_expression.h"
#include "execution/plans/abstract_plan.h"
#include "execution/plans/filter_plan.h"
#include "execution/plans/hash_join_plan.h"
#include "execution/plans/nested_loop_join_plan.h"
#include "execution/plans/projection_plan.h"
#include "optimizer/optimizer.h"
#include "type/type_id.h"

namespace bustub {

/**
 * @brief Recursively walks a predicate that should be a conjunction (AND) of equi-conditions each
 * comparing a left-side (tuple_idx 0) column to a right-side (tuple_idx 1) column, collecting the
 * left/right join key expressions. Returns false if the predicate contains anything else.
 */
static auto ExtractEquiConditions(const AbstractExpressionRef &expr, std::vector<AbstractExpressionRef> *left_keys,
                                  std::vector<AbstractExpressionRef> *right_keys) -> bool {
  if (const auto *logic_expr = dynamic_cast<const LogicExpression *>(expr.get()); logic_expr != nullptr) {
    if (logic_expr->logic_type_ != LogicType::And) {
      return false;
    }
    return ExtractEquiConditions(expr->GetChildAt(0), left_keys, right_keys) &&
           ExtractEquiConditions(expr->GetChildAt(1), left_keys, right_keys);
  }

  const auto *cmp_expr = dynamic_cast<const ComparisonExpression *>(expr.get());
  if (cmp_expr == nullptr || cmp_expr->comp_type_ != ComparisonType::Equal) {
    return false;
  }

  const auto *lhs = dynamic_cast<const ColumnValueExpression *>(expr->GetChildAt(0).get());
  const auto *rhs = dynamic_cast<const ColumnValueExpression *>(expr->GetChildAt(1).get());
  if (lhs == nullptr || rhs == nullptr || lhs->GetTupleIdx() == rhs->GetTupleIdx()) {
    return false;
  }
  // The column belonging to the left side of the join (tuple_idx 0) may appear on either side of `=`.
  if (lhs->GetTupleIdx() == 0) {
    left_keys->push_back(expr->GetChildAt(0));
    right_keys->push_back(expr->GetChildAt(1));
  } else {
    left_keys->push_back(expr->GetChildAt(1));
    right_keys->push_back(expr->GetChildAt(0));
  }
  return true;
}

/**
 * @brief optimize nested loop join into hash join.
 * In the starter code, we will check NLJs with exactly one equal condition. You can further support optimizing joins
 * with multiple eq conditions.
 */
auto Optimizer::OptimizeNLJAsHashJoin(const AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeNLJAsHashJoin(child));
  }
  auto optimized_plan = plan->CloneWithChildren(std::move(children));

  if (optimized_plan->GetType() == PlanType::NestedLoopJoin) {
    const auto &nlj_plan = dynamic_cast<const NestedLoopJoinPlanNode &>(*optimized_plan);

    std::vector<AbstractExpressionRef> left_keys;
    std::vector<AbstractExpressionRef> right_keys;
    if (ExtractEquiConditions(nlj_plan.Predicate(), &left_keys, &right_keys)) {
      return std::make_shared<HashJoinPlanNode>(nlj_plan.output_schema_, nlj_plan.GetLeftPlan(),
                                                nlj_plan.GetRightPlan(), std::move(left_keys), std::move(right_keys),
                                                nlj_plan.GetJoinType());
    }
  }

  return optimized_plan;
}

}  // namespace bustub
