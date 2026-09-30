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
    : AbstractExecutor(exec_ctx) ,
    plan_(plan),
    table_info_(exec_ctx->GetCatalog()->GetTable(plan_->GetTableOid()).get()),
    child_executor_(std::move(child_executor)){
  // HINT: initialize `plan_`, look up `table_info_` from the catalog using `plan->GetTableOid()`,
  // and store `child_executor` (moved) as `child_executor_`.
  
}

/** Initialize the insert */
void InsertExecutor::Init() {
  // HINT: call `child_executor_->Init()` and reset the `done_` flag.
  child_executor_->Init();
  done_ = false;
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
  tuple_batch->clear();
  rid_batch->clear();

  if(done_){
    return false;
  }

  std::vector<bustub::Tuple> child_tuple;
  std::vector<bustub::RID> rid_batch;
  int insert_count = 0;

  auto *catalog = exec_ctx_->GetCatalog();
  auto indexes = catalog->GetTableIndexes(table_info_->name_);

  while(child_executor_->Next(&child_tuple, &rid_batch, BUSTUB_BATCH_SIZE)) {
    for(int i = 0; i < child_tuple.size(); i++){
      TupleMeta meta{0, false};
      auto rid_opt = table_info_->table_->InsertTuple(meta, child_tuple[i]);
      if(!rid_opt.has_value()){
        continue;
      }

      auto rid = rid_opt.value();

      for(const auto &index_info : indexes) {
        auto key = child_tuple[i].KeyFromTuple(table_info_->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
        index_info->index_->InsertEntry(key, rid, exec_ctx_->GetTransaction());
      }

      insert_count++;

    }
  }

  tuple_batch->emplace_back(std::vector<Value>{Value(TypeId::INTEGER, insert_count)}, &GetOutputSchema());
  done_ = true;
  return true;
}

}  // namespace bustub
