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

#include "optimizer/optimizer.h"

namespace bustub {

/**
 * @brief Optimizes seq scan as index scan if there's an index on a table
 */
auto Optimizer::OptimizeSeqScanAsIndexScan(const bustub::AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  // TODO(P3): implement seq scan with predicate -> index scan optimizer rule
  // The Filter Predicate Pushdown has been enabled for you in optimizer.cpp when forcing starter rule

  // HINT: recurse into children first (see order_by_index_scan.cpp / merge_filter_scan.cpp for the pattern),
  // then check if `optimized_plan` is a SeqScan with a non-null `filter_predicate_`.
  //
  // Write a helper that walks the predicate and, if it consists ONLY of:
  //   - a single `ColumnValueExpression == ConstantValueExpression` (in either order), or
  //   - several such equalities on the SAME column joined by OR (see LogicType::Or in
  //     logic_expression.h),
  // returns the column index plus the list of constant-value expressions to look up.
  // Anything else (e.g. AND, other columns, other comparisons) should bail out (return the seq scan
  // unchanged) — see the "WHERE v1 = 1 AND v2 = 2" note in the task doc.
  //
  // Then look up `catalog_.GetTableIndexes(table_info->name_)` and find one whose
  // `index_->GetKeyAttrs()` is exactly `{col_idx}`. If found, build and return an
  // `IndexScanPlanNode(output_schema, table_oid, index_oid, filter_predicate_, pred_keys)`.
  return plan;
}

}  // namespace bustub
