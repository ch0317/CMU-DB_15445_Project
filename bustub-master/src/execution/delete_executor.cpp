//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// delete_executor.cpp
//
// Identification: src/execution/delete_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include "common/macros.h"

#include "execution/executors/delete_executor.h"

namespace bustub {

/**
 * Construct a new DeleteExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The delete plan to be executed
 * @param child_executor The child executor that feeds the delete
 */
DeleteExecutor::DeleteExecutor(ExecutorContext *exec_ctx, const DeletePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx) {
  // HINT: store `plan_`, look up `table_info_` via the catalog, move in `child_executor_`.
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/** Initialize the delete */
void DeleteExecutor::Init() {
  // HINT: call `child_executor_->Init()` and reset a "already produced output" flag.
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/**
 * Yield the number of rows deleted from the table.
 * @param[out] tuple_batch The tuple batch with one integer indicating the number of rows deleted from the table
 * @param[out] rid_batch The next tuple RID batch produced by the delete (ignore, not used)
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 *
 * NOTE: DeleteExecutor::Next() does not use the `rid_batch` out-parameter.
 * NOTE: DeleteExecutor::Next() returns true with the number of deleted rows produced only once.
 */
auto DeleteExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                          size_t batch_size) -> bool {
  // HINT: pull (tuple, rid) batches from `child_executor_`. For each rid not already deleted
  // (check `table_info_->table_->GetTupleMeta(rid).is_deleted_`), call
  // `table_info_->table_->UpdateTupleMeta({0, true}, rid)` and remove the tuple's entry from every
  // index on the table (build the key with `tuple.KeyFromTuple(...)`, then `index_->DeleteEntry(...)`).
  // Emit ONE INTEGER tuple with the total deleted count once the child is exhausted; guard against
  // being called again afterwards with a "done" flag.
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

}  // namespace bustub
