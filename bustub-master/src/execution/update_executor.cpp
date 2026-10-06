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
#include "catalog/catalog.h"
#include "common/config.h"
#include "common/macros.h"
#include "execution/execution_common.h"

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
      child_executor_(std::move(child_executor)) {}

/** Initialize the update */
void UpdateExecutor::Init() {
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
  tuple_batch->clear();
  rid_batch->clear();

  if (done_) {
    return false;
  }

  int32_t updated_count = 0;
  std::vector<Tuple> child_tuples;
  std::vector<RID> child_rids;
  std::vector<std::pair<RID, Tuple>> updates;
  std::unordered_set<RID> moved;
  auto indexes = exec_ctx_->GetCatalog()->GetTableIndexes(table_info_->name_);
  while (child_executor_->Next(&child_tuples, &child_rids, BUSTUB_BATCH_SIZE)) {
    for (size_t i = 0; i < child_tuples.size(); i++) {
      std::vector<Value> values;
      values.reserve(plan_->target_expressions_.size());
      for (const auto &expr : plan_->target_expressions_) {
        values.push_back(expr->Evaluate(&child_tuples[i], child_executor_->GetOutputSchema()));
      }
      Tuple target(values, &table_info_->schema_);
      for (const auto &index : indexes) {
        if (!index->is_primary_key_) {
          continue;
        }
        auto before =
            child_tuples[i].KeyFromTuple(table_info_->schema_, index->key_schema_, index->index_->GetKeyAttrs());
        auto after = target.KeyFromTuple(table_info_->schema_, index->key_schema_, index->index_->GetKeyAttrs());
        if (!IsTupleContentEqual(before, after)) {
          moved.insert(child_rids[i]);
        }
      }
      updates.emplace_back(child_rids[i], std::move(target));
    }
  }
  for (const auto &[rid, tuple] : updates) {
    ModifyTuple(exec_ctx_->GetTransactionManager(), exec_ctx_->GetTransaction(), table_info_, rid,
                moved.count(rid) == 0 ? &tuple : nullptr);
    updated_count++;
  }

  for (const auto &[rid, tuple] : updates) {
    if (moved.count(rid) != 0) {
      InsertTupleMvcc(exec_ctx_->GetCatalog(), exec_ctx_->GetTransactionManager(), exec_ctx_->GetTransaction(),
                      table_info_, tuple);
    }
  }
  tuple_batch->emplace_back(std::vector<Value>{Value(TypeId::INTEGER, updated_count)}, &GetOutputSchema());
  done_ = true;
  return true;
}

}  // namespace bustub
