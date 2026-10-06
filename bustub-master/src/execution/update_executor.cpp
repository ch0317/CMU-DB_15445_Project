//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// update_executor.cpp
//
// Identification: src/execution/update_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include "common/macros.h"

#include "execution/executors/update_executor.h"

namespace bustub {

/**
 * Construct a new UpdateExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The update plan to be executed
 * @param child_executor The child executor that feeds the update
 */
UpdateExecutor::UpdateExecutor(ExecutorContext *exec_ctx, const UpdatePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx) {
  // HINT: same shape as InsertExecutor's constructor — store `plan_`, look up `table_info_`
  // via `exec_ctx->GetCatalog()->GetTable(plan->GetTableOid())`, and move in `child_executor_`.
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/** Initialize the update */
void UpdateExecutor::Init() {
  // HINT: call `child_executor_->Init()` and reset a "already produced output" flag.
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/**
 * Yield the number of rows updated in the table.
 * @param[out] tuple_batch The tuple batch with one integer indicating the number of rows updated in the table
 * @param[out] rid_batch The next tuple RID batch produced by the update (ignore, not used)
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 *
 * NOTE: UpdateExecutor::Next() does not use the `rid_batch` out-parameter.
 * NOTE: UpdateExecutor::Next() returns true with the number of updated rows produced only once.
 */
auto UpdateExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                          size_t batch_size) -> bool {
  // HINT: the task doc says "to implement an update, first delete the affected tuple and then
  // insert a new tuple". For each (old_tuple, old_rid) pulled from `child_executor_`:
  // 1. Skip it if `table_info_->table_->GetTupleMeta(old_rid).is_deleted_` is already true.
  // 2. Evaluate `plan_->target_expressions_[i]->Evaluate(&old_tuple, child_executor_->GetOutputSchema())`
  //    for each column to build the new tuple's values, then construct `Tuple(new_values, &table_info_->schema_)`.
  // 3. Mark the old RID deleted via `UpdateTupleMeta({0, true}, old_rid)` and remove its index entries
  //    (`DeleteEntry`, keyed the same way as in InsertExecutor).
  // 4. Insert the new tuple (`InsertTuple({0, false}, new_tuple)`) and add its index entries.
  // 5. Count updates, emit one INTEGER-count tuple once all input is consumed, guard against calling
  //    twice with a "done" flag.
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

}  // namespace bustub
