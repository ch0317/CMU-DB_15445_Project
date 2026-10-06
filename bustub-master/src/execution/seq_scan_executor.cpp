//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// seq_scan_executor.cpp
//
// Identification: src/execution/seq_scan_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/seq_scan_executor.h"
#include "common/macros.h"
#include "concurrency/transaction_manager.h"
#include "execution/execution_common.h"

namespace bustub {

/**
 * Construct a new SeqScanExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The sequential scan plan to be executed
 */
SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

/** Initialize the sequential scan */
void SeqScanExecutor::Init() {
  table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->GetTableOid()).get();
  iterator_.emplace(table_info_->table_->MakeIterator());
}

/**
 * Yield the next tuple batch from the seq scan.
 * @param[out] tuple_batch The next tuple batch produced by the scan
 * @param[out] rid_batch The next tuple RID batch produced by the scan
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto SeqScanExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                           size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  while (!iterator_->IsEnd() && tuple_batch->size() < batch_size) {
    auto rid = iterator_->GetRID();
    auto [tuple_meta, base, link] =
        GetTupleAndUndoLink(exec_ctx_->GetTransactionManager(), table_info_->table_.get(), rid);

    ++(*iterator_);

    auto logs =
        CollectUndoLogs(rid, tuple_meta, base, link, exec_ctx_->GetTransaction(), exec_ctx_->GetTransactionManager());
    if (!logs.has_value()) {
      continue;
    }
    auto visible = ReconstructTuple(&table_info_->schema_, base, tuple_meta, *logs);
    if (!visible.has_value()) {
      continue;
    }
    auto tuple = *visible;
    tuple.SetRid(rid);

    if (plan_->filter_predicate_ != nullptr) {
      auto value = plan_->filter_predicate_->Evaluate(&tuple, table_info_->schema_);

      if (value.IsNull() || !value.GetAs<bool>()) {
        continue;
      }
    }

    tuple_batch->push_back(tuple);
    rid_batch->push_back(rid);
  }

  return !tuple_batch->empty();
}

}  // namespace bustub
