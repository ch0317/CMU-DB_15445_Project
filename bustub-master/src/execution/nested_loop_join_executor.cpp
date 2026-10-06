//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_loop_join_executor.cpp
//
// Identification: src/execution/nested_loop_join_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_loop_join_executor.h"
#include "binder/table_ref/bound_join_ref.h"
#include "common/exception.h"
#include "common/macros.h"

namespace bustub {

/**
 * Construct a new NestedLoopJoinExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The nested loop join plan to be executed
 * @param left_executor The child executor that produces tuple for the left side of join
 * @param right_executor The child executor that produces tuple for the right side of join
 */
NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext *exec_ctx, const NestedLoopJoinPlanNode *plan,
                                               std::unique_ptr<AbstractExecutor> &&left_executor,
                                               std::unique_ptr<AbstractExecutor> &&right_executor)
    : AbstractExecutor(exec_ctx) {
  if (plan->GetJoinType() != JoinType::LEFT && plan->GetJoinType() != JoinType::INNER) {
    // Note for Spring 2025: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
  // HINT: store `plan_` and move in `left_executor_` / `right_executor_`.
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/** Initialize the join */
void NestedLoopJoinExecutor::Init() {
  // HINT: only `left_executor_->Init()` here — `right_executor_->Init()` should happen once per
  // left tuple inside Next() (see the header hint about the grader's re-Init check). Reset your
  // left-side batching state too.
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/**
 * Yield the next tuple batch from the join.
 * @param[out] tuple_batch The next tuple batch produced by the join
 * @param[out] rid_batch The next tuple RID batch produced by the join
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto NestedLoopJoinExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                                  size_t batch_size) -> bool {
  // HINT: use `plan_->Predicate()->EvaluateJoin(&left_tuple, left_schema, &right_tuple, right_schema)`
  // (see FilterExecutor for the general pattern of checking `!value.IsNull() && value.GetAs<bool>()`).
  // Outer loop: for each left tuple (pulled/batched from `left_executor_`), call
  // `right_executor_->Init()` ONCE, then scan all right tuples (batched from `right_executor_->Next()`)
  // looking for predicate matches, emitting a joined tuple for each. If it's a LEFT join and no match
  // was found by the time the right side is exhausted, emit one NULL-padded row instead. Stop once
  // `tuple_batch->size() == batch_size`, but remember your position (in the left batch, and in the
  // right-side scan for the *current* left tuple) so the next Next() call resumes correctly.
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

}  // namespace bustub
