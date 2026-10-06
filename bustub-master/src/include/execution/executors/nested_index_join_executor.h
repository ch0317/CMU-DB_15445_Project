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

#include "catalog/catalog.h"
#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/nested_index_join_plan.h"
#include "storage/index/b_plus_tree_index.h"
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

  std::unique_ptr<AbstractExecutor> child_executor_;

  const TableInfo *table_info_;
  const IndexInfo *index_info_;
  BPlusTreeIndexForTwoIntegerColumn *tree_;

  /** The current batch pulled from the outer (child) side, and our position within it. */
  std::vector<Tuple> outer_tuples_;
  std::vector<RID> outer_rids_;
  size_t outer_idx_{0};

  /** Builds an output tuple from an outer tuple and an optional inner tuple (nullptr for LEFT join padding). */
  auto BuildJoinTuple(const Tuple &outer_tuple, const Tuple *inner_tuple) -> Tuple;
};
}  // namespace bustub
