//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_index_join_executor.h
//
// Identification: src/include/execution/executors/nested_index_join_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/nested_index_join_plan.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * NestedIndexJoinExecutor executes index join operations.
 */
class NestedIndexJoinExecutor : public AbstractExecutor {
 public:
  NestedIndexJoinExecutor(ExecutorContext *exec_ctx, const NestedIndexJoinPlanNode *plan,
                          std::unique_ptr<AbstractExecutor> &&child_executor);

  /** @return The output schema for the nested index join */
  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); }

  void Init() override;

  auto Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch, size_t batch_size)
      -> bool override;

 private:
  /** The nested index join plan node. */
  const NestedIndexJoinPlanNode *plan_;

  // HINT: you'll need `child_executor_` (the outer table), plus:
  //  - `const TableInfo *table_info_` and `const IndexInfo *index_info_`, looked up from the
  //    catalog via `plan_->GetInnerTableOid()` / `plan_->GetIndexOid()`
  //  - the concrete index type, e.g. `BPlusTreeIndexForTwoIntegerColumn *tree_ =
  //    dynamic_cast<BPlusTreeIndexForTwoIntegerColumn *>(index_info_->index_.get())` (see
  //    b_plus_tree_index.h)
  //  - batching state for the outer child (buffered tuples + cursor), similar to other executors
  // A `BuildJoinTuple(outer_tuple, inner_tuple_or_nullptr)` helper (outer columns, then
  // `plan_->InnerTableSchema()` columns, NULL-padded for a LEFT join non-match) will help.
};
}  // namespace bustub
