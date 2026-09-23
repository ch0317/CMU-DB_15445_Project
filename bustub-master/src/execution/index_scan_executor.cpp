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
#include "common/macros.h"

namespace bustub {

/**
 * Creates a new index scan executor.
 * @param exec_ctx the executor context
 * @param plan the index scan plan to be executed
 */
IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx) {
  // HINT: store `plan_`, then via `exec_ctx->GetCatalog()` look up:
  //  - `table_info_`  = `GetTable(plan->table_oid_)`
  //  - `index_info_`  = `GetIndex(plan->GetIndexOid())`
  // Cast the index to the concrete type used in this project:
  //   `dynamic_cast<BPlusTreeIndexForTwoIntegerColumn *>(index_info_->index_.get())`
  // and store it (e.g. as `tree_`).
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

void IndexScanExecutor::Init() {
  // HINT: this executor has two modes, decided by `plan_->pred_keys_`:
  //  - Point lookup (`pred_keys_` non-empty): for each key expression, `Evaluate(nullptr, dummy_schema)`
  //    it to get a `Value`, wrap it in a `Tuple` built from `index_info_->index_->GetKeySchema()`, and
  //    call `tree_->ScanKey(key_tuple, &result_rids, txn)` (see the `ScanKey` hint in the task doc).
  //    Accumulate the RIDs from every key so several point lookups on the same index are supported.
  //  - Ordered scan (`pred_keys_` empty): get an iterator via `tree_->GetBeginIterator()`. Note
  //    `IndexIterator` has no copy/move constructor (it owns a page guard) — you cannot store it in
  //    `std::optional`; use e.g. `std::unique_ptr<...>` and `iterator_.reset(new Iterator(tree_->GetBeginIterator()))`
  //    so the return value is constructed in place (guaranteed copy elision).
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

auto IndexScanExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                             size_t batch_size) -> bool {
  // HINT:
  //  - Point lookup mode: walk your accumulated RID list with a cursor, batching up to `batch_size`.
  //  - Ordered scan mode: while `!iterator_->IsEnd()`, dereference (`**iterator_` gives a
  //    `std::pair<const KeyType&, const ValueType&>`, `.second` is the RID) then `++(*iterator_)`.
  // In both modes, fetch the full tuple via `table_info_->table_->GetTuple(rid)` and skip it if
  // `meta.is_deleted_` is true (do not emit deleted tuples).
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

}  // namespace bustub
