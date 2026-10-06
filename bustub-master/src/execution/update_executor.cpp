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
  while (child_executor_->Next(&child_tuples, &child_rids, BUSTUB_BATCH_SIZE)) {
    for (size_t i = 0; i < child_tuples.size(); i++) {
      std::vector<Value> values;
      for (const auto &expr : plan_->target_expressions_) {
        values.push_back(expr->Evaluate(&child_tuples[i], child_executor_->GetOutputSchema()));
      }
      updates.emplace_back(child_rids[i], Tuple(values, &table_info_->schema_));
    }
  }
  for (const auto &[rid, tuple] : updates) {
    ModifyTuple(exec_ctx_->GetTransactionManager(), exec_ctx_->GetTransaction(), table_info_, rid, &tuple);
    updated_count++;
  }

  tuple_batch->emplace_back(std::vector<Value>{Value(TypeId::INTEGER, updated_count)}, &GetOutputSchema());
  done_ = true;
  return true;
}

}  // namespace bustub
