//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_scan_executor.cpp
//
// Identification: src/execution/index_scan_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/index_scan_executor.h"
#include "common/config.h"
#include "common/macros.h"

namespace bustub {

/**
 * Creates a new index scan executor.
 * @param exec_ctx the executor context
 * @param plan the index scan plan to be executed
 */
IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      table_info_(exec_ctx->GetCatalog()->GetTable(plan->table_oid_).get()),
      index_info_(exec_ctx->GetCatalog()->GetIndex(plan->GetIndexOid()).get()),
      tree_(dynamic_cast<BPlusTreeIndexForTwoIntegerColumn *>(index_info_->index_.get())) {}

void IndexScanExecutor::Init() {
  lookup_rids_.clear();
  lookup_cursor_ = 0;

  if (!plan_->pred_keys_.empty()) {
    Schema dummy_schema{std::vector<Column>{}};
    for (const auto &key_expr : plan_->pred_keys_) {
      auto value = key_expr->Evaluate(nullptr, dummy_schema);
      Tuple key_tuple(std::vector<Value>{value}, index_info_->index_->GetKeySchema());
      std::vector<RID> result;
      tree_->ScanKey(key_tuple, &result, exec_ctx_->GetTransaction());
      lookup_rids_.insert(lookup_rids_.end(), result.begin(), result.end());
    }
    iterator_.reset();
  } else {
    iterator_.reset(new BPlusTreeIndexIteratorForTwoIntegerColumn(tree_->GetBeginIterator()));
  }
}

auto IndexScanExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                             size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  if (!plan_->pred_keys_.empty()) {
    while (lookup_cursor_ < lookup_rids_.size() && tuple_batch->size() < batch_size) {
      auto rid = lookup_rids_[lookup_cursor_++];
      auto [meta, tuple] = table_info_->table_->GetTuple(rid);
      if (meta.is_deleted_) {
        continue;
      }
      tuple_batch->push_back(tuple);
      rid_batch->push_back(rid);
    }
  } else {
    while (!iterator_->IsEnd() && tuple_batch->size() < batch_size) {
      auto rid = (**iterator_).second;
      ++(*iterator_);

      auto [meta, tuple] = table_info_->table_->GetTuple(rid);
      if (meta.is_deleted_) {
        continue;
      }
      tuple_batch->push_back(tuple);
      rid_batch->push_back(rid);
    }
  }

  return !tuple_batch->empty();
}

}  // namespace bustub
