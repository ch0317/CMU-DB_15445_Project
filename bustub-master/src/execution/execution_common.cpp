//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// execution_common.cpp
//
// Identification: src/execution/execution_common.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/execution_common.h"

#include "catalog/catalog.h"
#include "common/macros.h"
#include "concurrency/transaction_manager.h"
#include "fmt/core.h"
#include "storage/table/table_heap.h"

namespace bustub {

TupleComparator::TupleComparator(std::vector<OrderBy> order_bys) : order_bys_(std::move(order_bys)) {}

/** TODO(P3): Implement the comparison method */
auto TupleComparator::operator()(const SortEntry &entry_a, const SortEntry &entry_b) const -> bool { return false; }

/**
 * Generate sort key for a tuple based on the order by expressions.
 *
 * TODO(P3): Implement this method.
 */
auto GenerateSortKey(const Tuple &tuple, const std::vector<OrderBy> &order_bys, const Schema &schema) -> SortKey {
  return {};
}

/**
 * Above are all you need for P3.
 * You can ignore the remaining part of this file until P4.
 */

/**
 * @brief Reconstruct a tuple by applying the provided undo logs from the base tuple. All logs in the undo_logs are
 * applied regardless of the timestamp
 *
 * @param schema The schema of the base tuple and the returned tuple.
 * @param base_tuple The base tuple to start the reconstruction from.
 * @param base_meta The metadata of the base tuple.
 * @param undo_logs The list of undo logs to apply during the reconstruction, the front is applied first.
 * @return An optional tuple that represents the reconstructed tuple. If the tuple is deleted as the result, returns
 * std::nullopt.
 */
auto ReconstructTuple(const Schema *schema, const Tuple &base_tuple, const TupleMeta &base_meta,
                      const std::vector<UndoLog> &undo_logs) -> std::optional<Tuple> {
  std::vector<Value> values;
  for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
    values.push_back(base_tuple.GetValue(schema, i));
  }
  bool deleted = base_meta.is_deleted_;
  for (const auto &log : undo_logs) {
    std::vector<Column> columns;
    for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
      if (log.modified_fields_[i]) {
        columns.push_back(schema->GetColumn(i));
      }
    }
    Schema undo_schema(columns);
    uint32_t index = 0;
    for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
      if (log.modified_fields_[i]) {
        values[i] = log.tuple_.GetValue(&undo_schema, index++);
      }
    }
    deleted = log.is_deleted_;
  }
  if (deleted) {
    return std::nullopt;
  }
  return Tuple(values, schema);
}

/**
 * @brief Collects the undo logs sufficient to reconstruct the tuple w.r.t. the txn.
 *
 * @param rid The RID of the tuple.
 * @param base_meta The metadata of the base tuple.
 * @param base_tuple The base tuple.
 * @param undo_link The undo link to the latest undo log.
 * @param txn The transaction.
 * @param txn_mgr The transaction manager.
 * @return An optional vector of undo logs to pass to ReconstructTuple(). std::nullopt if the tuple did not exist at the
 * time.
 */
auto CollectUndoLogs(RID rid, const TupleMeta &base_meta, const Tuple &base_tuple, std::optional<UndoLink> undo_link,
                     Transaction *txn, TransactionManager *txn_mgr) -> std::optional<std::vector<UndoLog>> {
  std::vector<UndoLog> logs;
  if (base_meta.ts_ <= txn->GetReadTs() || base_meta.ts_ == txn->GetTransactionTempTs()) {
    return logs;
  }
  while (undo_link.has_value() && undo_link->IsValid()) {
    auto log = txn_mgr->GetUndoLog(*undo_link);
    logs.push_back(log);
    if (log.ts_ <= txn->GetReadTs()) {
      return logs;
    }
    undo_link = log.prev_version_;
  }
  return std::nullopt;
}

/**
 * @brief Generates a new undo log as the transaction tries to modify this tuple at the first time.
 *
 * @param schema The schema of the table.
 * @param base_tuple The base tuple before the update, the one retrieved from the table heap. nullptr if the tuple is
 * deleted.
 * @param target_tuple The target tuple after the update. nullptr if this is a deletion.
 * @param ts The timestamp of the base tuple.
 * @param prev_version The undo link to the latest undo log of this tuple.
 * @return The generated undo log.
 */
auto GenerateNewUndoLog(const Schema *schema, const Tuple *base_tuple, const Tuple *target_tuple, timestamp_t ts,
                        UndoLink prev_version) -> UndoLog {
  UNIMPLEMENTED("not implemented");
}

/**
 * @brief Generate the updated undo log to replace the old one, whereas the tuple is already modified by this txn once.
 *
 * @param schema The schema of the table.
 * @param base_tuple The base tuple before the update, the one retrieved from the table heap. nullptr if the tuple is
 * deleted.
 * @param target_tuple The target tuple after the update. nullptr if this is a deletion.
 * @param log The original undo log.
 * @return The updated undo log.
 */
auto GenerateUpdatedUndoLog(const Schema *schema, const Tuple *base_tuple, const Tuple *target_tuple,
                            const UndoLog &log) -> UndoLog {
  UNIMPLEMENTED("not implemented");
}

void TxnMgrDbg(const std::string &info, TransactionManager *txn_mgr, const TableInfo *table_info,
               TableHeap *table_heap) {
  fmt::println(stderr, "debug_hook: {}", info);
  for (auto iter = table_heap->MakeIterator(); !iter.IsEnd(); ++iter) {
    auto rid = iter.GetRID();
    auto [meta, tuple, link] = GetTupleAndUndoLink(txn_mgr, table_heap, rid);
    fmt::println(stderr, "RID={}/{} ts={} deleted={} tuple={}", rid.GetPageId(), rid.GetSlotNum(), meta.ts_,
                 meta.is_deleted_, tuple.ToString(&table_info->schema_));
    while (link.has_value() && link->IsValid()) {
      auto log = txn_mgr->GetUndoLogOptional(*link);
      if (!log.has_value()) {
        break;
      }
      fmt::println(stderr, "  txn={} log={} ts={} deleted={}", link->prev_txn_ ^ TXN_START_ID, link->prev_log_idx_,
                   log->ts_, log->is_deleted_);
      link = log->prev_version_;
    }
  }
}

}  // namespace bustub
