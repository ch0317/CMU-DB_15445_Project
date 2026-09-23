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
#include <optional>
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

  std::unique_ptr<AbstractExecutor> left_executor_;
  std::unique_ptr<AbstractExecutor> right_executor_;

  /** The current batch pulled from the left (outer) child, and our position within it. */
  std::vector<Tuple> left_tuples_;
  std::vector<RID> left_rids_;
  size_t left_idx_{0};

  /** The current batch pulled from the right (inner) child for the current left tuple. */
  std::vector<Tuple> right_tuples_;
  std::vector<RID> right_rids_;
  size_t right_idx_{0};

  /** State for the left tuple currently being joined against the (re-scanned) right side. */
  bool has_current_left_{false};
  Tuple current_left_tuple_;
  bool current_match_found_{false};

  /** Pulls the next right tuple for the current left tuple, re-filling batches as needed. */
  auto FetchNextRightTuple() -> std::optional<Tuple>;

  /** Builds an output tuple from a left tuple and an optional right tuple (nullopt for LEFT join padding). */
  auto BuildJoinTuple(const Tuple &left_tuple, const Tuple *right_tuple) -> Tuple;
};

}  // namespace bustub
