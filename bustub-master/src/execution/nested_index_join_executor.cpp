//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_index_join_executor.cpp
//
// Identification: src/execution/nested_index_join_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_index_join_executor.h"
#include "common/macros.h"

namespace bustub {

/**
 * Creates a new nested index join executor.
 * @param exec_ctx the context that the nested index join should be performed in
 * @param plan the nested index join plan to be executed
 * @param child_executor the outer table
 */
NestedIndexJoinExecutor::NestedIndexJoinExecutor(ExecutorContext *exec_ctx, const NestedIndexJoinPlanNode *plan,
                                                 std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx) {
  if (plan->GetJoinType() != JoinType::LEFT && plan->GetJoinType() != JoinType::INNER) {
    // Note for Spring 2025: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
  // HINT: store `plan_`, move in `child_executor_`, and look up `table_info_` / `index_info_` /
  // `tree_` from the catalog (see the header hint).
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

void NestedIndexJoinExecutor::Init() {
  // HINT: `child_executor_->Init()` and reset your outer-side batching state.
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

auto NestedIndexJoinExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                                   size_t batch_size) -> bool {
  // HINT: for each outer tuple (batched from `child_executor_`):
  // 1. Evaluate `plan_->KeyPredicate()->Evaluate(&outer_tuple, child_executor_->GetOutputSchema())`
  //    to get the join key value.
  // 2. If it's not NULL, wrap it in a `Tuple` built from `index_info_->index_->GetKeySchema()` and
  //    call `tree_->ScanKey(key_tuple, &result_rids, exec_ctx_->GetTransaction())` (only one, unique
  //    match is expected — see the index scan hints from Task 1).
  // 3. For each matching (non-deleted) inner tuple, emit a joined row. If it's a LEFT join and no
  //    match was found, emit one NULL-padded row instead.
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

}  // namespace bustub
