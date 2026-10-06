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
#include <unordered_set>
#include "common/config.h"
#include "common/macros.h"
#include "concurrency/transaction_manager.h"
#include "execution/execution_common.h"

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
  exec_ctx_->GetTransaction()->AppendScanPredicate(table_info_->oid_, plan_->filter_predicate_);
  lookup_rids_.clear();
  lookup_cursor_ = 0;

  if (!plan_->pred_keys_.empty()) {
    Schema dummy_schema{std::vector<Column>{}};
    for (const auto &key_expr : plan_->pred_keys_) {
      auto value = key_expr->Evaluate(nullptr, dummy_schema);
      Tuple key_tuple(std::vector<Value>{value}, index_info_->index_->GetKeySchema());
      std::vector<RID> result;
      index_info_->index_->ScanKey(key_tuple, &result, exec_ctx_->GetTransaction());
      lookup_rids_.insert(lookup_rids_.end(), result.begin(), result.end());
    }
    std::unordered_set<RID> seen;
    std::vector<RID> unique;
    for (auto rid : lookup_rids_) {
      if (seen.insert(rid).second) {
        unique.push_back(rid);
      }
    }
    lookup_rids_ = std::move(unique);
    iterator_.reset();
  } else {
    // IndexIterator has no copy/move constructor (it owns a page guard), so std::make_unique cannot
    // forward the return value of GetBeginIterator() here; direct-initialization via `new` relies on
    // guaranteed copy elision instead.
    iterator_.reset(new BPlusTreeIndexIteratorForTwoIntegerColumn(tree_->GetBeginIterator()));  // NOLINT
  }
}

auto IndexScanExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                             size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  while (tuple_batch->size() < batch_size) {
    RID rid;
    if (!plan_->pred_keys_.empty()) {
      if (lookup_cursor_ == lookup_rids_.size()) {
        break;
      }
      rid = lookup_rids_[lookup_cursor_++];
    } else {
      if (iterator_->IsEnd()) {
        break;
      }
      rid = (**iterator_).second;
      ++(*iterator_);
    }
    auto [meta, base, link] = GetTupleAndUndoLink(exec_ctx_->GetTransactionManager(), table_info_->table_.get(), rid);
    auto logs = CollectUndoLogs(rid, meta, base, link, exec_ctx_->GetTransaction(), exec_ctx_->GetTransactionManager());
    if (!logs.has_value()) {
      continue;
    }
    auto tuple = ReconstructTuple(&table_info_->schema_, base, meta, *logs);
    if (!tuple.has_value()) {
      continue;
    }
    if (plan_->filter_predicate_ != nullptr) {
      auto value = plan_->filter_predicate_->Evaluate(&*tuple, table_info_->schema_);
      if (value.IsNull() || !value.GetAs<bool>()) {
        continue;
      }
    }
    tuple->SetRid(rid);
    tuple_batch->push_back(*tuple);
    rid_batch->push_back(rid);
  }

  return !tuple_batch->empty();
}

}  // namespace bustub
