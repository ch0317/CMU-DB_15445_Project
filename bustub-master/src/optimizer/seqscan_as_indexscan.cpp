//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// seqscan_as_indexscan.cpp
//
// Identification: src/optimizer/seqscan_as_indexscan.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "catalog/catalog.h"
#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/expressions/logic_expression.h"
#include "execution/plans/filter_plan.h"
#include "execution/plans/index_scan_plan.h"
#include "execution/plans/seq_scan_plan.h"
#include "optimizer/optimizer.h"

namespace bustub {

/**
 * @brief Recursively collects "column = constant" equality checks on a single column from a predicate
 * that consists only of such equalities combined with OR. Returns std::nullopt if the predicate
 * does not match this shape.
 */
static auto CollectEqualityKeys(const AbstractExpressionRef &expr, uint32_t *col_idx)
    -> std::optional<std::vector<AbstractExpressionRef>> {
  if (const auto *logic_expr = dynamic_cast<const LogicExpression *>(expr.get()); logic_expr != nullptr) {
    if (logic_expr->logic_type_ != LogicType::Or) {
      return std::nullopt;
    }
    auto left = CollectEqualityKeys(expr->GetChildAt(0), col_idx);
    if (!left.has_value()) {
      return std::nullopt;
    }
    auto right = CollectEqualityKeys(expr->GetChildAt(1), col_idx);
    if (!right.has_value()) {
      return std::nullopt;
    }
    left->insert(left->end(), right->begin(), right->end());
    return left;
  }

  if (const auto *cmp_expr = dynamic_cast<const ComparisonExpression *>(expr.get()); cmp_expr != nullptr) {
    if (cmp_expr->comp_type_ != ComparisonType::Equal) {
      return std::nullopt;
    }
    const auto *col_expr = dynamic_cast<const ColumnValueExpression *>(expr->GetChildAt(0).get());
    AbstractExpressionRef const_expr = expr->GetChildAt(1);
    if (col_expr == nullptr) {
      col_expr = dynamic_cast<const ColumnValueExpression *>(expr->GetChildAt(1).get());
      const_expr = expr->GetChildAt(0);
    }
    if (col_expr == nullptr || dynamic_cast<const ConstantValueExpression *>(const_expr.get()) == nullptr) {
      return std::nullopt;
    }
    if (*col_idx == static_cast<uint32_t>(-1)) {
      *col_idx = col_expr->GetColIdx();
    } else if (*col_idx != col_expr->GetColIdx()) {
      return std::nullopt;
    }
    return std::vector<AbstractExpressionRef>{const_expr};
  }

  return std::nullopt;
}

/**
 * @brief Optimizes seq scan as index scan if there's an index on a table
 */
auto Optimizer::OptimizeSeqScanAsIndexScan(const bustub::AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeSeqScanAsIndexScan(child));
  }
  auto optimized_plan = plan->CloneWithChildren(std::move(children));

  if (optimized_plan->GetType() == PlanType::SeqScan) {
    const auto &seq_scan_plan = dynamic_cast<const SeqScanPlanNode &>(*optimized_plan);
    if (seq_scan_plan.filter_predicate_ != nullptr) {
      uint32_t col_idx = static_cast<uint32_t>(-1);
      auto keys = CollectEqualityKeys(seq_scan_plan.filter_predicate_, &col_idx);
      if (keys.has_value()) {
        const auto table_info = catalog_.GetTable(seq_scan_plan.GetTableOid());
        const auto indices = catalog_.GetTableIndexes(table_info->name_);
        for (const auto &index : indices) {
          const auto &key_attrs = index->index_->GetKeyAttrs();
          if (key_attrs.size() == 1 && key_attrs[0] == col_idx) {
            return std::make_shared<IndexScanPlanNode>(optimized_plan->output_schema_, table_info->oid_,
                                                       index->index_oid_, seq_scan_plan.filter_predicate_, *keys);
          }
        }
      }
    }
  }

  return optimized_plan;
}

}  // namespace bustub
