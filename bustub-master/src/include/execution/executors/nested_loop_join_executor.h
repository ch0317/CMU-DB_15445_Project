//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_loop_join_executor.h
//
// Identification: src/include/execution/executors/nested_loop_join_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/nested_loop_join_plan.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * NestedLoopJoinExecutor executes a nested-loop JOIN on two tables.
 */
class NestedLoopJoinExecutor : public AbstractExecutor {
 public:
  NestedLoopJoinExecutor(ExecutorContext *exec_ctx, const NestedLoopJoinPlanNode *plan,
                         std::unique_ptr<AbstractExecutor> &&left_executor,
                         std::unique_ptr<AbstractExecutor> &&right_executor);

  void Init() override;

  auto Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch, size_t batch_size)
      -> bool override;

  /** @return The output schema for the insert */
  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); };

 private:
  /** The NestedLoopJoin plan node to be executed. */
  const NestedLoopJoinPlanNode *plan_;

  // HINT: you'll need `left_executor_` and `right_executor_` (moved in from the constructor args),
  // plus enough state to implement the classic "for each outer tuple, rescan the whole inner table"
  // algorithm across possibly-many Next() calls:
  //  - batching state for the left (outer) child: buffered tuples + a cursor
  //  - the "current" left tuple being probed, and whether a match has been found for it yet
  //    (needed for LEFT joins: emit a NULL-padded row if no match was found after exhausting the
  //    right side)
  //  - batching state for the right (inner) child *for the current left tuple only* — the grader
  //    checks that you call `right_executor_->Init()` again for (roughly) every left tuple, so do
  //    NOT materialize the whole right table once in Init().
  // A helper like `BuildJoinTuple(left_tuple, right_tuple_or_nullptr)` that concatenates left and
  // right schema columns (using `ValueFactory::GetNullValueByType` for the missing side on a LEFT
  // join non-match) will keep Next() readable.
};

}  // namespace bustub
