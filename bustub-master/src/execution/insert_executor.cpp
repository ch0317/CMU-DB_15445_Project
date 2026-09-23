//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// insert_executor.cpp
//
// Identification: src/execution/insert_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include "common/macros.h"

#include "execution/executors/insert_executor.h"

namespace bustub {

/**
 * Construct a new InsertExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The insert plan to be executed
 * @param child_executor The child executor from which inserted tuples are pulled
 */
InsertExecutor::InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx) {
  // HINT: initialize `plan_`, look up `table_info_` from the catalog using `plan->GetTableOid()`,
  // and store `child_executor` (moved) as `child_executor_`.
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/** Initialize the insert */
void InsertExecutor::Init() {
  // HINT: call `child_executor_->Init()` and reset the `done_` flag.
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/**
 * Yield the number of rows inserted into the table.
 * @param[out] tuple_batch The tuple batch with one integer indicating the number of rows inserted into the table
 * @param[out] rid_batch The next tuple RID batch produced by the insert (ignore, not used)
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 *
 * NOTE: InsertExecutor::Next() does not use the `rid_batch` out-parameter.
 * NOTE: InsertExecutor::Next() returns true with the number of inserted rows produced only once.
 */
auto InsertExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                          size_t batch_size) -> bool {
  // HINT:
  // 1. If `done_` is already true, return false (this executor only ever produces one output row).
  // 2. Pull tuples from `child_executor_->Next(...)` in a loop (batch_size can be BUSTUB_BATCH_SIZE)
  //    until it returns false.
  // 3. For each tuple, call `table_info_->table_->InsertTuple({/*ts=*/0, /*is_deleted=*/false}, tuple)`
  //    to get a `RID`.
  // 4. For every index on this table (`catalog->GetTableIndexes(table_info_->name_)`), build the index
  //    key with `tuple.KeyFromTuple(table_info_->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs())`
  //    and call `index_info->index_->InsertEntry(key, rid, exec_ctx_->GetTransaction())`.
  // 5. Count how many rows were inserted, push ONE tuple of that INTEGER count into `tuple_batch`,
  //    set `done_ = true`, and return true.
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

}  // namespace bustub
