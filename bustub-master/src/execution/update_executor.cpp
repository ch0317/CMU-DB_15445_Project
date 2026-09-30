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
    : AbstractExecutor(exec_ctx),
    plan_(plan),
    table_info_(exec_ctx->GetCatalog()->GetTable(plan->GetTableOid()).get()),
    child_executor_(std::move(child_executor)){
  // HINT: same shape as InsertExecutor's constructor — store `plan_`, look up `table_info_`
  // via `exec_ctx->GetCatalog()->GetTable(plan->GetTableOid())`, and move in `child_executor_`.
}

/** Initialize the update */
void UpdateExecutor::Init() {
  // HINT: call `child_executor_->Init()` and reset a "already produced output" flag.
  child_executor_->Init();
  done_ = false;
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
  tuple_batch->clear();
  rid_batch->clear();

  if(done_){
    return false;
  }

  auto *catalog = exec_ctx_->GetCatalog();
  auto indexes = catalog->GetTableIndexes(table_info_->name_);
  int updated_count = 0;
  std::vector<bustub::Tuple> child_tuples;
  std::vector<bustub::RID> child_rids;

  while(child_executor_->Next(&child_tuples, &child_rids, BUSTUB_BATCH_SIZE)){
    for(int i = 0; i < child_tuples.size(); i++){
      auto old_tuple = child_tuples[i];
      auto old_rid = child_rids[i];

      auto old_meta = table_info_->table_->GetTupleMeta(old_rid);
      if(old_meta.is_deleted_){
        continue;
      }

      std::vector<Value> new_values;
      new_values.reserve(plan_->target_expressions_.size());
      for (const auto &expr : plan_->target_expressions_) {
        new_values.push_back(expr->Evaluate(&old_tuple, child_executor_->GetOutputSchema()));
      }
      Tuple new_tuple(new_values, &table_info_->schema_);

      table_info_->table_->UpdateTupleMeta(TupleMeta{0, true}, old_rid);
      for(const auto &index_info : indexes){
        auto old_key = old_tuple.KeyFromTuple(table_info_->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());  
        index_info->index_->DeleteEntry(old_key, old_rid, exec_ctx_->GetTransaction());
      }

      TupleMeta new_meta{0, false};
      auto rid_opt = table_info_->table_->InsertTuple(new_meta, new_tuple);
      if(!rid_opt.has_value()){
        continue;
      }
      auto new_rid = rid_opt.value();
      for (const auto &index_info : indexes) {
        auto new_key = new_tuple.KeyFromTuple(table_info_->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
        index_info->index_->InsertEntry(new_key, new_rid, exec_ctx_->GetTransaction());
      }
      
      updated_count++;
    }
  }
  tuple_batch->emplace_back(std::vector<Value>{Value(TypeId::INTEGER, updated_count)}, &GetOutputSchema());
  done_ = true;
  return true;

}

}  // namespace bustub
