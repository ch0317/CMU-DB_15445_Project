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
#include "catalog/catalog.h"
#include "common/config.h"
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
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      table_info_(exec_ctx->GetCatalog()->GetTable(plan->GetTableOid()).get()),
      child_executor_(std::move(child_executor)) {}

/** Initialize the insert */
void InsertExecutor::Init() {
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
  tuple_batch->clear();
  rid_batch->clear();

  if (done_) {
    return false;
  }

  auto *catalog = exec_ctx_->GetCatalog();
  auto indexes = catalog->GetTableIndexes(table_info_->name_);

  int32_t inserted_count = 0;
  std::vector<Tuple> child_tuples;
  std::vector<RID> child_rids;

  while (child_executor_->Next(&child_tuples, &child_rids, BUSTUB_BATCH_SIZE)) {
    for (auto &tuple : child_tuples) {
      TupleMeta meta{0, false};
      auto rid_opt = table_info_->table_->InsertTuple(meta, tuple);
      if (!rid_opt.has_value()) {
        continue;
      }
      auto rid = rid_opt.value();

      for (const auto &index_info : indexes) {
        auto key = tuple.KeyFromTuple(table_info_->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
        index_info->index_->InsertEntry(key, rid, exec_ctx_->GetTransaction());
      }

      inserted_count += 1;
    }
  }

  tuple_batch->emplace_back(std::vector<Value>{Value(TypeId::INTEGER, inserted_count)}, &GetOutputSchema());
  done_ = true;
  return true;
}

}  // namespace bustub
